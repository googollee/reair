#pragma once

#include <esp_matter.h>

// ADR 0001 中除风扇外的端点:六个 ModeSelect,其中实际风向只读,多主警告只能由控制器清除。

namespace reair {

// ModeSelect 的取值(CurrentMode)。
namespace period_mode { enum : uint8_t { s60 = 0, s90 = 1, s120 = 2 }; }
namespace direction_mode { enum : uint8_t { fixed_forward = 0, fixed_reverse = 1, reciprocating = 2 }; }
namespace role { enum : uint8_t { master = 0, slave = 1, independent = 2 }; }
namespace forward_direction { enum : uint8_t { intake = 0, exhaust = 1 }; }

// 只读状态端点的取值(CurrentMode)
namespace actual_direction_state { enum : uint8_t { intake = 0, exhaust = 1, stopped = 2 }; }
// 警告:控制器只能写「无」来清除,不能写「警告」
namespace warning_state { enum : uint8_t { none = 0, warning = 1 }; }

struct Endpoints {
    uint16_t fan = 0;
    uint16_t period = 0;
    uint16_t role = 0;
    uint16_t forward_direction = 0;
    uint16_t actual_direction = 0;
    uint16_t warning = 0;
    uint16_t direction_mode = 0;
};

// 创建除风扇外的六个端点,把端点号写入 eps。必须在 esp_matter::start 之前调用。
esp_err_t create_endpoints(esp_matter::node_t *node, Endpoints &eps);

// 设置 ModeSelect 的写入限制:返回 true 的端点拒绝控制器改变模式(用于从节点)。
void set_write_lock(bool (*is_locked)(uint16_t endpoint_id));

// 更新两个只读状态端点。在 Matter 栈的上下文(属性回调)里调用。
void set_actual_direction(uint8_t state);
void set_warning(uint8_t state);

} // namespace reair
