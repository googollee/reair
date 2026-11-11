#include "fan.h"

#include <driver/ledc.h>
#include <esp_log.h>

namespace reair {
namespace fan {
namespace {

const char *TAG = "fan";

constexpr int kGpio = CONFIG_REAIR_FAN_PWM_GPIO;
constexpr uint32_t kFrequencyHz = 3000;
constexpr ledc_timer_bit_t kResolution = LEDC_TIMER_12_BIT;
constexpr uint32_t kMaxDuty = (1u << kResolution) - 1;

void apply(uint8_t line_duty)
{
    // MOSFET 使风扇控制线与 GPIO 反相
    uint32_t gpio_duty = (100 - line_duty) * kMaxDuty / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, gpio_duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

}  // namespace

esp_err_t init()
{
    if (kGpio < 0) {
        return ESP_OK;
    }
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = kResolution;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = kFrequencyHz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t channel = {};
    channel.gpio_num = kGpio;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = (100 - kStopDuty) * kMaxDuty / 100;
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
    }
    return err;
}

void set(Direction direction, uint8_t tier)
{
    if (kGpio < 0) {
        return;
    }
    uint8_t duty = line_duty(direction, tier);
    static uint8_t last_duty = 255;  // 同步报文每 5 秒会再调用一次,没变化就不重复设置
    if (duty == last_duty) {
        return;
    }
    last_duty = duty;
    ESP_LOGI(TAG, "tier %u %s: duty %u%%", tier, direction == Direction::exhaust ? "exhaust" : "intake", duty);
    apply(duty);
}

}  // namespace fan
}  // namespace reair
