// 同步报文编解码的主机单元测试:make test
#include "../main/sync_packet.h"

#include <cstdio>
#include <initializer_list>

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                                             \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace reair::sync;

int main()
{
    // 取值全覆盖:方向 × 风力,编码再解码一致
    for (int reverse = 0; reverse < 2; reverse++) {
        for (int tier = 0; tier <= kMaxTier; tier++) {
            Packet in;
            const uint8_t mac[6] = {0xb4, 0x3a, 0x45, 0x18, 0x25, 0x70};
            memcpy(in.mac, mac, 6);
            in.reverse = reverse;
            in.tier = tier;
            uint8_t raw[kPacketLength];
            encode(in, raw);
            CHECK(raw[0] == 1);
            CHECK(memcmp(&raw[1], mac, 6) == 0);
            CHECK((raw[7] & 0x0f) == 0); // 保留位发送时为 0
            Packet out;
            CHECK(decode(raw, sizeof(raw), out));
            CHECK(out.reverse == (bool)reverse && out.tier == tier && memcmp(out.mac, mac, 6) == 0);
        }
    }
    // 固定样例:反向 + 高档 = 0xB0
    {
        Packet in;
        in.reverse = true;
        in.tier = 3;
        uint8_t raw[kPacketLength];
        encode(in, raw);
        CHECK(raw[7] == 0xB0);
    }
    // 长度不是 8 的整个丢弃
    {
        uint8_t raw[16] = {1, 0, 0, 0, 0, 0, 0, 0x20};
        Packet out;
        for (size_t len : {0, 1, 7, 9, 16}) {
            CHECK(!decode(raw, len, out));
        }
        CHECK(!decode(nullptr, 8, out));
    }
    // 版本不认识的丢弃
    for (int version : {0, 2, 3, 255}) {
        uint8_t raw[8] = {(uint8_t)version, 1, 2, 3, 4, 5, 6, 0x20};
        Packet out;
        CHECK(!decode(raw, 8, out));
    }
    // 风力保留值 4~7 丢弃
    for (int tier = 4; tier <= 7; tier++) {
        uint8_t raw[8] = {1, 1, 2, 3, 4, 5, 6, (uint8_t)(tier << 4)};
        Packet out;
        CHECK(!decode(raw, 8, out));
    }
    // 保留位(低 4 位)被忽略
    for (int low = 0; low < 16; low++) {
        uint8_t raw[8] = {1, 1, 2, 3, 4, 5, 6, (uint8_t)(0x80 | (2 << 4) | low)};
        Packet out;
        CHECK(decode(raw, 8, out));
        CHECK(out.reverse && out.tier == 2);
    }
    printf(failures == 0 ? "OK\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
