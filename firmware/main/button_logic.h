#pragma once

#include <utility>

// 按键的短按、长按判定(ADR 0006),不依赖 ESP-IDF 和 Matter,可在主机上单元测试:make test。
// button.cpp 把去抖后的电平和长按计时到期喂进来,并执行返回的动作。

namespace reair {
namespace button {

enum class Action {
    none,
    start_long_press_timer,  // 按下:启动长按计时
    short_press,             // 长按到期前松开:重新开放配网窗口
    factory_reset,           // 长按到期后松开:恢复出厂设置并重启
};

class Logic {
public:
    // 去抖后的电平,只有电平变化才产生动作
    Action on_settled_level(bool pressed)
    {
        if (pressed == pressed_) {
            return Action::none;
        }
        pressed_ = pressed;
        if (pressed) {
            return Action::start_long_press_timer;
        }
        // 长按到期后要等松开再复位:按键兼作启动配置引脚,按住时复位会进入下载模式
        return std::exchange(long_pressed_, false) ? Action::factory_reset : Action::short_press;
    }

    // 长按计时到期,level_low 是此刻重新读到的引脚电平。仍按住才算长按,松开时不再当作短按
    void on_long_press_expired(bool level_low)
    {
        if (pressed_ && level_low) {
            long_pressed_ = true;
        }
    }

    bool long_pressed() const { return long_pressed_; }

private:
    bool pressed_ = false;  // 空闲为高电平(内部上拉),按下拉低
    bool long_pressed_ = false;
};

}  // namespace button
}  // namespace reair
