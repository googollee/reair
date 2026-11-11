#include "ota.h"

#include <esp_app_format.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#include <app/clusters/ota-requestor/ExtendedOTARequestorDriver.h>
#include <app/clusters/ota-requestor/OTARequestorInterface.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>

// 仅测试用:1 自检失败,2 启动即崩溃,3 自检永不完成(超时)。只对待验证的新固件生效;发布构建为 0
#ifndef REAIR_OTA_TEST_MODE
#define REAIR_OTA_TEST_MODE 0
#endif

namespace reair {
namespace ota {
namespace {

const char *TAG = "ota";

constexpr const char *kNamespace = "reair";
constexpr const char *kFailedKey = "ota_failed";
constexpr uint32_t kPollMs = 500;

SelfTest selftest;
bool pending = false;  // 运行的是刚 OTA 的新固件,还没有确认
uint32_t failed_version = 0;
esp_timer_handle_t timeout_timer = nullptr;

uint32_t load_failed_version()
{
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }
    uint32_t value = 0;
    if (nvs_get_u32(handle, kFailedKey, &value) != ESP_OK) {
        value = 0;
    }
    nvs_close(handle);
    return value;
}

void store_failed_version(uint32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, kFailedKey, value);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to record rolled back version: %s", esp_err_to_name(err));
    }
}

[[noreturn]] void rollback(const char *reason)
{
    ESP_LOGE(TAG, "Self test failed (%s), rolling back to the previous firmware", reason);
    esp_ota_mark_app_invalid_rollback_and_reboot();
    // 只有没有可回退的固件时才会返回,此时重启,由引导程序处理
    esp_restart();
}

void on_timeout(void *)
{
    ESP_LOGE(TAG, "Self test timed out, missing items 0x%02x", selftest.missing());
    rollback("timeout");
}

// 刚被回退的固件留在另一个应用分区里,状态是 ABORTED(引导程序回退)或 INVALID(自检失败主动回退)
void record_rolled_back_firmware()
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);
    if (other == nullptr || other == running) {
        return;
    }
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(other, &state) != ESP_OK ||
        (state != ESP_OTA_IMG_ABORTED && state != ESP_OTA_IMG_INVALID)) {
        return;
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(other, &desc) != ESP_OK) {
        return;
    }
    uint32_t version = 0;
    if (!parse_version(desc.version, version)) {
        ESP_LOGW(TAG, "Rolled back firmware has no numeric version (\"%s\"), not recorded", desc.version);
        return;
    }
    if (version != failed_version) {
        ESP_LOGW(TAG, "Firmware version %" PRIu32 " was rolled back, recorded", version);
        failed_version = version;
        store_failed_version(version);
    }
}

bool ble_ready()
{
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    auto &server = chip::Server::GetInstance();
    bool provisioned = server.GetFabricTable().FabricCount() > 0;
    if (provisioned || !server.GetCommissioningWindowManager().IsCommissioningWindowOpen()) {
        return true;  // 不需要蓝牙
    }
    auto &connectivity = chip::DeviceLayer::ConnectivityMgr();
    return connectivity.IsBLEAdvertising() || connectivity.NumBLEConnections() > 0;
}

void selftest_task(void *)
{
    while (!selftest.failed()) {
        if (!(selftest.missing() & kBle) || ble_ready()) {
            selftest.pass(kBle);
        }
        if (selftest.complete()) {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Self test passed, firmware confirmed");
            } else {
                ESP_LOGE(TAG, "Failed to confirm firmware: %s", esp_err_to_name(err));
            }
            if (timeout_timer != nullptr) {
                esp_timer_stop(timeout_timer);
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
    vTaskDelete(nullptr);
}

// 提议的软件版本号不大于被回退固件时拒绝。SDK 自带的“必须大于当前版本”检查在此之前
class GuardDriver : public chip::DeviceLayer::ExtendedOTARequestorDriver {
public:
    void UpdateAvailable(const chip::UpdateDescription &update, chip::System::Clock::Seconds32 delay) override
    {
        uint32_t current = 0;
        (void)chip::DeviceLayer::ConfigurationMgr().GetSoftwareVersion(current);
        if (!version_acceptable(update.softwareVersion, current, failed_version)) {
            ESP_LOGW(TAG, "Offered version %" PRIu32 " is not newer than rolled back version %" PRIu32 ", rejected",
                     update.softwareVersion, failed_version);
            (void)UpdateNotFound(chip::UpdateNotFoundReason::kUpToDate, delay);
            mRequestor->CancelImageUpdate();
            return;
        }
        ExtendedOTARequestorDriver::UpdateAvailable(update, delay);
    }
};

GuardDriver guard_driver;

}  // namespace

void init()
{
    failed_version = load_failed_version();

    const esp_app_desc_t *app = esp_app_get_description();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        pending = true;
    }
    ESP_LOGI(TAG, "Firmware version %s, built %s %s, %s, rolled back version %" PRIu32, app->version, app->date,
             app->time, pending ? "pending verification" : "confirmed", failed_version);

    if (pending) {
        esp_timer_create_args_t args = {};
        args.callback = &on_timeout;
        args.name = "ota_selftest";
        if (esp_timer_create(&args, &timeout_timer) == ESP_OK) {
            esp_timer_start_once(timeout_timer, static_cast<uint64_t>(CONFIG_REAIR_OTA_SELFTEST_TIMEOUT_S) * 1000000ULL);
        } else {
            rollback("no timer");
        }
    } else {
        record_rolled_back_firmware();
    }

#if REAIR_OTA_TEST_MODE != 0
    ESP_LOGW(TAG, "OTA test mode %d, never use in a release build", REAIR_OTA_TEST_MODE);
#endif
#if REAIR_OTA_TEST_MODE == 1
    if (pending) {
        check(kLed, ESP_FAIL);
    }
#elif REAIR_OTA_TEST_MODE == 2
    if (pending) {
        abort();
    }
#endif
}

void check(Item item, esp_err_t err)
{
    if (err == ESP_OK) {
        selftest.pass(item);
        return;
    }
    ESP_LOGE(TAG, "Self test item 0x%02x failed: %s", item, esp_err_to_name(err));
    selftest.fail(item);
    if (pending) {
        rollback("hardware init");
    }
}

void start_selftest()
{
    if (!pending) {
        return;
    }
#if REAIR_OTA_TEST_MODE == 3
    return;  // 测试:自检永不完成,等超时回退
#endif
    if (xTaskCreate(selftest_task, "ota_selftest", 4096, nullptr, 1, nullptr) != pdPASS) {
        rollback("no task");
    }
}

esp_err_t install_guard()
{
    static const esp_matter_ota_requestor_impl_t impl = {
        .driver = &guard_driver,
        .user_consent = nullptr,
        .image_processor = nullptr,
    };
    esp_matter_ota_config_t config = {};
    config.impl = &impl;
    return esp_matter_ota_requestor_set_config(config);
}

void clear_failed_record()
{
    failed_version = 0;
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, kFailedKey);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

}  // namespace ota
}  // namespace reair
