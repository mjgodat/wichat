/* main.cpp -- WiChat CYD (Freenove ESP32-S3 3.5" FNK0104N) */
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "touch_cal.h"
#include "keyboard.h"
#include "mesh.h"
#include "radio_esp32.h"
#include "fn_menu.h"

#define INPUT_MAX 128
static char input_buf[INPUT_MAX + 1];
static int input_len = 0;
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
    display_flush();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[WiChat] Booting (Freenove S3 3.5\")...");

    display_init();
    touch_init();

    display_status_bar("CYD", WICHAT_DEFAULT_CH, 0, 0);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(90, 80);
    tft.print("WiChat CYD");
    tft.setTextSize(2);
    tft.setCursor(150, 120);
    tft.print(WICHAT_VERSION);
    tft.setCursor(80, 150);
    tft.print("Initializing...");
    display_flush();

    if (radio_esp32_init(WICHAT_DEFAULT_CH) != 0) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(80, 180);
        tft.print("RADIO FAILED!");
        display_flush();
        Serial.println("[WiChat] Radio init failed!");
        while (1) delay(1000);
    }

    cyd_mesh_init("CYD");
    Serial.printf("[WiChat] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        g_mesh.local_mac[0], g_mesh.local_mac[1], g_mesh.local_mac[2],
        g_mesh.local_mac[3], g_mesh.local_mac[4], g_mesh.local_mac[5]);

    delay(500);
    display_clear();
    display_status_bar(g_mesh.callsign, g_mesh.channel, 0, 0);
    display_input(input_buf, input_len);
    keyboard_init();
    keyboard_draw();
    display_flush();

    cyd_mesh_send_beacon();
    g_mesh.last_beacon_ms = millis();
    Serial.println("[WiChat] Ready.");
}

void loop() {
    uint32_t now = millis();
    uint8_t rx_buf[512]; int8_t rssi;
    bool needs_flush = false;

    int rx_len = radio_esp32_rx(rx_buf, sizeof(rx_buf), &rssi);
    if (rx_len > 0) cyd_mesh_process_rx(rx_buf, (size_t)rx_len, rssi);
    cyd_mesh_tick(now);

    if (g_mesh.msg_version != last_msg_version) {
        last_msg_version = g_mesh.msg_version;
        if (scroll_offset == 0) refresh_chat();
        display_input(input_buf, input_len);
        needs_flush = true;
    }

    { static uint32_t lsm = 0; int16_t sx, sy;
      if (touch_read(&sx, &sy) && (now - lsm > 300)) {
          int kbt = TFT_HEIGHT - KB_HEIGHT;
          if (sy >= CHAT_Y && sy < kbt) {
              int mid = CHAT_Y + (kbt - CHAT_Y) / 2;
              if (sy < mid) scroll_offset += CHAT_LINES;
              else { scroll_offset -= CHAT_LINES; if (scroll_offset < 0) scroll_offset = 0; }
              lsm = now; refresh_chat();
          }
      }
    }

    if (now - last_status_ms >= 2000) {
        last_status_ms = now;
        display_status_bar(g_mesh.callsign, g_mesh.channel, g_mesh.node_count, now);
        radio_esp32_print_stats();
        needs_flush = true;
    }

    kb_event_t evt = keyboard_poll();
    switch (evt.type) {
    case KB_CHAR: if(input_len<INPUT_MAX){input_buf[input_len++]=evt.ch;input_buf[input_len]='\0';display_input(input_buf,input_len);needs_flush=true;}break;
    case KB_BACKSPACE: if(input_len>0){input_buf[--input_len]='\0';display_input(input_buf,input_len);needs_flush=true;}break;
    case KB_ENTER: if(input_len>0){cyd_mesh_send_chat(input_buf);input_buf[0]='\0';input_len=0;display_input(input_buf,input_len);needs_flush=true;}break;
    case KB_FUNC: fn_menu_show(); display_input(input_buf,input_len); keyboard_draw(); needs_flush=true; break;
    case KB_SHIFT: needs_flush=true; break;
    default: break;
    }

    if (needs_flush) display_flush();
    delay(10);
}
