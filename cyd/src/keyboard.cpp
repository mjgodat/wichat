/* keyboard.cpp -- Touch QWERTY keyboard for CYD
 * 3 rows of letters + bottom row (SHIFT, SPACE, BKSP, SEND).
 * Key size 44x40px, debounce 200ms. */
#include "keyboard.h"
#include "display.h"
#include "config.h"
#include <Arduino.h>
#include <string.h>

/* Key geometry */
#define KEY_W       44
#define KEY_H       40
#define KEY_GAP     2
#define KB_Y        (TFT_HEIGHT - KB_HEIGHT)

/* Colors */
#define KB_BG       0x2104  /* dark gray */
#define KB_KEY      0x4208  /* medium gray */
#define KB_KEY_HL   0x6B4D  /* lighter gray for press */
#define KB_TEXT     0xFFFF  /* white */
#define KB_SPECIAL  0x001F  /* blue for special keys */

/* Keyboard layout */
static const char *row0 = "QWERTYUIOP";    /* 10 keys */
static const char *row1 = "ASDFGHJKL";     /* 9 keys */
static const char *row2 = "ZXCVBNM";       /* 7 keys */
/* Row 3: SHIFT(66px), SPACE(176px), BKSP(66px), SEND(66px) */

static bool shift_active = false;
static uint32_t last_touch_ms = 0;

/* Debounce time in ms */
#define DEBOUNCE_MS 200

/* Calculate starting X to center a row of n keys */
static int row_start_x(int n)
{
    int total_w = n * KEY_W + (n - 1) * KEY_GAP;
    return (TFT_WIDTH - total_w) / 2;
}

/* Draw a single key */
static void draw_key(int x, int y, int w, int h, const char *label, uint16_t bg)
{
    tft.fillRoundRect(x, y, w, h, 4, bg);
    tft.setTextColor(KB_TEXT, bg);
    tft.setTextSize(1);
    int lx = x + (w - (int)strlen(label) * 6) / 2;
    int ly = y + (h - 8) / 2;
    tft.setCursor(lx, ly);
    tft.print(label);
}

void keyboard_init(void)
{
    shift_active = false;
    last_touch_ms = 0;
}

void keyboard_draw(void)
{
    int y_base = KB_Y;

    /* Background */
    tft.fillRect(0, y_base, TFT_WIDTH, KB_HEIGHT, KB_BG);

    /* Row 0: QWERTYUIOP */
    {
        int x = row_start_x(10);
        int y = y_base + KEY_GAP;
        for (int i = 0; i < 10; i++) {
            char label[2] = { row0[i], '\0' };
            if (!shift_active) label[0] += 32;  /* lowercase */
            draw_key(x, y, KEY_W, KEY_H, label, KB_KEY);
            x += KEY_W + KEY_GAP;
        }
    }

    /* Row 1: ASDFGHJKL */
    {
        int x = row_start_x(9);
        int y = y_base + KEY_GAP + (KEY_H + KEY_GAP);
        for (int i = 0; i < 9; i++) {
            char label[2] = { row1[i], '\0' };
            if (!shift_active) label[0] += 32;
            draw_key(x, y, KEY_W, KEY_H, label, KB_KEY);
            x += KEY_W + KEY_GAP;
        }
    }

    /* Row 2: ZXCVBNM */
    {
        int x = row_start_x(7);
        int y = y_base + KEY_GAP + 2 * (KEY_H + KEY_GAP);
        for (int i = 0; i < 7; i++) {
            char label[2] = { row2[i], '\0' };
            if (!shift_active) label[0] += 32;
            draw_key(x, y, KEY_W, KEY_H, label, KB_KEY);
            x += KEY_W + KEY_GAP;
        }
    }

    /* Row 3: SHIFT, SPACE, BKSP, SEND */
    {
        int y = y_base + KEY_GAP + 3 * (KEY_H + KEY_GAP);
        int special_w = 66;
        int space_w = TFT_WIDTH - 4 * special_w - 5 * KEY_GAP;
        int x = KEY_GAP;

        draw_key(x, y, special_w, KEY_H, "SHFT", shift_active ? KB_KEY_HL : KB_SPECIAL);
        x += special_w + KEY_GAP;

        draw_key(x, y, space_w, KEY_H, "SPACE", KB_KEY);
        x += space_w + KEY_GAP;

        draw_key(x, y, special_w, KEY_H, "BKSP", KB_SPECIAL);
        x += special_w + KEY_GAP;

        draw_key(x, y, special_w, KEY_H, "SEND", KB_SPECIAL);
    }
}

kb_event_t keyboard_poll(void)
{
    kb_event_t evt = { KB_NONE, 0 };

    int16_t tx, ty;
    if (!tft.getTouch(&tx, &ty))
        return evt;

    /* Debounce */
    uint32_t now = millis();
    if (now - last_touch_ms < DEBOUNCE_MS)
        return evt;
    last_touch_ms = now;

    /* Check if touch is in keyboard area */
    int y_base = KB_Y;
    if (ty < y_base)
        return evt;

    int row = (ty - y_base - KEY_GAP) / (KEY_H + KEY_GAP);

    if (row == 0) {
        /* QWERTYUIOP */
        int x_start = row_start_x(10);
        int col = (tx - x_start) / (KEY_W + KEY_GAP);
        if (col >= 0 && col < 10 && tx >= x_start) {
            char c = row0[col];
            if (!shift_active) c += 32;
            evt.type = KB_CHAR;
            evt.ch = c;
        }
    }
    else if (row == 1) {
        /* ASDFGHJKL */
        int x_start = row_start_x(9);
        int col = (tx - x_start) / (KEY_W + KEY_GAP);
        if (col >= 0 && col < 9 && tx >= x_start) {
            char c = row1[col];
            if (!shift_active) c += 32;
            evt.type = KB_CHAR;
            evt.ch = c;
        }
    }
    else if (row == 2) {
        /* ZXCVBNM */
        int x_start = row_start_x(7);
        int col = (tx - x_start) / (KEY_W + KEY_GAP);
        if (col >= 0 && col < 7 && tx >= x_start) {
            char c = row2[col];
            if (!shift_active) c += 32;
            evt.type = KB_CHAR;
            evt.ch = c;
        }
    }
    else if (row == 3) {
        /* Special row: SHIFT, SPACE, BKSP, SEND */
        int special_w = 66;
        int space_w = TFT_WIDTH - 4 * special_w - 5 * KEY_GAP;
        int x = KEY_GAP;

        if (tx >= x && tx < x + special_w) {
            /* SHIFT */
            shift_active = !shift_active;
            evt.type = KB_SHIFT;
            keyboard_draw();  /* redraw to show shift state */
        }
        x += special_w + KEY_GAP;

        if (tx >= x && tx < x + space_w) {
            /* SPACE */
            evt.type = KB_CHAR;
            evt.ch = ' ';
        }
        x += space_w + KEY_GAP;

        if (tx >= x && tx < x + special_w) {
            /* BKSP */
            evt.type = KB_BACKSPACE;
        }
        x += special_w + KEY_GAP;

        if (tx >= x && tx < x + special_w) {
            /* SEND */
            evt.type = KB_ENTER;
        }
    }

    return evt;
}
