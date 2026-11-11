// OTA 升级判定与自检汇总的主机单元测试(ADR 0007)
#include <cstdio>
#include <cstdlib>

#include "../main/ota_policy.h"

using namespace reair::ota;

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static void test_version_acceptable()
{
    // 没有回退记录:只要比当前新
    CHECK(version_acceptable(26111117, 26111116, 0));
    CHECK(!version_acceptable(26111116, 26111116, 0));
    CHECK(!version_acceptable(26111115, 26111116, 0));
    // 有回退记录:必须同时比当前和被回退的新
    CHECK(version_acceptable(26111120, 26111116, 26111118));
    CHECK(!version_acceptable(26111118, 26111116, 26111118));  // 相同版本再次提议
    CHECK(!version_acceptable(26111117, 26111116, 26111118));  // 比被回退的旧
    // 被回退的版本比当前还旧(更早的记录):以较大者为准
    CHECK(!version_acceptable(26111105, 26111116, 26111100));
    CHECK(version_acceptable(26111117, 26111116, 26111100));
}

static void test_parse_version()
{
    uint32_t v = 0;
    CHECK(parse_version("26111116", v) && v == 26111116u);
    CHECK(parse_version("0", v) && v == 0u);
    CHECK(parse_version("4294967295", v) && v == 4294967295u);
    CHECK(!parse_version("4294967296", v));
    CHECK(!parse_version("", v));
    CHECK(!parse_version(nullptr, v));
    CHECK(!parse_version("0.1", v));
    CHECK(!parse_version("26111116a", v));
    CHECK(!parse_version("-1", v));
    CHECK(!parse_version("99999999999999999999", v));
}

static void test_selftest()
{
    SelfTest t;
    CHECK(!t.complete() && !t.failed() && t.missing() == SelfTest::kAll);
    t.pass(kFan);
    t.pass(kLed);
    t.pass(kButton);
    t.pass(kMatter);
    CHECK(!t.complete());          // 还差蓝牙
    CHECK(t.missing() == kBle);
    t.pass(kBle);
    CHECK(t.complete() && t.missing() == 0);

    SelfTest bad;
    bad.pass(kFan);
    bad.fail(kLed);
    bad.pass(kButton);
    bad.pass(kMatter);
    bad.pass(kBle);
    CHECK(bad.failed() && !bad.complete());  // 一项失败就不能确认,即使其他项都通过
}

int main()
{
    test_version_acceptable();
    test_parse_version();
    test_selftest();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ota_policy_test passed\n");
    return 0;
}
