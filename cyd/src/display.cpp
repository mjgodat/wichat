/* display.cpp -- LovyanGFX driver for CYD ST7796 3.5" display */
#include "display.h"
#include "config.h"
#include <stdio.h>

/* Global display instance */
LGFX tft;

/* Colors */
#define COL_BG         0x0000  /* black */
#define COL_STATUS_BG  0x18E3  /* dark gray */
#define COL_STATUS_FG  0x07E0  /* green */
#define COL_SENDER     0x07FF  /* cyan */
#define COL_TEXT       0xFFFF  /* white */
#define COL_INPUT_BG   0x10A2  /* dark gray */
#define COL_INPUT_FG   0xFFE0  /* yellow */
#define COL_SYSTEM     0xF800  /* red for [system] */
#define COL_CURSOR     0xFFFF  /* white */

/* Layout constants */
#define STATUS_H       24
#define LINE_H         16
#define INPUT_H        20
#define CHAT_AREA_Y    STATUS_H
#define CHAT_AREA_H    (TFT_HEIGHT - STATUS_H - KB_HEIGHT - INPUT_H)
#define INPUT_Y        (TFT_HEIGHT - KB_HEIGHT - INPUT_H)
#define FONT_SIZE      1

void display_init(void)
{
    tft.init();
    tft.setRotation(1);  /* landscape: 480 x 320 */
    tft.setBrightness(200);
    tft.fillScreen(COL_BG);
    tft.setTextSize(FONT_SIZE);
    tft.setTextWrap(false);
}

void display_clear(void)
{
    tft.fillScreen(COL_BG);
}

void display_status_bar(const char *callsign, uint8_t channel,
                        int node_count, uint32_t uptime_ms)
{
    tft.fillRect(0, 0, TFT_WIDTH, STATUS_H, COL_STATUS_BG);
    tft.setTextColor(COL_STATUS_FG, COL_STATUS_BG);
    tft.setTextSize(1);

    /* Left: callsign */
    tft.setCursor(4, 4);
    tft.printf("%.16s", callsign);

    /* Center: channel */
    char ch_buf[16];
    snprintf(ch_buf, sizeof(ch_buf), "CH:%d", channel);
    int16_t cx = (TFT_WIDTH / 2) - (int16_t)(strlen(ch_buf) * 3);
    tft.setCursor(cx, 4);
    tft.print(ch_buf);

    /* Right: node count + uptime */
    char right_buf[32];
    uint32_t up_sec = uptime_ms / 1000;
    snprintf(right_buf, sizeof(right_buf), "N:%d  %lu:%02lu",
             node_count, (unsigned long)(up_sec / 60), (unsigned long)(up_sec % 60));
    int16_t rx = TFT_WIDTH - (int16_t)(strlen(right_buf) * 6) - 4;
    tft.setCursor(rx, 4);
    tft.print(right_buf);
}

void display_message(int line, const char *sender, const char *text)
{
    if (line < 0 || line >= CHAT_LINES)
        return;

    int y = CHAT_AREA_Y + line * LINE_H;
    tft.fillRect(0, y, TFT_WIDTH, LINE_H, COL_BG);

    /* Sender in cyan (or red for [system]) */
    bool is_system = (sender[0] == '[');
    tft.setTextColor(is_system ? COL_SYSTEM : COL_SENDER, COL_BG);
    tft.setTextSize(1);
    tft.setCursor(2, y + 2);
    tft.printf("%.10s: ", sender);

    /* Message text in white */
    tft.setTextColor(COL_TEXT, COL_BG);
    /* Truncate to fit screen: ~480px / 6px per char = 80 chars total, minus sender */
    tft.printf("%.60s", text);
}

void display_clear_chat(void)
{
    tft.fillRect(0, CHAT_AREA_Y, TFT_WIDTH, CHAT_AREA_H, COL_BG);
}

void display_input(const char *text, int cursor_pos)
{
    int y = INPUT_Y;
    tft.fillRect(0, y, TFT_WIDTH, INPUT_H, COL_INPUT_BG);
    tft.setTextColor(COL_INPUT_FG, COL_INPUT_BG);
    tft.setTextSize(1);
    tft.setCursor(4, y + 4);
    tft.print("> ");

    /* Show text, max ~74 chars */
    if (text && text[0] != '\0') {
        tft.printf("%.74s", text);
    }

    /* Blinking cursor (just a solid bar here) */
    int cx = 4 + 12 + cursor_pos * 6;  /* 12 = "> " width */
    if (cx < TFT_WIDTH - 6) {
        tft.fillRect(cx, y + 2, 2, INPUT_H - 4, COL_CURSOR);
    }
}
