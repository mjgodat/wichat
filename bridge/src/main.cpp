/* main.cpp -- WiChat ESP32 USB Serial Bridge
 *
 * Turns a bare ESP32 dev board into a USB-to-WiFi modem for the PC.
 * No display, no keyboard, no mesh logic -- just radio + serial framing.
 *
 * Serial protocol: SLIP (RFC 1055) at 921600 baud.
 * Each SLIP frame is: [tag byte] [payload...]
 *
 * Tags:
 *   0x01  PC->ESP  TX_FRAME     raw 802.11 frame to transmit
 *   0x02  ESP->PC  RX_FRAME     received 802.11 frame + 1 byte RSSI (signed)
 *   0x03  PC->ESP  SET_CHANNEL  1 byte channel number (1-14)
 *   0x04  PC->ESP  GET_MAC      (no payload)
 *   0x05  ESP->PC  MAC_RESP     6 bytes MAC address
 *   0x06  ESP->PC  STATUS       heartbeat: 4B rx_count + 4B tx_count + 1B channel
 */

#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

/* ================================================================
 * Configuration
 * ================================================================ */
#ifndef BRIDGE_BAUD
#define BRIDGE_BAUD 921600
#endif

#define RX_BUF_MAX    512    /* max raw 802.11 frame we buffer */
#define RX_QUEUE_DEPTH 32    /* FreeRTOS queue depth for radio RX */
#define SLIP_BUF_MAX  1100   /* max SLIP-encoded frame (worst case 2x + 2) */
#define SERIAL_BUF_MAX 1100  /* max incoming SLIP frame accumulator */
#define STATUS_INTERVAL_MS 5000

/* SLIP special bytes */
#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

/* Command tags */
#define TAG_TX_FRAME    0x01
#define TAG_RX_FRAME    0x02
#define TAG_SET_CHANNEL 0x03
#define TAG_GET_MAC     0x04
#define TAG_MAC_RESP    0x05
#define TAG_STATUS      0x06

/* WiChat BSSID filter */
static const uint8_t BSSID_FILTER[6] = {0x57, 0x43, 0x57, 0x43, 0x57, 0x43};

/* ================================================================
 * Radio RX queue
 * ================================================================ */
typedef struct {
    uint8_t  data[RX_BUF_MAX];
    uint16_t len;
    int8_t   rssi;
} rx_entry_t;

static QueueHandle_t rx_queue = NULL;

/* Stats */
static volatile uint32_t stat_rx_total = 0;
static volatile uint32_t stat_rx_match = 0;
static volatile uint32_t stat_tx_count = 0;
static uint8_t current_channel = 1;

/* ================================================================
 * Promiscuous RX callback (runs in WiFi task context)
 * ================================================================ */
static void IRAM_ATTR promisc_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *frame = pkt->payload;
    uint16_t frame_len = pkt->rx_ctrl.sig_len;

    stat_rx_total++;

    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT)
        return;

    if (frame_len < 24 || frame_len > RX_BUF_MAX)
        return;

    /* Filter by BSSID at offset 16 in 802.11 data frame header */
    if (memcmp(frame + 16, BSSID_FILTER, 6) != 0)
        return;

    stat_rx_match++;

    rx_entry_t entry;
    entry.len = frame_len;
    entry.rssi = pkt->rx_ctrl.rssi;
    memcpy(entry.data, frame, frame_len);

    /* Non-blocking -- drop if queue full */
    xQueueSendFromISR(rx_queue, &entry, NULL);
}

/* ================================================================
 * Radio init
 * ================================================================ */
static int radio_init(uint8_t channel)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_max_tx_power(80); /* 20 dBm */

    rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rx_entry_t));
    if (!rx_queue)
        return -1;

    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(promisc_rx_cb);
    esp_wifi_set_promiscuous(true);

    current_channel = channel;
    return 0;
}

static int radio_tx(const uint8_t *data, size_t len)
{
    if (!data || len == 0 || len > RX_BUF_MAX)
        return -1;
    esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, data, (int)len, true);
    if (err == ESP_OK) {
        stat_tx_count++;
        return 0;
    }
    return -1;
}

static void radio_set_channel(uint8_t ch)
{
    if (ch < 1 || ch > 14)
        return;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    current_channel = ch;
}

/* ================================================================
 * SLIP encoder: write a SLIP frame to Serial
 * ================================================================ */
static void slip_send(const uint8_t *data, size_t len)
{
    Serial.write(SLIP_END);
    for (size_t i = 0; i < len; i++) {
        switch (data[i]) {
        case SLIP_END:
            Serial.write(SLIP_ESC);
            Serial.write(SLIP_ESC_END);
            break;
        case SLIP_ESC:
            Serial.write(SLIP_ESC);
            Serial.write(SLIP_ESC_ESC);
            break;
        default:
            Serial.write(data[i]);
            break;
        }
    }
    Serial.write(SLIP_END);
}

/* ================================================================
 * SLIP decoder state machine
 * ================================================================ */
static uint8_t slip_buf[SERIAL_BUF_MAX];
static size_t  slip_len = 0;
static bool    slip_esc = false;

/* Returns true when a complete frame is in slip_buf[0..slip_len-1] */
static bool slip_feed(uint8_t byte)
{
    if (byte == SLIP_END) {
        if (slip_len > 0) {
            /* Complete frame */
            return true;
        }
        /* Empty frame or leading END -- reset */
        slip_len = 0;
        slip_esc = false;
        return false;
    }

    if (slip_esc) {
        slip_esc = false;
        if (byte == SLIP_ESC_END)
            byte = SLIP_END;
        else if (byte == SLIP_ESC_ESC)
            byte = SLIP_ESC;
        /* else: protocol error, pass through */
    } else if (byte == SLIP_ESC) {
        slip_esc = true;
        return false;
    }

    if (slip_len < SERIAL_BUF_MAX) {
        slip_buf[slip_len++] = byte;
    }
    /* else: overflow, silently drop byte (frame will be corrupt) */

    return false;
}

static void slip_reset(void)
{
    slip_len = 0;
    slip_esc = false;
}

/* ================================================================
 * Command handlers
 * ================================================================ */
static void handle_serial_frame(const uint8_t *data, size_t len)
{
    if (len < 1)
        return;

    uint8_t tag = data[0];
    const uint8_t *payload = data + 1;
    size_t payload_len = len - 1;

    switch (tag) {

    case TAG_TX_FRAME:
        /* Transmit the raw 802.11 frame */
        if (payload_len > 0) {
            radio_tx(payload, payload_len);
        }
        break;

    case TAG_SET_CHANNEL:
        if (payload_len >= 1) {
            radio_set_channel(payload[0]);
        }
        break;

    case TAG_GET_MAC: {
        uint8_t resp[7];
        resp[0] = TAG_MAC_RESP;
        esp_wifi_get_mac(WIFI_IF_STA, resp + 1);
        slip_send(resp, 7);
        break;
    }

    default:
        /* Unknown tag -- ignore */
        break;
    }
}

/* ================================================================
 * Send an RX frame to the PC
 * ================================================================ */
static void send_rx_frame(const rx_entry_t *entry)
{
    /* Format: [TAG_RX_FRAME] [rssi as uint8] [raw frame...] */
    uint8_t pkt[2 + RX_BUF_MAX];
    pkt[0] = TAG_RX_FRAME;
    pkt[1] = (uint8_t)entry->rssi;  /* signed->unsigned cast, PC casts back */
    memcpy(pkt + 2, entry->data, entry->len);
    slip_send(pkt, 2 + entry->len);
}

/* ================================================================
 * Send periodic status heartbeat
 * ================================================================ */
static void send_status(void)
{
    uint8_t pkt[10];
    pkt[0] = TAG_STATUS;
    /* rx_count (4 bytes LE) */
    uint32_t rx = stat_rx_match;
    memcpy(pkt + 1, &rx, 4);
    /* tx_count (4 bytes LE) */
    uint32_t tx = stat_tx_count;
    memcpy(pkt + 5, &tx, 4);
    /* channel */
    pkt[9] = current_channel;
    slip_send(pkt, 10);
}

/* ================================================================
 * Arduino setup + loop
 * ================================================================ */
void setup()
{
    Serial.begin(BRIDGE_BAUD);

    /* Wait a moment for USB CDC to settle */
    delay(100);

    Serial.println();  /* flush any garbage */
    Serial.println("[BRIDGE] WiChat ESP32 USB Bridge starting...");

    if (radio_init(1) != 0) {
        Serial.println("[BRIDGE] RADIO INIT FAILED!");
        while (1) delay(1000);
    }

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    Serial.printf("[BRIDGE] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("[BRIDGE] Channel: %d  Baud: %d\n", current_channel, BRIDGE_BAUD);
    Serial.println("[BRIDGE] Ready. Entering SLIP mode.");

    /* Send initial MAC response so PC can grab it at startup */
    {
        uint8_t resp[7];
        resp[0] = TAG_MAC_RESP;
        memcpy(resp + 1, mac, 6);
        slip_send(resp, 7);
    }
}

void loop()
{
    static uint32_t last_status_ms = 0;
    uint32_t now = millis();

    /* ---- Drain serial RX: PC -> ESP32 commands ---- */
    while (Serial.available()) {
        uint8_t b = Serial.read();
        if (slip_feed(b)) {
            handle_serial_frame(slip_buf, slip_len);
            slip_reset();
        }
    }

    /* ---- Drain radio RX queue: air -> PC ---- */
    rx_entry_t entry;
    int drained = 0;
    while (drained < 8 && xQueueReceive(rx_queue, &entry, 0) == pdTRUE) {
        send_rx_frame(&entry);
        drained++;
    }

    /* ---- Periodic status ---- */
    if (now - last_status_ms >= STATUS_INTERVAL_MS) {
        last_status_ms = now;
        send_status();
    }

    /* Yield -- keep loop responsive */
    delay(1);
}
