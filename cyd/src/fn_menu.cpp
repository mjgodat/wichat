/* fn_menu.cpp -- Function menu popup for WiChat */
#include "fn_menu.h"
#include "display.h"
#include "touch_cal.h"
#include "config.h"
#include "mesh.h"
#include "frame.h"
#include "radio_esp32.h"

/* From mesh.cpp */
extern int mesh_transmit(const wichat_frame_t *frame);
#include <Arduino.h>
#include <esp_wifi.h>

#define MENU_X  100
#define MENU_Y  30
#define MENU_W  280
#define MENU_H  240
#define ITEM_H  30
#define ITEM_GAP 4
#define NUM_ITEMS 7

#define COL_MENU_BG    0x2104
#define COL_MENU_BORDER 0x07E0
#define COL_MENU_ITEM  0x4208
#define COL_MENU_TEXT  TFT_WHITE
#define COL_MENU_TITLE TFT_CYAN

static const char *menu_labels[NUM_ITEMS] = {
    "Ping Network",
    "Node List",
    "Change Channel",
    "Change Name",
    "Stats",
    "Scan Channels",
    "Clear Chat"
};

static void draw_menu(void) {
    /* Background with border */
    tft.fillRect(MENU_X - 2, MENU_Y - 2, MENU_W + 4, MENU_H + 4, COL_MENU_BORDER);
    tft.fillRect(MENU_X, MENU_Y, MENU_W, MENU_H, COL_MENU_BG);

    /* Title */
    tft.setTextSize(2);
    tft.setTextColor(COL_MENU_TITLE, COL_MENU_BG);
    tft.setCursor(MENU_X + 70, MENU_Y + 6);
    tft.print("FUNCTIONS");

    /* Items */
    tft.setTextSize(2);
    for (int i = 0; i < NUM_ITEMS; i++) {
        int iy = MENU_Y + 30 + i * (ITEM_H + ITEM_GAP);
        tft.fillRect(MENU_X + 10, iy, MENU_W - 20, ITEM_H, COL_MENU_ITEM);
        tft.setTextColor(COL_MENU_TEXT, COL_MENU_ITEM);
        tft.setCursor(MENU_X + 20, iy + 7);
        tft.print(menu_labels[i]);
    }
    display_flush();
}

static int wait_for_selection(void) {
    /* Wait for touch release first */
    int16_t tx, ty;
    while (touch_read(&tx, &ty)) delay(10);
    delay(100);

    /* Wait for tap */
    while (true) {
        if (touch_read(&tx, &ty)) {
            /* Check if outside menu */
            if (tx < MENU_X || tx > MENU_X + MENU_W || ty < MENU_Y || ty > MENU_Y + MENU_H) {
                while (touch_read(&tx, &ty)) delay(10);
                return -1;
            }
            /* Check which item */
            for (int i = 0; i < NUM_ITEMS; i++) {
                int iy = MENU_Y + 30 + i * (ITEM_H + ITEM_GAP);
                if (ty >= iy && ty < iy + ITEM_H && tx >= MENU_X + 10 && tx < MENU_X + MENU_W - 10) {
                    while (touch_read(&tx, &ty)) delay(10);
                    return i;
                }
            }
            while (touch_read(&tx, &ty)) delay(10);
        }
        delay(20);
    }
}

static void show_message(const char *title, const char *msg, uint16_t color) {
    tft.fillRect(MENU_X, MENU_Y, MENU_W, MENU_H, COL_MENU_BG);
    tft.fillRect(MENU_X - 2, MENU_Y - 2, MENU_W + 4, MENU_H + 4, COL_MENU_BORDER);
    tft.fillRect(MENU_X, MENU_Y, MENU_W, MENU_H, COL_MENU_BG);
    tft.setTextSize(2);
    tft.setTextColor(color, COL_MENU_BG);
    tft.setCursor(MENU_X + 20, MENU_Y + 20);
    tft.print(title);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, COL_MENU_BG);
    tft.setCursor(MENU_X + 20, MENU_Y + 50);
    tft.print(msg);
    display_flush();
}

static void wait_tap(void) {
    int16_t tx, ty;
    while (touch_read(&tx, &ty)) delay(10);
    delay(200);
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW, COL_MENU_BG);
    tft.setCursor(MENU_X + 60, MENU_Y + MENU_H - 20);
    tft.print("Tap to close");
    display_flush();
    while (!touch_read(&tx, &ty)) delay(20);
    while (touch_read(&tx, &ty)) delay(10);
}

/* ---- Function implementations ---- */

static void fn_ping(void) {
    show_message("PING", "Sending PING...", TFT_CYAN);

    /* Send actual PING frame */
    wichat_frame_t ping;
    memset(&ping, 0, sizeof(ping));
    ping.hdr.magic  = WICHAT_MAGIC;
    ping.hdr.msg_id = frame_gen_msg_id();
    ping.hdr.ttl    = WICHAT_DEFAULT_TTL;
    ping.hdr.type   = WICHAT_TYPE_PING;
    memcpy(ping.hdr.sender, g_mesh.local_mac, 6);
    ping.payload_len = 0;
    cyd_mesh_mark_seen(ping.hdr.msg_id);
    mesh_transmit(&ping);
    cyd_mesh_send_beacon();

}

static void fn_nodes(void) {
    tft.fillRect(MENU_X - 2, MENU_Y - 2, MENU_W + 4, MENU_H + 4, COL_MENU_BORDER);
    tft.fillRect(MENU_X, MENU_Y, MENU_W, MENU_H, COL_MENU_BG);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, COL_MENU_BG);
    tft.setCursor(MENU_X + 60, MENU_Y + 6);
    tft.print("NODE LIST");
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, COL_MENU_BG);

    if (g_mesh.node_count == 0) {
        tft.setCursor(MENU_X + 20, MENU_Y + 40);
        tft.print("No nodes discovered yet.");
    } else {
        for (int i = 0; i < g_mesh.node_count && i < 8; i++) {
            int ny = MENU_Y + 35 + i * 22;
            tft.setCursor(MENU_X + 10, ny);
            tft.printf("%d. %-10s RSSI:%d %s",
                       i + 1,
                       g_mesh.nodes[i].callsign,
                       g_mesh.nodes[i].rssi,
                       (millis() - g_mesh.nodes[i].last_seen_ms < 30000) ? "OK" : "STALE");
        }
    }
    display_flush();
    wait_tap();
}

static void fn_channel(void) {
    show_message("CHANNEL", "", TFT_CYAN);
    char buf[64];
    snprintf(buf, sizeof buf, "Current: CH %d", g_mesh.channel);
    tft.setCursor(MENU_X + 20, MENU_Y + 50);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, COL_MENU_BG);
    tft.print(buf);

    /* Draw channel buttons: - and + */
    tft.fillRect(MENU_X + 40, MENU_Y + 90, 60, 40, TFT_RED);
    tft.fillRect(MENU_X + 180, MENU_Y + 90, 60, 40, TFT_GREEN);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setCursor(MENU_X + 60, MENU_Y + 100);
    tft.print("-");
    tft.setTextColor(TFT_WHITE, TFT_GREEN);
    tft.setCursor(MENU_X + 200, MENU_Y + 100);
    tft.print("+");

    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW, COL_MENU_BG);
    tft.setCursor(MENU_X + 60, MENU_Y + 150);
    tft.print("Tap outside to close");
    display_flush();

    int16_t tx, ty;
    while (touch_read(&tx, &ty)) delay(10);
    delay(200);

    while (true) {
        if (touch_read(&tx, &ty)) {
            if (tx < MENU_X || tx > MENU_X + MENU_W || ty < MENU_Y || ty > MENU_Y + MENU_H) {
                while (touch_read(&tx, &ty)) delay(10);
                break;
            }
            int by = MENU_Y + 90;
            if (ty >= by && ty < by + 40) {
                if (tx >= MENU_X + 40 && tx < MENU_X + 100 && g_mesh.channel > 1) {
                    g_mesh.channel--;
                    esp_wifi_set_channel(g_mesh.channel, WIFI_SECOND_CHAN_NONE);
                } else if (tx >= MENU_X + 180 && tx < MENU_X + 240 && g_mesh.channel < 14) {
                    g_mesh.channel++;
                    esp_wifi_set_channel(g_mesh.channel, WIFI_SECOND_CHAN_NONE);
                }
                /* Redraw channel */
                tft.fillRect(MENU_X + 20, MENU_Y + 45, MENU_W - 40, 30, COL_MENU_BG);
                snprintf(buf, sizeof buf, "Current: CH %d", g_mesh.channel);
                tft.setCursor(MENU_X + 20, MENU_Y + 50);
                tft.setTextSize(2);
                tft.setTextColor(TFT_GREEN, COL_MENU_BG);
                tft.print(buf);
                display_flush();
            }
            while (touch_read(&tx, &ty)) delay(10);
            delay(200);
        }
        delay(20);
    }
}

static void fn_name(void) {
    show_message("NAME", "", TFT_CYAN);
    char buf[64];
    snprintf(buf, sizeof buf, "Current: %s", g_mesh.callsign);
    tft.setCursor(MENU_X + 20, MENU_Y + 50);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, COL_MENU_BG);
    tft.print(buf);
    tft.setCursor(MENU_X + 20, MENU_Y + 80);
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW, COL_MENU_BG);
    tft.print("Use keyboard to type name,");
    tft.setCursor(MENU_X + 20, MENU_Y + 95);
    tft.print("then SEND to confirm.");
    display_flush();
    wait_tap();
}

static void fn_stats(void) {
    tft.fillRect(MENU_X - 2, MENU_Y - 2, MENU_W + 4, MENU_H + 4, COL_MENU_BORDER);
    tft.fillRect(MENU_X, MENU_Y, MENU_W, MENU_H, COL_MENU_BG);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, COL_MENU_BG);
    tft.setCursor(MENU_X + 80, MENU_Y + 6);
    tft.print("STATS");
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, COL_MENU_BG);

    int y = MENU_Y + 35;
    tft.setCursor(MENU_X + 20, y); tft.printf("Callsign:  %s", g_mesh.callsign); y += 18;
    tft.setCursor(MENU_X + 20, y); tft.printf("Channel:   %d", g_mesh.channel); y += 18;
    tft.setCursor(MENU_X + 20, y); tft.printf("Nodes:     %d", g_mesh.node_count); y += 18;
    tft.setCursor(MENU_X + 20, y); tft.printf("Messages:  %d", g_mesh.msg_count); y += 18;
    tft.setCursor(MENU_X + 20, y); tft.printf("Msg count: %d", g_mesh.msg_count); y += 18;
    tft.setCursor(MENU_X + 20, y); tft.printf("Uptime:    %lu min", millis() / 60000); y += 18;
    tft.setCursor(MENU_X + 20, y);
    tft.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X",
               g_mesh.local_mac[0], g_mesh.local_mac[1], g_mesh.local_mac[2],
               g_mesh.local_mac[3], g_mesh.local_mac[4], g_mesh.local_mac[5]);

    display_flush();
    wait_tap();
}

static void fn_scan(void) {
    show_message("SCAN", "Scanning all channels...", TFT_CYAN);
    uint8_t orig_ch = g_mesh.channel;
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, COL_MENU_BG);

    for (int ch = 1; ch <= 14; ch++) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        delay(200);
        int y = MENU_Y + 50 + (ch - 1) * 12;
        if (y > MENU_Y + MENU_H - 20) break;
        tft.setCursor(MENU_X + 20, y);
        tft.printf("CH %2d: scanning...", ch);
        display_flush();
        /* Brief listen */
        uint8_t buf[512]; int8_t rssi;
        int heard = 0;
        uint32_t start = millis();
        while (millis() - start < 300) {
            if (radio_esp32_rx(buf, sizeof buf, &rssi) > 0) heard++;
        }
        tft.fillRect(MENU_X + 20, y, MENU_W - 40, 12, COL_MENU_BG);
        tft.setCursor(MENU_X + 20, y);
        tft.printf("CH %2d: %d packets", ch, heard);
        display_flush();
    }

    /* Restore original channel */
    esp_wifi_set_channel(orig_ch, WIFI_SECOND_CHAN_NONE);
    g_mesh.channel = orig_ch;
    wait_tap();
}

static void fn_clear(void) {
    g_mesh.msg_count = 0;
    g_mesh.msg_head = 0;
    g_mesh.msg_version++;
    show_message("CLEAR", "Chat history cleared.", TFT_GREEN);
    wait_tap();
}

void fn_menu_show(void) {
    draw_menu();
    int sel = wait_for_selection();

    switch (sel) {
        case 0: fn_ping(); break;
        case 1: fn_nodes(); break;
        case 2: fn_channel(); break;
        case 3: fn_name(); break;
        case 4: fn_stats(); break;
        case 5: fn_scan(); break;
        case 6: fn_clear(); break;
        default: break;  /* dismissed */
    }

    /* Redraw full UI */
    display_clear();
    display_status_bar(g_mesh.callsign, g_mesh.channel, g_mesh.node_count, millis());
    display_flush();
}
