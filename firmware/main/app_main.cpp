#include <esp_err.h>
#include <cstdlib>
#include <cstring>

#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>

#include <app/clusters/fan-control-server/fan-control-server.h>
#include <common_macros.h>

#include <platform/CHIPDeviceLayer.h>

#include "button.h"
#include "fan.h"
#include "led.h"
#include "node_endpoints.h"
#include "node_sync.h"
#include "ota.h"

static const char *TAG = "reair";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static void refresh_roles(bool role_changed);

static reair::Endpoints eps;

static void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "IP address changed");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed");
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identify: type %u, effect %u, variant %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// 风扇三档对应的百分比;FanMode 与 PercentSetting 互相换算成档位
static const uint8_t tier_percent[] = {0, 33, 66, 100};
static const uint8_t max_tier = 3;

static uint8_t percent_to_tier(uint8_t percent)
{
    if (percent == 0) {
        return 0;
    }
    return percent <= 33 ? 1 : (percent <= 66 ? 2 : 3);
}

// 派生属性用 report 更新:不触发应用回调,不会递归
static void update_if_changed(uint16_t endpoint_id, uint32_t attribute_id, uint8_t value)
{
    attribute_t *attr = attribute::get(endpoint_id, FanControl::Id, attribute_id);
    esp_matter_attr_val_t val;
    if (attr == nullptr || attribute::get_val(attr, &val) != ESP_OK || val.val.u8 == value) {
        return;
    }
    val.val.u8 = value;
    esp_err_t err = attribute::report(endpoint_id, FanControl::Id, attribute_id, &val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "update attribute 0x%04" PRIx32 " to %u failed: %d", attribute_id, value, err);
    }
}

static void apply_fan_tier(uint16_t endpoint_id, uint8_t tier)
{
    update_if_changed(endpoint_id, FanControl::Attributes::FanMode::Id, tier);
    update_if_changed(endpoint_id, FanControl::Attributes::PercentSetting::Id, tier_percent[tier]);
    // PercentCurrent 是只读的实际转速,SDK 只允许通过专用接口设置;风扇没有转速反馈,等于目标值
    FanControlCluster *cluster = FanControl::FindClusterOnEndpoint(endpoint_id);
    if (cluster != nullptr) {
        cluster->SetPercentCurrent(tier_percent[tier]);
    }
}

static bool get_u8(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, uint8_t &out)
{
    attribute_t *attr = attribute::get(endpoint_id, cluster_id, attribute_id);
    esp_matter_attr_val_t val;
    if (attr == nullptr || attribute::get_val(attr, &val) != ESP_OK) {
        return false;
    }
    out = val.val.u8;
    return true;
}

static uint8_t current_mode(uint16_t endpoint_id)
{
    uint8_t mode = 0;
    get_u8(endpoint_id, ModeSelect::Id, ModeSelect::Attributes::CurrentMode::Id, mode);
    return mode;
}

static bool is_slave()
{
    return current_mode(eps.role) == reair::role::slave;
}

// 从节点的风向模式和往复周期不接受控制器修改
static bool mode_select_locked(uint16_t endpoint_id)
{
    return (endpoint_id == eps.period || endpoint_id == eps.direction_mode) && is_slave();
}

static uint8_t direction_mode()
{
    return current_mode(eps.direction_mode);
}

// ---- 运行时状态与同步(ADR 0002、0003) ----

static bool logical_reverse = false;   // 当前逻辑风向:false 正向,true 反向。不持久化
static uint32_t cycle_override_s = 0;  // 测试用:覆盖往复周期,0 表示不覆盖
static const uint32_t sync_interval_s = 5;

static uint8_t role()
{
    return current_mode(eps.role);
}

static uint8_t fan_tier()
{
    uint8_t tier = 0;
    get_u8(eps.fan, FanControl::Id, FanControl::Attributes::FanMode::Id, tier);
    return tier;
}

// 实际风向由风力、逻辑风向和正向风向决定
static void update_actual_direction()
{
    if (fan_tier() == 0) {
        reair::set_actual_direction(reair::actual_direction_state::stopped);
        reair::fan::set(reair::fan::Direction::exhaust, 0);
        return;
    }
    bool exhaust_is_forward = current_mode(eps.forward_direction) == reair::forward_direction::exhaust;
    bool exhaust = logical_reverse != exhaust_is_forward;
    reair::set_actual_direction(exhaust ? reair::actual_direction_state::exhaust
                                        : reair::actual_direction_state::intake);
    reair::fan::set(exhaust ? reair::fan::Direction::exhaust : reair::fan::Direction::intake, fan_tier());
}

static uint32_t cycle_seconds()
{
    if (cycle_override_s != 0) {
        return cycle_override_s;
    }
    switch (current_mode(eps.period)) {
    case reair::period_mode::s60:
        return 60;
    case reair::period_mode::s90:
        return 90;
    default:
        return 120;
    }
}

// 只有主节点发送
static void send_sync()
{
    if (role() == reair::role::master) {
        reair::sync::send(logical_reverse, fan_tier());
    }
}

static void on_sender_timer(chip::System::Layer *, void *)
{
    send_sync();
    chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(sync_interval_s), on_sender_timer, nullptr);
}

static void on_cycle_timer(chip::System::Layer *, void *)
{
    logical_reverse = !logical_reverse;
    update_actual_direction();
    send_sync();
    chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(cycle_seconds()), on_cycle_timer, nullptr);
}

// 角色、风向模式或周期变化后,整理往复计时器和同步发送。主、独立节点按风向模式决定逻辑风向,从节点只跟随同步。
static void refresh_roles(bool role_changed)
{
    chip::DeviceLayer::SystemLayer().CancelTimer(on_sender_timer, nullptr);
    chip::DeviceLayer::SystemLayer().CancelTimer(on_cycle_timer, nullptr);
    bool old_reverse = logical_reverse;
    if (role() != reair::role::slave) {
        switch (direction_mode()) {
        case reair::direction_mode::fixed_forward:
            logical_reverse = false;
            break;
        case reair::direction_mode::fixed_reverse:
            logical_reverse = true;
            break;
        default:
            chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(cycle_seconds()), on_cycle_timer,
                                                        nullptr);
            break;
        }
    }
    update_actual_direction();
    if (role() == reair::role::master) {
        chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(sync_interval_s), on_sender_timer,
                                                    nullptr);
        if (role_changed || logical_reverse != old_reverse) {
            send_sync();
        }
    }
}

// 收到同步报文(在 Matter 栈的上下文里)
static void on_sync_packet(const reair::sync::Packet &packet, bool own)
{
    if (own) {
        return; // 组播回环
    }
    switch (role()) {
    case reair::role::master:
        reair::set_warning(reair::warning_state::warning); // 多主警告
        break;
    case reair::role::slave:
        logical_reverse = packet.reverse;
        apply_fan_tier(eps.fan, packet.tier);
        update_actual_direction();
        break;
    default:
        break; // 独立节点不接收
    }
}

static void on_mode_select_changed(uint16_t endpoint_id, uint8_t mode)
{
    if (endpoint_id == eps.forward_direction) {
        update_actual_direction();
    } else if (endpoint_id == eps.role) {
        refresh_roles(true);
    } else if (endpoint_id == eps.period || endpoint_id == eps.direction_mode) {
        refresh_roles(false);
    }
}

// 记录控制器的写入,让各端点的状态保持一致;风扇输出由 update_actual_direction 驱动。
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    const bool is_fan_write = endpoint_id == eps.fan && cluster_id == FanControl::Id &&
        (attribute_id == FanControl::Attributes::FanMode::Id || attribute_id == FanControl::Attributes::PercentSetting::Id);
    const bool is_mode_write = cluster_id == ModeSelect::Id && attribute_id == ModeSelect::Attributes::CurrentMode::Id;

    if (type == attribute::PRE_UPDATE) {
        ESP_LOGI(TAG, "write: endpoint %u cluster 0x%04" PRIx32 " attribute 0x%04" PRIx32 " type %d value %" PRIu32,
                 endpoint_id, cluster_id, attribute_id, (int)val->type, (uint32_t)val->val.u32);
        // 序列之外的档位(如自动)不接受
        if (cluster_id == FanControl::Id && attribute_id == FanControl::Attributes::FanMode::Id &&
            val->val.u8 > max_tier) {
            return ESP_ERR_INVALID_ARG;
        }
        // 从节点的风力、风向模式、往复周期由主节点或自身设置决定,不接受控制器写入
        if (is_slave() && (is_fan_write || (is_mode_write && endpoint_id == eps.period))) {
            ESP_LOGW(TAG, "rejected: node is slave");
            return ESP_ERR_INVALID_STATE;
        }
    } else if (type == attribute::POST_UPDATE) {
        if (is_fan_write) {
            if (attribute_id == FanControl::Attributes::FanMode::Id) {
                apply_fan_tier(endpoint_id, val->val.u8);
            } else if (attribute_id == FanControl::Attributes::PercentSetting::Id) {
                apply_fan_tier(endpoint_id, percent_to_tier(val->val.u8));
            }
            update_actual_direction();
            send_sync(); // 风力变化立即发送
        } else if (is_mode_write) {
            on_mode_select_changed(endpoint_id, val->val.u8);
        }
    }
    return ESP_OK;
}

#if CONFIG_ENABLE_CHIP_SHELL
// 测试用串口命令:matter esp reair <state|loss|cycle|send|raw>
static esp_err_t reair_command(int argc, char **argv)
{
    if (argc >= 1 && strcmp(argv[0], "state") == 0) {
        lock::ScopedChipStackLock lock(portMAX_DELAY);
        reair::sync::Stats st = reair::sync::stats();
        const uint8_t *mac = reair::sync::own_mac();
        printf("STATE mac=%02x%02x%02x%02x%02x%02x role=%u tier=%u rev=%d actual=%u warn=%u mode=%u cycle=%" PRIu32
               " fwd=%u tx=%" PRIu32 " txd=%" PRIu32 " txf=%" PRIu32 " rx=%" PRIu32 " own=%" PRIu32
               " inv=%" PRIu32 " rxd=%" PRIu32 " up=%" PRId64 " heap=%" PRIu32 "\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], role(), fan_tier(), (int)logical_reverse,
               current_mode(eps.actual_direction), current_mode(eps.warning), direction_mode(), cycle_seconds(),
               current_mode(eps.forward_direction), st.tx, st.tx_dropped, st.tx_failed, st.rx, st.rx_own,
               st.rx_invalid, st.rx_dropped, esp_timer_get_time() / 1000, esp_get_free_heap_size());
        return ESP_OK;
    }
    if (argc >= 3 && strcmp(argv[0], "loss") == 0) {
        reair::sync::set_loss((uint8_t)atoi(argv[1]), (uint8_t)atoi(argv[2]));
        printf("OK loss tx=%d%% rx=%d%%\n", atoi(argv[1]), atoi(argv[2]));
        return ESP_OK;
    }
    if (argc >= 2 && strcmp(argv[0], "cycle") == 0) {
        lock::ScopedChipStackLock lock(portMAX_DELAY);
        cycle_override_s = (uint32_t)atoi(argv[1]);
        refresh_roles(false);
        printf("OK cycle=%" PRIu32 "\n", cycle_seconds());
        return ESP_OK;
    }
    if (argc >= 1 && strcmp(argv[0], "raw") == 0) {
        // raw [hex]:发出这些字节,不带参数则发空报文
        uint8_t buffer[64];
        size_t length = 0;
        if (argc >= 2) {
            for (const char *p = argv[1]; p[0] != 0 && p[1] != 0 && length < sizeof(buffer); p += 2) {
                char byte[3] = {p[0], p[1], 0};
                buffer[length++] = (uint8_t)strtoul(byte, nullptr, 16);
            }
        }
        reair::sync::send_raw(buffer, length);
        printf("OK raw %u bytes\n", (unsigned)length);
        return ESP_OK;
    }
    if (argc >= 1 && strcmp(argv[0], "send") == 0) {
        lock::ScopedChipStackLock lock(portMAX_DELAY);
        send_sync();
        printf("OK send\n");
        return ESP_OK;
    }
    printf("usage: reair state | loss <tx%%> <rx%%> | cycle <seconds|0> | send | raw [hex]\n");
    return ESP_OK;
}
#endif

extern "C" void app_main()
{
    // 12V 接入后到这里之前风扇控制线被上拉,风扇全速,所以尽早输出停机点
    esp_err_t fan_err = reair::fan::init();
    nvs_flash_init();

    // OTA 回退(ADR 0007):读取待验证状态并开始自检计时;硬件自检结果在这里汇总
    reair::ota::init();
    reair::ota::check(reair::ota::kFan, fan_err);
    ESP_ERROR_CHECK_WITHOUT_ABORT(reair::ota::install_guard());

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    // 风扇：关闭与高、中、低三档
    fan::config_t fan_config;
    fan_config.fan_control.fan_mode = (uint8_t)FanControl::FanModeEnum::kOff;
    fan_config.fan_control.fan_mode_sequence = (uint8_t)FanControl::FanModeSequenceEnum::kOffLowMedHigh;
    fan_config.fan_control.percent_setting = (uint8_t)0;
    fan_config.fan_control.percent_current = 0;
    endpoint_t *fan_endpoint = fan::create(node, &fan_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(fan_endpoint != nullptr, ESP_LOGE(TAG, "Failed to create fan endpoint"));
    eps.fan = endpoint::get_id(fan_endpoint);

    ESP_LOGI(TAG, "Fan created with endpoint_id %d", eps.fan);

    ABORT_APP_ON_FAILURE(reair::create_endpoints(node, eps) == ESP_OK,
                         ESP_LOGE(TAG, "Failed to create node endpoints"));

    esp_err_t err = esp_matter::start(app_event_cb);
    reair::ota::check(reair::ota::kMatter, err);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    reair::set_write_lock(mode_select_locked);
    reair::ota::check(reair::ota::kButton, reair::button::start());
    reair::ota::check(reair::ota::kLed, reair::led::start());

    // 启动状态(ADR 0002):从节点静止等待同步;主、独立节点恢复保存的设置和风力,往复从正向开始
    {
        lock::ScopedChipStackLock lock(portMAX_DELAY);
        reair::set_warning(reair::warning_state::none);
        logical_reverse = false;
        apply_fan_tier(eps.fan, role() == reair::role::slave ? 0 : fan_tier());
        refresh_roles(false);
        ABORT_APP_ON_FAILURE(reair::sync::start(on_sync_packet) == ESP_OK, ESP_LOGE(TAG, "Failed to start sync"));
    }

    // 硬件都已初始化:检查蓝牙并确认新固件有效(没有待验证的固件时什么也不做)
    reair::ota::start_selftest();

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
    static const esp_matter::console::command_t reair_cmd = {
        .name = "reair", .description = "ReAir test commands", .handler = reair_command};
    esp_matter::console::add_commands(&reair_cmd, 1);
    esp_matter::console::init();
#endif
}
