// 风力和风向到占空比映射的主机单元测试:make test
#include "../main/fan_duty.h"

#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                                             \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace reair::fan;

int main()
{
    // 排气在低占空比一侧,进气在高占空比一侧;档位越高离停机点越远
    CHECK(line_duty(Direction::exhaust, 1) == 38);
    CHECK(line_duty(Direction::exhaust, 2) == 25);
    CHECK(line_duty(Direction::exhaust, 3) == 5);
    CHECK(line_duty(Direction::intake, 1) == 62);
    CHECK(line_duty(Direction::intake, 2) == 75);
    CHECK(line_duty(Direction::intake, 3) == 95);

    // 关闭和越界档位输出停机点,两个方向相同
    CHECK(line_duty(Direction::exhaust, 0) == kStopDuty);
    CHECK(line_duty(Direction::intake, 0) == kStopDuty);
    CHECK(line_duty(Direction::exhaust, 4) == kStopDuty);

    // 低档必须在停转区之外(实测停转区约 45% 到 57%)
    CHECK(line_duty(Direction::exhaust, 1) < 45);
    CHECK(line_duty(Direction::intake, 1) > 57);

    // 两个方向以停机点对称
    for (uint8_t tier = 0; tier <= kMaxTier; tier++) {
        CHECK(line_duty(Direction::exhaust, tier) + line_duty(Direction::intake, tier) == 100);
    }

    if (failures == 0) {
        printf("fan_duty_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
