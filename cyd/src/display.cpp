/* display.cpp -- Freenove ST77922 display via TFT_eSprite + QSPI
 * Uses Freenove's official ST77922 driver for QSPI panel access.
 * TFT_eSprite as full-screen PSRAM framebuffer for text/graphics.
 * Flush sprite to panel with ST77922::Fill_Colors(). */
#include "display.h"
#include "config.h"
#include <Arduino.h>

TFT_eSPI tft_qspi = TFT_eSPI();
TFT_eSprite tft = TFT_eSprite(&tft_qspi);
ST77922 tft_st77922 = ST77922();

#define COL_BG         TFT_BLACK
#define COL_STATUS_BG  0x18E3
#define COL_STATUS_FG  TFT_GREEN
#define COL_SENDER     TFT_CYAN
#define COL_TEXT       TFT_WHITE
#define COL_INPUT_BG   0x10A2
#define COL_INPUT_FG   TFT_YELLOW
#define COL_SYSTEM     TFT_RED
#define COL_CURSOR     TFT_WHITE

#define STATUS_H  24
#define LINE_H    36  /* 2 lines of size-2 text */
#define INPUT_H   20
#define CHAT_AREA_Y STATUS_H
#define CHAT_AREA_H (TFT_HEIGHT - STATUS_H - KB_HEIGHT - INPUT_H)
#define INPUT_Y     (TFT_HEIGHT - KB_HEIGHT - INPUT_H)

void display_init(void) {
    tft_st77922.Set_Rotation(1);  /* landscape 480×320 */
    tft.createSprite(tft_st77922.Get_Width(), tft_st77922.Get_Height());
    tft.setSwapBytes(true);
    tft.fillSprite(COL_BG);
    tft.setTextSize(1);
    tft.setTextWrap(false);
    display_flush();
    Serial.println("[display] ST77922 + TFT_eSprite ready");
}

void display_flush(void) {
    tft_st77922.Fill_Colors(0, 0, tft_st77922.Get_Width(), tft_st77922.Get_Height(),
                            (uint16_t *)tft.getPointer());
}

void display_clear(void) {
    tft.fillSprite(COL_BG);
}

void display_status_bar(const char *callsign, uint8_t channel,
                        int node_count, uint32_t uptime_ms) {
    tft.fillRect(0, 0, TFT_WIDTH, STATUS_H, COL_STATUS_BG);
    tft.setTextColor(COL_STATUS_FG, COL_STATUS_BG);
    tft.setTextSize(2);
    tft.setCursor(4, 4);
    tft.printf("%.12s", callsign);

    char ch_buf[16];
    snprintf(ch_buf, sizeof ch_buf, "CH:%d", channel);
    tft.setCursor(TFT_WIDTH / 2 - (int)strlen(ch_buf) * 6, 4);
    tft.print(ch_buf);

    char rb[32]; uint32_t us = uptime_ms / 1000;
    snprintf(rb, sizeof rb, "N:%d %lu:%02lu", node_count,
             (unsigned long)(us / 60), (unsigned long)(us % 60));
    tft.setCursor(TFT_WIDTH - (int)strlen(rb) * 12 - 4, 4);
    tft.print(rb);
}

void display_message(int line, const char *sender, const char *text, uint8_t status) {
    if (line < 0 || line >= CHAT_LINES) return;
    int y = CHAT_AREA_Y + line * LINE_H;
    tft.fillRect(0, y, TFT_WIDTH, LINE_H, COL_BG);
    tft.setTextSize(2);

    if (sender && sender[0]) {
        bool is_system = (sender[0] == '[');
        tft.setTextColor(is_system ? COL_SYSTEM : COL_SENDER, COL_BG);
        tft.setCursor(2, y + 2);
        tft.printf("%.8s: ", sender);
    }
    if (text && text[0]) {
        tft.setTextColor(COL_TEXT, COL_BG);
        int cpl = (TFT_WIDTH - tft.getCursorX()) / 12;
        int len = strlen(text);
        if (len <= cpl) {
            tft.print(text);
        } else {
            /* Word-wrap: find last space before limit */
            int brk = cpl;
            for (int i = cpl; i > 0; i--) { if (text[i] == ' ') { brk = i; break; } }
            char line1[41]; strncpy(line1, text, brk); line1[brk] = '\0';
            tft.print(line1);
            /* Skip the space at break point */
            const char *rest = text + brk;
            while (*rest == ' ') rest++;
            if (*rest) {
                tft.setCursor(2, y + 18);
                int line2_max = TFT_WIDTH / 12;
                int rlen = strlen(rest);
                if (rlen > line2_max) rlen = line2_max;
                char line2[41]; strncpy(line2, rest, rlen); line2[rlen] = '\0';
                tft.print(line2);
            }
        }
    }

    /* Delivery status at right edge */
    if (status == 1) { tft.setTextColor(TFT_YELLOW, COL_BG); tft.setCursor(TFT_WIDTH - 14, y + 2); tft.print("?"); }
    else if (status == 2) { tft.setTextColor(TFT_GREEN, COL_BG); tft.setCursor(TFT_WIDTH - 14, y + 2); tft.print("Y"); }
    else if (status == 3) { tft.setTextColor(TFT_RED, COL_BG); tft.setCursor(TFT_WIDTH - 14, y + 2); tft.print("X"); }
}

void display_clear_chat(void) {
    tft.fillRect(0, CHAT_AREA_Y, TFT_WIDTH, CHAT_AREA_H, COL_BG);
}

void display_input(const char *text, int cursor_pos) {
    int y = INPUT_Y;
    tft.fillRect(0, y, TFT_WIDTH, INPUT_H, COL_INPUT_BG);
    tft.setTextColor(COL_INPUT_FG, COL_INPUT_BG);
    tft.setTextSize(2);
    tft.setCursor(4, y + 2);
    tft.print("> ");
    if (text && text[0] != '\0') tft.printf("%.36s", text);
    int cx = 4 + 24 + cursor_pos * 12;
    if (cx < TFT_WIDTH - 6) tft.fillRect(cx, y + 2, 2, INPUT_H - 4, COL_CURSOR);
}
