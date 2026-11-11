#pragma once

#include <esp_err.h>

#include "ota_policy.h"

// OTA 自动回退与升级约束(ADR 0007)。
//
// 新固件 OTA 后首次启动处于“待验证”状态:硬件自检全部通过才确认有效,
// 任何一项失败、自检超时、或确认之前重启,引导程序退回旧固件。
// 回退后旧固件记录被回退固件的版本号,之后不接受版本号不大于它的升级提议。

namespace reair {
namespace ota {

// 在 nvs_flash_init 之后、其他初始化之前调用:读取待验证状态,启动自检超时计时,记录刚被回退的固件
void init();

// 报告一项硬件自检的结果。待验证状态下失败会立即回退;不在待验证状态时只记日志
void check(Item item, esp_err_t err);

// 在 esp_matter::start 和各硬件初始化完成之后调用:检查蓝牙并确认固件有效
void start_selftest();

// 用拒绝回退版本的驱动替换 Matter OTA 请求端的默认驱动。必须在 esp_matter::start 之前调用
esp_err_t install_guard();

// 恢复出厂设置时清除回退记录
void clear_failed_record();

}  // namespace ota
}  // namespace reair
