#pragma once

#include <cstdint>

#include <esp_err.h>

#include "fan_duty.h"

// 风扇 PWM 输出(ADR 0005):GPIO 经 N 沟道 MOSFET 拉低风扇控制线,输出在这里取反。
// 引脚由 CONFIG_REAIR_FAN_PWM_GPIO 指定,小于 0 表示该目标板不接风扇。

namespace reair {
namespace fan {

// 启动后尽早调用,输出停机点(50%)。没有接风扇时返回 ESP_OK
esp_err_t init();

// tier 为 0 时停机,否则按实际风向和档位输出
void set(Direction direction, uint8_t tier);

}  // namespace fan
}  // namespace reair
