/* radio_esp32.cpp -- ESP32 promiscuous-mode radio for WiChat */
#include "radio_esp32.h"
#include "config.h"
#include <Arduino.h>
#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/* Max raw frame size we will buffer */
#define RX_BUF_MAX 512

/* RX queue entry */
typedef struct {
    uint8_t  data[RX_BUF_MAX];
    uint16_t len;
    int8_t   rssi;
} rx_entry_t;

/* FreeRTOS queue for received frames */
static QueueHandle_t rx_queue = NULL;

/* Our BSSID filter */
static const uint8_t BSSID_FILTER[6] = WICHAT_BSSID;

/* ---- Promiscuous RX callback ---- */
static volatile uint32_t rx_total = 0;
static volatile uint32_t rx_bssid_match = 0;

static void IRAM_ATTR promisc_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *frame = pkt->payload;
    uint16_t frame_len = pkt->rx_ctrl.sig_len;

    rx_total++;

    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT)
        return;

    /* Sanity check length */
    if (frame_len < 24 || frame_len > RX_BUF_MAX)
        return;

    /* Debug: dump BSSID of first 20 data frames */
    static int dump_count = 0;
    if (type == WIFI_PKT_DATA && dump_count < 20) {
        dump_count++;
        Serial.printf("[RX#%d] len=%d type=%d fc=0x%02x%02x bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
                      dump_count, frame_len, type,
                      frame[0], frame[1],
                      frame[16], frame[17], frame[18], frame[19], frame[20], frame[21]);
    }

    /* Filter by BSSID at offset 16 in 802.11 data frame header */
    if (memcmp(frame + 16, BSSID_FILTER, 6) == 0) {
        rx_bssid_match++;

        /* Queue the frame */
        rx_entry_t entry;
        entry.len = frame_len;
        entry.rssi = pkt->rx_ctrl.rssi;
        memcpy(entry.data, frame, frame_len);

        /* Non-blocking send -- drop if queue is full */
        xQueueSendFromISR(rx_queue, &entry, NULL);
    }
}

/* ---- Public API ---- */

int radio_esp32_init(uint8_t channel)
{
    /* Initialize NVS (required before WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Create the default event loop (required by WiFi) */
    esp_event_loop_create_default();

    /* Initialize WiFi with default config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    /* Set WiFi to STA mode (required for esp_wifi_80211_tx and MAC access) */
    esp_wifi_set_mode(WIFI_MODE_STA);

    /* Force 802.11b only — 1Mbps DSSS has best range (~10dB over OFDM) */
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);

    esp_wifi_start();

    /* Set channel */
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    /* Set max TX power (20 dBm = 80 quarter-dBm) */
    esp_wifi_set_max_tx_power(80);

    /* Verify actual TX power */
    {
        int8_t actual_power;
        esp_wifi_get_max_tx_power(&actual_power);
        Serial.printf("[RADIO] TX power: requested 80 (20dBm), actual %d (%.1fdBm)\n",
                      actual_power, actual_power / 4.0f);
    }

    /* Force 1Mbps long preamble — maximum range PHY rate */
    esp_wifi_config_80211_tx_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);
    Serial.println("[RADIO] TX rate: 1Mbps long preamble (max range)");

    /* Create RX queue */
    rx_queue = xQueueCreate(16, sizeof(rx_entry_t));
    if (!rx_queue)
        return -1;

    /* Set promiscuous filter: data frames only */
    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);

    /* Register callback and enable promiscuous mode */
    esp_wifi_set_promiscuous_rx_cb(promisc_rx_cb);
    esp_wifi_set_promiscuous(true);

    return 0;
}

void radio_esp32_get_mac(uint8_t mac[6])
{
    esp_wifi_get_mac(WIFI_IF_STA, mac);
}

static int tx_count = 0;
int radio_esp32_tx(const uint8_t *data, size_t len)
{
    if (!data || len == 0 || len > RX_BUF_MAX)
        return -1;

    /* Quiet backlight PWM during TX to reduce RF noise on 2.4GHz */
    ledcWrite(7, 0);
    esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, data, (int)len, true);
    ledcWrite(7, 200);

    tx_count++;
    Serial.printf("[TX] #%d len=%d err=%d\n", tx_count, (int)len, err);
    return (err == ESP_OK) ? 0 : -1;
}

void radio_esp32_print_stats(void)
{
    Serial.printf("[RADIO] total_rx=%lu bssid_match=%lu\n",
                  (unsigned long)rx_total, (unsigned long)rx_bssid_match);
}

int radio_esp32_rx(uint8_t *buf, size_t max_len, int8_t *rssi)
{
    if (!rx_queue)
        return 0;

    rx_entry_t entry;
    if (xQueueReceive(rx_queue, &entry, 0) != pdTRUE)
        return 0;

    size_t copy_len = entry.len;
    if (copy_len > max_len)
        copy_len = max_len;

    memcpy(buf, entry.data, copy_len);
    if (rssi)
        *rssi = entry.rssi;

    return (int)copy_len;
}
