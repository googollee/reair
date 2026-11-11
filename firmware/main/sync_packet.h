#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// ADR 0003 的同步报文:定长 8 字节,版本 + 发送者 MAC + 状态位域。纯函数,不依赖 IDF,可在主机上测试。

namespace reair {
namespace sync {

constexpr uint8_t kVersion = 1;
constexpr size_t kPacketLength = 8;
constexpr uint16_t kPort = 36010;
constexpr uint8_t kMaxTier = 3;

struct Packet {
    uint8_t mac[6] = {};
    bool reverse = false; // 当前风向:false 正向,true 反向
    uint8_t tier = 0;     // 风力:0 关闭,1 低,2 中,3 高
};

inline void encode(const Packet &packet, uint8_t out[kPacketLength])
{
    out[0] = kVersion;
    memcpy(&out[1], packet.mac, 6);
    out[7] = (uint8_t)((packet.reverse ? 0x80 : 0x00) | ((packet.tier & 0x07) << 4));
}

// 长度不对、版本不认识、风力是保留值时整个丢弃;保留位忽略
inline bool decode(const uint8_t *data, size_t length, Packet &packet)
{
    if (data == nullptr || length != kPacketLength || data[0] != kVersion) {
        return false;
    }
    uint8_t tier = (data[7] >> 4) & 0x07;
    if (tier > kMaxTier) {
        return false;
    }
    memcpy(packet.mac, &data[1], 6);
    packet.reverse = (data[7] & 0x80) != 0;
    packet.tier = tier;
    return true;
}

} // namespace sync
} // namespace reair
