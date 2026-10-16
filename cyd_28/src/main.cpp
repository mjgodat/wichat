/* main.cpp -- WiChat CYD firmware entry point
 * ESP32-D0WD-V3, ILI9341 2.8" resistive touch, XPT2046.
 * Arduino framework: setup() + loop(). */
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "keyboard.h"
#include "touch_cal.h"
#include "mesh.h"
#include "radio_esp32.h"
#include "fn_menu.h"

/* Input buffer */
#define INPUT_MAX 128
static char input_buf[INPUT_MAX + 1];
static int  input_len = 0;

/* Track display refresh */
static int last_msg_version = -1;
static uint32_t last_status_ms = 0;

/* Scroll offset (0 = showing newest messages) */
static int scroll_offset = 0;

/* ---- Refresh the chat area from mesh message ring buffer ---- */
static void refresh_chat(void)
{
    display_clear_chat();

    int total = g_mesh.msg_count;
    if (total == 0) return;

    /* Clamp scroll_offset */
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
        int idx;
        if (total <= WICHAT_MAX_MESSAGES) {
            idx = start + i;
        } else {
            idx = (g_mesh.msg_head + start + i) % WICHAT_MAX_MESSAGES;
        }
        display_message(i, g_mesh.messages[idx].sender,
                        g_mesh.messages[idx].text,
                        g_mesh.messages[idx].status);
    }

    /* Show scroll indicator if not at bottom */
    if (scroll_offset > 0) {
        tft.setTextColor(0xFFE0, 0x0000);
        tft.setCursor(TFT_WIDTH - 30, CHAT_Y + 2);
        tft.printf("v%d", scroll_offset);
    }
}

/* ---- Arduino setup ---- */
void setup()
{
    Serial.begin(115200);
    Serial.println("[WiChat CYD] Booting...");

    /* Initialize display */
    display_init();

    /* Touch calibration:
     * - Try to load stored calibration from NVS.
     * - If none exists, or BOOT button (GPIO 0) is held, run calibration.
     * - BOOT button held at startup also enters touch test mode first. */
    pinMode(0, INPUT_PULLUP);
    delay(50);  /* debounce */
    bool boot_held = (digitalRead(0) == LOW);

    if (boot_held) {
        Serial.println("[WiChat] BOOT button held -- entering touch test/cal mode.");
        /* Touch test first so user can see raw coords */
        touch_test_run();
        /* Then run calibration */
        touch_cal_run();
    } else if (!touch_cal_load()) {
        Serial.println("[WiChat] No calibration found -- running first-boot calibration.");
        touch_cal_run();
    }

    display_status_bar("CYD", WICHAT_DEFAULT_CH, 0, 0);

    /* Show boot message */
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(70, 60);
    tft.print("WiChat CYD");
    tft.setTextSize(1);
    tft.setCursor(100, 85);
    tft.print(WICHAT_VERSION);
    tft.setCursor(60, 105);
    tft.print("Initializing radio...");

    /* Initialize promiscuous radio */
    if (radio_esp32_init(WICHAT_DEFAULT_CH) != 0) {
        tft.setCursor(60, 125);
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
        if (scroll_offset == 0) {
            /* Auto-scroll to bottom only if already at bottom */
            refresh_chat();
        }
        display_input(input_buf, input_len);
    }

    /* Check for touch in chat area (above keyboard) for scrolling */
    {
        static uint32_t last_scroll_ms = 0;
        int16_t sx, sy;
        if (tft.getTouch(&sx, &sy) && (now - last_scroll_ms > 300)) {
            int kb_top = TFT_HEIGHT - KB_HEIGHT;
            if (sy >= CHAT_Y && sy < kb_top) {
                /* Touch in chat area — top half scrolls up, bottom half scrolls down */
                int mid = CHAT_Y + (kb_top - CHAT_Y) / 2;
                if (sy < mid) {
                    scroll_offset += CHAT_LINES;  /* scroll up (older) */
                } else {
                    scroll_offset -= CHAT_LINES;  /* scroll down (newer) */
                    if (scroll_offset < 0) scroll_offset = 0;
                }
                last_scroll_ms = now;
                refresh_chat();
            }
        }
    }

    /* Update status bar every 2 seconds */
    if (now - last_status_ms >= 2000) {
        last_status_ms = now;
        display_status_bar(g_mesh.callsign, g_mesh.channel,
                           g_mesh.node_count, now);
        radio_esp32_print_stats();
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

    case KB_FUNC: fn_menu_show(); display_input(input_buf,input_len); keyboard_draw(); break;
    case KB_SHIFT:
        /* Keyboard handles its own redraw */
        break;

    case KB_NONE:
    default:
        break;
    }

    delay(10);
}
