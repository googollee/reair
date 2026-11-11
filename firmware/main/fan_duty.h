#pragma once

#include <cstdint>

// 风力档位和实际风向到风扇 PWM 线占空比的映射(ADR 0005),不依赖 ESP-IDF,可在主机上单元测试:make test。
// 返回的是风扇 PWM 线上的占空比(%),不含 MOSFET 的反相。

namespace reair {
namespace fan {

enum class Direction : uint8_t { exhaust, intake };

constexpr uint8_t kStopDuty = 50;
constexpr uint8_t kMaxTier = 3;

// 风力档位 0 关闭、1 低、2 中、3 高。低占空比一侧是物理排气,高占空比一侧是物理进气
inline uint8_t line_duty(Direction direction, uint8_t tier)
{
    static const uint8_t exhaust[] = {kStopDuty, 38, 25, 5};
    static const uint8_t intake[] = {kStopDuty, 62, 75, 95};
    if (tier == 0 || tier > kMaxTier) {
        return kStopDuty;
    }
    return direction == Direction::exhaust ? exhaust[tier] : intake[tier];
}

}  // namespace fan
}  // namespace reair
