#include "node_sync.h"

#include <cstdlib>
#include <cstring>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_random.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>
#include <platform/CHIPDeviceLayer.h>

static const char *TAG = "reair.sync";

namespace reair {
namespace sync {
namespace {

int sock = -1;
uint8_t mac[6];
uint32_t netif_index = 0;
ReceiveCallback receive_callback = nullptr;
Stats counters;
uint8_t tx_loss = 0;
uint8_t rx_loss = 0;

bool lose(uint8_t percent)
{
    return percent != 0 && (esp_random() % 100) < percent;
}

struct Pending {
    Packet packet;
    bool own;
};

// 在 Matter 栈的上下文里处理报文
void deliver(intptr_t arg)
{
    Pending *pending = reinterpret_cast<Pending *>(arg);
    if (receive_callback != nullptr) {
        receive_callback(pending->packet, pending->own);
    }
    delete pending;
}

void receive_task(void *)
{
    uint8_t buffer[64];
    for (;;) {
        // 比报文长一点,这样长度不对的报文也能收到并丢弃
        int length = recvfrom(sock, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (length < 0) {
            continue;
        }
        Packet packet;
        if (!decode(buffer, (size_t)length, packet)) {
            counters.rx_invalid++;
            continue;
        }
        if (lose(rx_loss)) {
            counters.rx_dropped++;
            continue;
        }
        bool own = memcmp(packet.mac, mac, 6) == 0;
        counters.rx++;
        ESP_LOGI(TAG, "SYNC-RX mac=%02x%02x%02x%02x%02x%02x rev=%d tier=%u own=%d", packet.mac[0], packet.mac[1],
                 packet.mac[2], packet.mac[3], packet.mac[4], packet.mac[5], (int)packet.reverse, packet.tier, (int)own);
        if (own) {
            counters.rx_own++;
        }
        Pending *pending = new Pending{packet, own};
        if (chip::DeviceLayer::PlatformMgr().ScheduleWork(deliver, reinterpret_cast<intptr_t>(pending)) != CHIP_NO_ERROR) {
            delete pending;
        }
    }
}

} // namespace

esp_err_t start(ReceiveCallback callback)
{
    receive_callback = callback;
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        ESP_LOGE(TAG, "no Wi-Fi station netif");
        return ESP_FAIL;
    }
    netif_index = esp_netif_get_netif_impl_index(netif);
    struct netif *lwip_netif = reinterpret_cast<struct netif *>(esp_netif_get_netif_impl(netif));
    if (lwip_netif != nullptr) {
        ESP_LOGI(TAG, "netif impl index %u, lwip index %u, name %c%c%u", (unsigned)netif_index,
                 (unsigned)netif_get_index(lwip_netif), lwip_netif->name[0], lwip_netif->name[1], lwip_netif->num);
        netif_index = netif_get_index(lwip_netif);
    }

    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_IPV6);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed: errno %d", errno);
        return ESP_FAIL;
    }
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in6 address = {};
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(kPort);
    address.sin6_addr = IN6ADDR_ANY_INIT;
    if (bind(sock, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        ESP_LOGE(TAG, "bind failed: errno %d", errno);
        return ESP_FAIL;
    }
    xTaskCreate(receive_task, "reair_sync_rx", 4096, nullptr, 5, nullptr);
    ESP_LOGI(TAG, "sync started, port %u, netif %u, mac %02x:%02x:%02x:%02x:%02x:%02x", kPort, (unsigned)netif_index,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}

void send(bool reverse, uint8_t tier)
{
    if (sock < 0) {
        return;
    }
    if (lose(tx_loss)) {
        counters.tx_dropped++;
        return;
    }
    Packet packet;
    memcpy(packet.mac, mac, 6);
    packet.reverse = reverse;
    packet.tier = tier;
    uint8_t raw[kPacketLength];
    encode(packet, raw);

    sockaddr_in6 destination = {};
    destination.sin6_family = AF_INET6;
    destination.sin6_port = htons(kPort);
    inet_pton(AF_INET6, "ff02::1", &destination.sin6_addr);
    destination.sin6_scope_id = netif_index;
    int sent = sendto(sock, raw, sizeof(raw), 0, reinterpret_cast<sockaddr *>(&destination), sizeof(destination));
    if (sent == (int)sizeof(raw)) {
        counters.tx++;
        ESP_LOGI(TAG, "SYNC-TX rev=%d tier=%u", (int)reverse, tier);
    } else {
        counters.tx_failed++;
        ESP_LOGW(TAG, "sendto failed: errno %d", errno);
    }
}

void send_raw(const uint8_t *data, size_t length)
{
    if (sock < 0) {
        return;
    }
    sockaddr_in6 destination = {};
    destination.sin6_family = AF_INET6;
    destination.sin6_port = htons(kPort);
    inet_pton(AF_INET6, "ff02::1", &destination.sin6_addr);
    destination.sin6_scope_id = netif_index;
    sendto(sock, data, length, 0, reinterpret_cast<sockaddr *>(&destination), sizeof(destination));
}

const uint8_t *own_mac()
{
    return mac;
}

Stats stats()
{
    return counters;
}

void set_loss(uint8_t tx_percent, uint8_t rx_percent)
{
    tx_loss = tx_percent;
    rx_loss = rx_percent;
}

} // namespace sync
} // namespace reair
