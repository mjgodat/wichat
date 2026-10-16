/* radio_esp32.cpp -- Dual transport radio for WiChat
 * Transport 1: Raw 802.11 promiscuous mode (PC compatible)
 * Transport 2: ESP-NOW Long Range (CYD-to-CYD, ~1km range)
 * Both share the same RX queue and frame format. */
#include "radio_esp32.h"
#include "config.h"
#include <Arduino.h>
#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_now.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define RX_BUF_MAX 512

typedef struct {
    uint8_t  data[RX_BUF_MAX];
    uint16_t len;
    int8_t   rssi;
} rx_entry_t;

static QueueHandle_t rx_queue = NULL;
static const uint8_t BSSID_FILTER[6] = WICHAT_BSSID;
static const uint8_t ESPNOW_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* Stats */
static volatile uint32_t rx_total = 0;
static volatile uint32_t rx_bssid_match = 0;
static volatile uint32_t rx_espnow = 0;
static int tx_count = 0;

/* ---- Promiscuous RX callback (raw 802.11) ---- */
static void IRAM_ATTR promisc_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *frame = pkt->payload;
    uint16_t frame_len = pkt->rx_ctrl.sig_len;

    rx_total++;

    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT)
        return;
    if (frame_len < 24 || frame_len > RX_BUF_MAX)
        return;

    /* Filter by WiChat BSSID at offset 16 */
    if (memcmp(frame + 16, BSSID_FILTER, 6) == 0) {
        rx_bssid_match++;
        rx_entry_t entry;
        entry.len = frame_len;
        entry.rssi = pkt->rx_ctrl.rssi;
        memcpy(entry.data, frame, frame_len);
        xQueueSendFromISR(rx_queue, &entry, NULL);
    }
}

/* ---- ESP-NOW RX callback ---- */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void espnow_rx_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    int8_t rssi = info->rx_ctrl->rssi;
#else
static void espnow_rx_cb(const uint8_t *mac, const uint8_t *data, int len)
{
    int8_t rssi = -50; /* ESP-IDF 4.x doesn't provide RSSI in callback */
#endif
    if (!data || len < 10 || len > RX_BUF_MAX)
        return;

    rx_espnow++;

    /* ESP-NOW payload is the raw WiChat frame (same as what goes inside 802.11).
     * We need to wrap it in a minimal 802.11-like header so mesh_process_rx
     * can parse it the same way. Actually, the mesh layer calls frame_decode()
     * which expects the raw 802.11 frame with headers.
     *
     * Simpler approach: ESP-NOW carries the ENCODED frame (same bytes as
     * radio_esp32_tx sends via 802.11). So we queue it directly. */
    rx_entry_t entry;
    entry.len = (uint16_t)len;
    entry.rssi = rssi;
    memcpy(entry.data, data, len);
    xQueueSendFromISR(rx_queue, &entry, NULL);
}

/* ---- ESP-NOW TX callback ---- */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void espnow_tx_cb(const uint8_t *mac, esp_now_send_status_t status) { }
#else
static void espnow_tx_cb(const uint8_t *mac, esp_now_send_status_t status) { }
#endif

/* ---- Public API ---- */

int radio_esp32_init(uint8_t channel)
{
    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_event_loop_create_default();

    /* WiFi init */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);

    /* Enable BOTH 802.11b AND LR protocols for dual transport */
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_LR);

    esp_wifi_start();
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    /* Max TX power */
    esp_wifi_set_max_tx_power(80);
    {
        int8_t actual_power;
        esp_wifi_get_max_tx_power(&actual_power);
        Serial.printf("[RADIO] TX power: %d (%.1fdBm)\n", actual_power, actual_power / 4.0f);
    }

    /* Set 1Mbps long preamble for raw 802.11 */
    esp_wifi_config_80211_tx_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);
    Serial.println("[RADIO] Raw 802.11: 1Mbps long preamble");

    /* Create shared RX queue */
    rx_queue = xQueueCreate(32, sizeof(rx_entry_t));
    if (!rx_queue) return -1;

    /* Set up promiscuous mode (raw 802.11 RX) */
    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(promisc_rx_cb);
    esp_wifi_set_promiscuous(true);
    Serial.println("[RADIO] Promiscuous mode ON");

    /* Set up ESP-NOW (LR transport) */
    esp_err_t enr = esp_now_init();
    if (enr != ESP_OK) {
        Serial.printf("[RADIO] ESP-NOW init failed: %d\n", enr);
    } else {
        esp_now_register_recv_cb(espnow_rx_cb);
        esp_now_register_send_cb(espnow_tx_cb);

        /* Add broadcast peer for ESP-NOW */
        esp_now_peer_info_t peer;
        memset(&peer, 0, sizeof(peer));
        memcpy(peer.peer_addr, ESPNOW_BROADCAST, 6);
        peer.channel = channel;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false;
        esp_now_add_peer(&peer);

        /* Set ESP-NOW to LR rate for maximum range */
        esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_LORA_250K);
        Serial.println("[RADIO] ESP-NOW LR: 250Kbps LoRa-like (max range)");
    }

    Serial.printf("[RADIO] Dual transport ready on CH %d\n", channel);
    return 0;
}

void radio_esp32_get_mac(uint8_t mac[6])
{
    esp_wifi_get_mac(WIFI_IF_STA, mac);
}

int radio_esp32_tx(const uint8_t *data, size_t len)
{
    if (!data || len == 0 || len > RX_BUF_MAX)
        return -1;

    int err_raw = -1, err_espnow = -1;

    /* Transport 1: Raw 802.11 (PC compatible) */
    esp_err_t e1 = esp_wifi_80211_tx(WIFI_IF_STA, data, (int)len, true);
    err_raw = (e1 == ESP_OK) ? 0 : -1;

    /* Transport 2: ESP-NOW broadcast (LR range) */
    esp_err_t e2 = esp_now_send(ESPNOW_BROADCAST, data, len);
    err_espnow = (e2 == ESP_OK) ? 0 : -1;

    tx_count++;
    if (tx_count <= 5 || tx_count % 10 == 0)
        Serial.printf("[TX] #%d len=%d raw=%d espnow=%d\n", tx_count, (int)len, e1, e2);

    return (err_raw == 0 || err_espnow == 0) ? 0 : -1;
}

void radio_esp32_print_stats(void)
{
    Serial.printf("[RADIO] rx_raw=%lu rx_bssid=%lu rx_espnow=%lu tx=%d\n",
                  (unsigned long)rx_total, (unsigned long)rx_bssid_match,
                  (unsigned long)rx_espnow, tx_count);
}

int radio_esp32_rx(uint8_t *buf, size_t max_len, int8_t *rssi)
{
    if (!rx_queue) return 0;

    rx_entry_t entry;
    if (xQueueReceive(rx_queue, &entry, 0) != pdTRUE)
        return 0;

    size_t copy_len = entry.len;
    if (copy_len > max_len) copy_len = max_len;

    memcpy(buf, entry.data, copy_len);
    if (rssi) *rssi = entry.rssi;
    return (int)copy_len;
}
