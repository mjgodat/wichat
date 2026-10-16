/* keyboard.cpp -- Touch QWERTY keyboard for CYD (ST77922 direct draw) */
#include "keyboard.h"
#include "display.h"
#include "touch_cal.h"
#include "config.h"
#include <Arduino.h>
#include <string.h>

#define KEY_W       44
#define KEY_H       40
#define KEY_GAP     2
#define KB_Y        (TFT_HEIGHT - KB_HEIGHT)

#define KB_BG       0x2104
#define KB_KEY      0x4208
#define KB_KEY_HL   0x6B4D
#define KB_TEXT     0xFFFF
#define KB_SPECIAL  0x001F

static const char *row0 = "QWERTYUIOP";
static const char *row1 = "ASDFGHJKL";
static const char *row2 = "ZXCVBNM";

static bool shift_active = false;
static uint32_t last_touch_ms = 0;
#define DEBOUNCE_MS 200

static int row_start_x(int n) {
    return (TFT_WIDTH - (n * KEY_W + (n - 1) * KEY_GAP)) / 2;
}

static void draw_key(int x, int y, int w, int h, const char *label, uint16_t bg) {
    display_fillRect(x, y, w, h, bg);
    display_setTextColor(KB_TEXT, bg);
    display_setTextSize(2);
    int lx = x + (w - (int)strlen(label) * 12) / 2;
    int ly = y + (h - 16) / 2;
    display_setCursor(lx, ly);
    display_print(label);
}

void keyboard_init(void) { shift_active = false; last_touch_ms = 0; }

void keyboard_draw(void) {
    int y_base = KB_Y;
    display_fillRect(0, y_base, TFT_WIDTH, KB_HEIGHT, KB_BG);

    { int x = row_start_x(10), y = y_base + KEY_GAP;
      for (int i = 0; i < 10; i++) {
          char l[2] = { row0[i], 0 }; if (!shift_active) l[0] += 32;
          draw_key(x, y, KEY_W, KEY_H, l, KB_KEY); x += KEY_W + KEY_GAP; } }

    { int x = row_start_x(9), y = y_base + KEY_GAP + KEY_H + KEY_GAP;
      for (int i = 0; i < 9; i++) {
          char l[2] = { row1[i], 0 }; if (!shift_active) l[0] += 32;
          draw_key(x, y, KEY_W, KEY_H, l, KB_KEY); x += KEY_W + KEY_GAP; } }

    { int x = row_start_x(7), y = y_base + KEY_GAP + 2 * (KEY_H + KEY_GAP);
      for (int i = 0; i < 7; i++) {
          char l[2] = { row2[i], 0 }; if (!shift_active) l[0] += 32;
          draw_key(x, y, KEY_W, KEY_H, l, KB_KEY); x += KEY_W + KEY_GAP; } }

    { int y = y_base + KEY_GAP + 3 * (KEY_H + KEY_GAP);
      int sw = 66, sp = TFT_WIDTH - 4 * sw - 5 * KEY_GAP, x = KEY_GAP;
      draw_key(x, y, sw, KEY_H, "SHFT", shift_active ? KB_KEY_HL : KB_SPECIAL); x += sw + KEY_GAP;
      draw_key(x, y, sp, KEY_H, "SPACE", KB_KEY); x += sp + KEY_GAP;
      draw_key(x, y, sw, KEY_H, "BKSP", KB_SPECIAL); x += sw + KEY_GAP;
      draw_key(x, y, sw, KEY_H, "SEND", KB_SPECIAL); }
}

kb_event_t keyboard_poll(void) {
    kb_event_t evt = { KB_NONE, 0 };
    int16_t tx, ty;
    if (!touch_read(&tx, &ty)) return evt;
    uint32_t now = millis();
    if (now - last_touch_ms < DEBOUNCE_MS) return evt;
    last_touch_ms = now;

    int y_base = KB_Y;
    if (ty < y_base) return evt;
    int row = (ty - y_base - KEY_GAP) / (KEY_H + KEY_GAP);

    if (row == 0) {
        int xs = row_start_x(10), col = (tx - xs) / (KEY_W + KEY_GAP);
        if (col >= 0 && col < 10 && tx >= xs) { evt.type = KB_CHAR; evt.ch = row0[col]; if (!shift_active) evt.ch += 32; }
    } else if (row == 1) {
        int xs = row_start_x(9), col = (tx - xs) / (KEY_W + KEY_GAP);
        if (col >= 0 && col < 9 && tx >= xs) { evt.type = KB_CHAR; evt.ch = row1[col]; if (!shift_active) evt.ch += 32; }
    } else if (row == 2) {
        int xs = row_start_x(7), col = (tx - xs) / (KEY_W + KEY_GAP);
        if (col >= 0 && col < 7 && tx >= xs) { evt.type = KB_CHAR; evt.ch = row2[col]; if (!shift_active) evt.ch += 32; }
    } else if (row == 3) {
        int sw = 66, sp = TFT_WIDTH - 4 * sw - 5 * KEY_GAP, x = KEY_GAP;
        if (tx >= x && tx < x + sw) { shift_active = !shift_active; evt.type = KB_SHIFT; keyboard_draw(); }
        x += sw + KEY_GAP;
        if (tx >= x && tx < x + sp) { evt.type = KB_CHAR; evt.ch = ' '; }
        x += sp + KEY_GAP;
        if (tx >= x && tx < x + sw) { evt.type = KB_BACKSPACE; }
        x += sw + KEY_GAP;
        if (tx >= x && tx < x + sw) { evt.type = KB_ENTER; }
    }
    return evt;
}
