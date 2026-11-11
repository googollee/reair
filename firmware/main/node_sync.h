#pragma once

#include <esp_err.h>

#include "sync_packet.h"

// 同步传输:IPv6 链路本地全节点组播 ff02::1,端口 36010(ADR 0003)。

namespace reair {
namespace sync {

// 收到合法报文时在 Matter 栈的上下文里调用,own 为发送者是否是本节点
using ReceiveCallback = void (*)(const Packet &packet, bool own);

struct Stats {
    uint32_t tx = 0;          // 实际发出
    uint32_t tx_dropped = 0;  // 测试用:被模拟丢弃的发送
    uint32_t tx_failed = 0;   // sendto 失败
    uint32_t rx = 0;          // 合法报文(含本节点的回环)
    uint32_t rx_own = 0;      // 其中发送者是本节点
    uint32_t rx_invalid = 0;  // 格式不合法被丢弃
    uint32_t rx_dropped = 0;  // 测试用:被模拟丢弃的接收
};

esp_err_t start(ReceiveCallback callback);

// 用本节点的 MAC 作为发送者发出一条报文
void send(bool reverse, uint8_t tier);

// 测试用:原样发出任意字节(可构造非法报文、他人的 MAC)
void send_raw(const uint8_t *data, size_t length);

const uint8_t *own_mac();
Stats stats();

// 测试用:按百分比随机丢弃发送或接收的报文
void set_loss(uint8_t tx_percent, uint8_t rx_percent);

} // namespace sync
} // namespace reair
