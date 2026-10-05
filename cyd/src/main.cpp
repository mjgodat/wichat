/* main.cpp -- WiChat CYD firmware entry point
 * ESP32-D0WD-V3, ST7796 3.5" capacitive touch, GT911.
 * Arduino framework: setup() + loop(). */
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "keyboard.h"
#include "mesh.h"
#include "radio_esp32.h"

/* Input buffer */
#define INPUT_MAX 128
static char input_buf[INPUT_MAX + 1];
static int  input_len = 0;

/* Track display refresh */
static int last_msg_version = -1;
static uint32_t last_status_ms = 0;

/* ---- Refresh the chat area from mesh message ring buffer ---- */
static void refresh_chat(void)
{
    display_clear_chat();

    int total = g_mesh.msg_count;
    int start;
    int count;

    if (total <= CHAT_LINES) {
        start = 0;
        count = total;
    } else {
        /* Show the most recent CHAT_LINES messages */
        start = total - CHAT_LINES;
        count = CHAT_LINES;
    }

    for (int i = 0; i < count; i++) {
        /* msg_head points to the NEXT write slot.
         * Messages are stored in ring order. Walk backwards from msg_head. */
        int idx;
        if (total <= WICHAT_MAX_MESSAGES) {
            idx = start + i;
        } else {
            /* Ring buffer wrapped: oldest is at msg_head, newest at msg_head-1 */
            idx = (g_mesh.msg_head + start + i) % WICHAT_MAX_MESSAGES;
        }
        display_message(i, g_mesh.messages[idx].sender,
                        g_mesh.messages[idx].text);
    }
}

/* ---- Arduino setup ---- */
void setup()
{
    Serial.begin(115200);
    Serial.println("[WiChat CYD] Booting...");

    /* Initialize display */
    display_init();
    display_status_bar("CYD", WICHAT_DEFAULT_CH, 0, 0);

    /* Show boot message */
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(120, 100);
    tft.print("WiChat CYD");
    tft.setTextSize(1);
    tft.setCursor(140, 130);
    tft.print(WICHAT_VERSION);
    tft.setCursor(100, 160);
    tft.print("Initializing radio...");

    /* Initialize promiscuous radio */
    if (radio_esp32_init(WICHAT_DEFAULT_CH) != 0) {
        tft.setCursor(100, 180);
        tft.setTextColor(0xF800, 0x0000);  /* red */
        tft.print("RADIO INIT FAILED!");
        Serial.println("[WiChat CYD] Radio init failed!");
        while (1) delay(1000);  /* halt */
    }

    /* Initialize mesh engine (gets MAC from radio) */
    cyd_mesh_init("CYD");

    Serial.printf("[WiChat CYD] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  g_mesh.local_mac[0], g_mesh.local_mac[1],
                  g_mesh.local_mac[2], g_mesh.local_mac[3],
                  g_mesh.local_mac[4], g_mesh.local_mac[5]);

    /* Clear screen and draw UI */
    delay(500);
    display_clear();
    display_status_bar(g_mesh.callsign, g_mesh.channel, 0, 0);
    display_input(input_buf, input_len);

    /* Initialize and draw keyboard */
    keyboard_init();
    keyboard_draw();

    /* Send initial beacon */
    cyd_mesh_send_beacon();
    g_mesh.last_beacon_ms = millis();

    Serial.println("[WiChat CYD] Ready.");
}

/* ---- Arduino loop ---- */
void loop()
{
    uint32_t now = millis();
    uint8_t rx_buf[512];
    int8_t rssi;

    /* Poll radio RX */
    int rx_len = radio_esp32_rx(rx_buf, sizeof(rx_buf), &rssi);
    if (rx_len > 0) {
        cyd_mesh_process_rx(rx_buf, (size_t)rx_len, rssi);
    }

    /* Mesh tick (beacons, node expiry) */
    cyd_mesh_tick(now);

    /* Refresh chat display if new messages arrived */
    if (g_mesh.msg_version != last_msg_version) {
        last_msg_version = g_mesh.msg_version;
        refresh_chat();
        display_input(input_buf, input_len);
    }

    /* Update status bar every 2 seconds */
    if (now - last_status_ms >= 2000) {
        last_status_ms = now;
        display_status_bar(g_mesh.callsign, g_mesh.channel,
                           g_mesh.node_count, now);
    }

    /* Poll keyboard */
    kb_event_t evt = keyboard_poll();
    switch (evt.type) {
    case KB_CHAR:
        if (input_len < INPUT_MAX) {
            input_buf[input_len++] = evt.ch;
            input_buf[input_len] = '\0';
            display_input(input_buf, input_len);
        }
        break;

    case KB_BACKSPACE:
        if (input_len > 0) {
            input_buf[--input_len] = '\0';
            display_input(input_buf, input_len);
        }
        break;

    case KB_ENTER:
        if (input_len > 0) {
            cyd_mesh_send_chat(input_buf);
            input_buf[0] = '\0';
            input_len = 0;
            /* Chat will refresh on next msg_version check */
            display_input(input_buf, input_len);
        }
        break;

    case KB_SHIFT:
        /* Keyboard handles its own redraw */
        break;

    case KB_NONE:
    default:
        break;
    }

    delay(10);
}
