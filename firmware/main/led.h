#pragma once

// 状态 LED(ADR 0006):只显示联网状态。LED 低电平点亮,引脚由 CONFIG_REAIR_LED_GPIO 指定,
// 小于 0 表示该目标板没有这个 LED。必须在 esp_matter::start 之后调用。
// 成功或没有这个 LED 时返回 ESP_OK。

#include <esp_err.h>

namespace reair {
namespace led {

esp_err_t start();

}  // namespace led
}  // namespace reair
