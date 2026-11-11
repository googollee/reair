#pragma once

// 板上按键(ADR 0006):短按重新开放配网窗口,长按 5 秒后松开恢复出厂设置。
// 引脚由 CONFIG_REAIR_BUTTON_GPIO 指定,按目标芯片在 sdkconfig.defaults.<target> 中设置。

#include <esp_err.h>

namespace reair {
namespace button {

// 成功返回 ESP_OK;GPIO、队列、定时器或任务创建失败时返回错误,按键不可用
esp_err_t start();

}  // namespace button
}  // namespace reair
