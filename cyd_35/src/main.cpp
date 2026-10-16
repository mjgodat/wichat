/* main.cpp -- WiChat CYD firmware entry point
 * Freenove ESP32-S3 3.5" (FNK0104N), ST77922 QSPI.
 * Arduino framework: setup() + loop(). */
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "touch_cal.h"
#include "keyboard.h"
#include "mesh.h"
#include "radio_esp32.h"

#define INPUT_MAX 128
static char input_buf[INPUT_MAX + 1];
static int  input_len = 0;
static int last_msg_version = -1;
static uint32_t last_status_ms = 0;
static int scroll_offset = 0;

static void refresh_chat(void) {
    display_clear_chat();
    int total = g_mesh.msg_count;
    if (total == 0) return;
    int max_scroll = total - CHAT_LINES;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset > max_scroll) scroll_offset = max_scroll;
    if (scroll_offset < 0) scroll_offset = 0;
    int start = total - CHAT_LINES - scroll_offset;
    if (start < 0) start = 0;
    int count = total - start - scroll_offset;
    if (count > CHAT_LINES) count = CHAT_LINES;
    if (count < 0) count = 0;
    for (int i = 0; i < count; i++) {
        int idx = (total <= WICHAT_MAX_MESSAGES) ? start + i :
                  (g_mesh.msg_head + start + i) % WICHAT_MAX_MESSAGES;
        display_message(i, g_mesh.messages[idx].sender,
                        g_mesh.messages[idx].text,
                        g_mesh.messages[idx].status);
    }
    if (scroll_offset > 0) {
        display_setTextColor(0xFFE0, 0x0000);
        display_setCursor(TFT_WIDTH - 30, CHAT_Y + 2);
        display_printf("v%d", scroll_offset);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[WiChat CYD] Booting (Freenove S3 3.5\")...");

    display_init();
    touch_init();

    display_status_bar("CYD", WICHAT_DEFAULT_CH, 0, 0);
    display_setTextColor(0xFFFF, 0x0000);
    display_setTextSize(2);
    display_setCursor(120, 100);
    display_print("WiChat CYD");
    display_setTextSize(1);
    display_setCursor(140, 130);
    display_print(WICHAT_VERSION);
    display_setCursor(100, 160);
    display_print("Initializing radio...");

    if (radio_esp32_init(WICHAT_DEFAULT_CH) != 0) {
        display_setCursor(100, 180);
        display_setTextColor(0xF800, 0x0000);
        display_print("RADIO INIT FAILED!");
        Serial.println("[WiChat CYD] Radio init failed!");
        while (1) delay(1000);
    }

    cyd_mesh_init("CYD");
    Serial.printf("[WiChat CYD] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  g_mesh.local_mac[0], g_mesh.local_mac[1],
                  g_mesh.local_mac[2], g_mesh.local_mac[3],
                  g_mesh.local_mac[4], g_mesh.local_mac[5]);

    delay(500);
    display_clear();
    display_status_bar(g_mesh.callsign, g_mesh.channel, 0, 0);
    display_input(input_buf, input_len);
    keyboard_init();
    keyboard_draw();

    cyd_mesh_send_beacon();
    g_mesh.last_beacon_ms = millis();
    Serial.println("[WiChat CYD] Ready.");
}

void loop() {
    uint32_t now = millis();
    uint8_t rx_buf[512];
    int8_t rssi;

    int rx_len = radio_esp32_rx(rx_buf, sizeof(rx_buf), &rssi);
    if (rx_len > 0) cyd_mesh_process_rx(rx_buf, (size_t)rx_len, rssi);
    cyd_mesh_tick(now);

    if (g_mesh.msg_version != last_msg_version) {
        last_msg_version = g_mesh.msg_version;
        if (scroll_offset == 0) refresh_chat();
        display_input(input_buf, input_len);
    }

    { static uint32_t last_scroll_ms = 0;
      int16_t sx, sy;
      if (touch_read(&sx, &sy) && (now - last_scroll_ms > 300)) {
          int kb_top = TFT_HEIGHT - KB_HEIGHT;
          if (sy >= CHAT_Y && sy < kb_top) {
              int mid = CHAT_Y + (kb_top - CHAT_Y) / 2;
              if (sy < mid) scroll_offset += CHAT_LINES;
              else { scroll_offset -= CHAT_LINES; if (scroll_offset < 0) scroll_offset = 0; }
              last_scroll_ms = now;
              refresh_chat();
          }
      }
    }

    if (now - last_status_ms >= 2000) {
        last_status_ms = now;
        display_status_bar(g_mesh.callsign, g_mesh.channel, g_mesh.node_count, now);
        radio_esp32_print_stats();
    }

    kb_event_t evt = keyboard_poll();
    switch (evt.type) {
    case KB_CHAR:
        if (input_len < INPUT_MAX) { input_buf[input_len++] = evt.ch; input_buf[input_len] = '\0'; display_input(input_buf, input_len); }
        break;
    case KB_BACKSPACE:
        if (input_len > 0) { input_buf[--input_len] = '\0'; display_input(input_buf, input_len); }
        break;
    case KB_ENTER:
        if (input_len > 0) { cyd_mesh_send_chat(input_buf); input_buf[0] = '\0'; input_len = 0; display_input(input_buf, input_len); }
        break;
    case KB_SHIFT: case KB_NONE: default: break;
    }

    delay(10);
}
