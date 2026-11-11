#include "led.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>

namespace reair {
namespace led {
namespace {

const char *TAG = "led";

constexpr int kGpio = CONFIG_REAIR_LED_GPIO;
constexpr uint32_t kTickMs = 100;
constexpr uint32_t kSlowHalfPeriodMs = 500;  // 1 Hz
constexpr uint32_t kFastHalfPeriodMs = 100;  // 5 Hz
// 配网窗口刚打开时蓝牙广播还没起来,超过这个时间仍没有广播才算异常
constexpr uint32_t kBleGraceMs = 3000;
// Wi-Fi 刚连上时链路本地地址还要几秒才分配好,超过这个时间仍没有才算异常
constexpr uint32_t kIpv6GraceMs = 5000;

enum class State { commissioning, commissioning_timeout, reconnecting, online, fault };

const char *name(State state)
{
    switch (state) {
    case State::commissioning:
        return "commissioning";
    case State::commissioning_timeout:
        return "commissioning timeout";
    case State::reconnecting:
        return "reconnecting";
    case State::online:
        return "online";
    default:
        return "fault";
    }
}

bool has_ipv6_link_local()
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_ip6_addr_t addr;
    return netif != nullptr && esp_netif_get_ip6_linklocal(netif, &addr) == ESP_OK;
}

State current_state(uint32_t &ble_missing_ms, uint32_t &no_ipv6_ms)
{
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    auto &server = chip::Server::GetInstance();
    auto &connectivity = chip::DeviceLayer::ConnectivityMgr();
    bool provisioned = server.GetFabricTable().FabricCount() > 0;

    if (!provisioned) {
        if (!server.GetCommissioningWindowManager().IsCommissioningWindowOpen()) {
            ble_missing_ms = 0;
            return State::commissioning_timeout;
        }
        // 控制器通过蓝牙连上后广播会自动停止,有连接时不算异常
        bool ble_ok = connectivity.IsBLEAdvertising() || connectivity.NumBLEConnections() > 0;
        ble_missing_ms = ble_ok ? 0 : ble_missing_ms + kTickMs;
        return ble_missing_ms > kBleGraceMs ? State::fault : State::commissioning;
    }
    ble_missing_ms = 0;
    if (!connectivity.IsWiFiStationConnected()) {
        no_ipv6_ms = 0;
        return State::reconnecting;
    }
    no_ipv6_ms = has_ipv6_link_local() ? 0 : no_ipv6_ms + kTickMs;
    return no_ipv6_ms > kIpv6GraceMs ? State::fault : (no_ipv6_ms > 0 ? State::reconnecting : State::online);
}

void write(bool on)
{
    gpio_set_level(static_cast<gpio_num_t>(kGpio), on ? 0 : 1);
}

void led_task(void *)
{
    uint32_t ble_missing_ms = 0;
    uint32_t no_ipv6_ms = 0;
    uint32_t elapsed_ms = 0;
    State shown = State::commissioning_timeout;
    bool first = true;
    for (;;) {
        State state = current_state(ble_missing_ms, no_ipv6_ms);
        if (first || state != shown) {
            ESP_LOGI(TAG, "state: %s", name(state));
            shown = state;
            elapsed_ms = 0;
            first = false;
        }
        switch (state) {
        case State::online:
            write(false);
            break;
        case State::commissioning_timeout:
            write(true);
            break;
        default: {
            uint32_t half = state == State::fault ? kFastHalfPeriodMs : kSlowHalfPeriodMs;
            write((elapsed_ms / half) % 2 == 0);
            break;
        }
        }
        elapsed_ms += kTickMs;
        vTaskDelay(pdMS_TO_TICKS(kTickMs));
    }
}

}  // namespace

esp_err_t start()
{
    if (kGpio < 0) {
        return ESP_OK;
    }
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << kGpio;
    config.mode = GPIO_MODE_OUTPUT;
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }
    write(false);  // 先熄灭再设为输出,GPIO8 是启动配置引脚,复位时要保持高电平

    if (xTaskCreate(led_task, "led", 3072, nullptr, 1, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create led task, led disabled");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

}  // namespace led
}  // namespace reair
