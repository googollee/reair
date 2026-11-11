// 按键判定的主机单元测试:make test
#include "../main/button_logic.h"

#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                                             \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace reair::button;

int main()
{
    // 按下再在长按到期前松开:短按
    {
        Logic l;
        CHECK(l.on_settled_level(true) == Action::start_long_press_timer);
        CHECK(l.on_settled_level(false) == Action::short_press);
    }

    // 电平没变不产生动作
    {
        Logic l;
        CHECK(l.on_settled_level(false) == Action::none);
        CHECK(l.on_settled_level(true) == Action::start_long_press_timer);
        CHECK(l.on_settled_level(true) == Action::none);
    }

    // 长按到期时仍按住:到期本身不复位,松开后才复位
    {
        Logic l;
        l.on_settled_level(true);
        l.on_long_press_expired(true);
        CHECK(l.long_pressed());
        CHECK(l.on_settled_level(false) == Action::factory_reset);
        CHECK(!l.long_pressed());
    }

    // 到期时引脚已经松开(去抖期间的竞争):不算长按
    {
        Logic l;
        l.on_settled_level(true);
        l.on_settled_level(false);
        l.on_long_press_expired(false);
        CHECK(!l.long_pressed());
    }

    // 没按下时到期:忽略
    {
        Logic l;
        l.on_long_press_expired(true);
        CHECK(!l.long_pressed());
        CHECK(l.on_settled_level(true) == Action::start_long_press_timer);
        CHECK(l.on_settled_level(false) == Action::short_press);
    }

    // 复位后恢复正常:下一次按下按短按处理
    {
        Logic l;
        l.on_settled_level(true);
        l.on_long_press_expired(true);
        l.on_settled_level(false);
        CHECK(l.on_settled_level(true) == Action::start_long_press_timer);
        CHECK(l.on_settled_level(false) == Action::short_press);
    }

    if (failures == 0) {
        printf("button_logic_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
