#pragma once

#include <atomic>
#include <cstdint>

// OTA 回退与升级判定中不依赖硬件的部分(ADR 0007),可以在主机上单元测试。

namespace reair {
namespace ota {

// 只接受比当前固件和最近一次回退的固件都更新的版本。failed 为 0 表示没有回退记录。
inline bool version_acceptable(uint32_t offered, uint32_t current, uint32_t failed)
{
    return offered > current && (failed == 0 || offered > failed);
}

// 固件的 PROJECT_VER 就是十进制的软件版本号(YYMMDDHH)。空串、含非数字或超出 32 位时返回 false。
inline bool parse_version(const char *text, uint32_t &out)
{
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    uint64_t value = 0;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = value * 10 + static_cast<uint64_t>(*p - '0');
        if (value > UINT32_MAX) {
            return false;
        }
    }
    out = static_cast<uint32_t>(value);
    return true;
}

// 自检项。全部通过固件才算有效,任何一项失败立即回退。
enum Item : uint8_t {
    kFan = 1 << 0,     // 风扇 PWM 输出初始化并已输出停机点
    kLed = 1 << 1,     // 状态 LED 的 GPIO 和任务
    kButton = 1 << 2,  // 按键 GPIO、中断和任务
    kMatter = 1 << 3,  // Matter 协议栈已启动
    kBle = 1 << 4,     // 蓝牙:未配网且配网窗口开放时必须在广播或有连接,其他情况不要求
};

class SelfTest {
public:
    static constexpr uint8_t kAll = kFan | kLed | kButton | kMatter | kBle;

    void pass(Item item) { passed_ |= item; }
    void fail(Item item) { failed_ |= item; }
    bool failed() const { return failed_.load() != 0; }
    bool complete() const { return failed_.load() == 0 && (passed_.load() & kAll) == kAll; }
    uint8_t missing() const { return static_cast<uint8_t>(kAll & ~passed_.load()); }

private:
    std::atomic<uint8_t> passed_{0};
    std::atomic<uint8_t> failed_{0};
};

}  // namespace ota
}  // namespace reair
