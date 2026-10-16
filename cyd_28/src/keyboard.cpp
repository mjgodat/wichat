/* keyboard.cpp -- Touch QWERTY keyboard (2.8" CYD, LovyanGFX) */
#include "keyboard.h"
#include "display.h"
#include "config.h"
#include <Arduino.h>
#include <string.h>

#define KEY_W       30
#define KEY_H       28
#define KEY_GAP     1
#define KB_Y        (TFT_HEIGHT - KB_HEIGHT)
#define KB_BG       0x2104
#define KB_KEY      0x4208
#define KB_KEY_HL   0x6B4D
#define KB_TEXT     0xFFFF
#define KB_SPECIAL  0x001F
#define DEBOUNCE_MS 200

static const char *row0 = "QWERTYUIOP";
static const char *row1 = "ASDFGHJKL";
static const char *row2 = "ZXCVBNM";
static bool shift_active = false;
static uint32_t last_touch_ms = 0;

static int row_start_x(int n) { return (TFT_WIDTH - (n*KEY_W + (n-1)*KEY_GAP)) / 2; }

static void draw_key(int x, int y, int w, int h, const char *label, uint16_t bg) {
    tft.fillRoundRect(x, y, w, h, 4, bg);
    tft.setTextColor(KB_TEXT, bg);
    tft.setTextSize(2);
    int lx = x + (w - (int)strlen(label)*12) / 2;
    int ly = y + (h - 16) / 2;
    tft.setCursor(lx, ly);
    tft.print(label);
}

void keyboard_init(void) { shift_active = false; last_touch_ms = 0; }

void keyboard_draw(void) {
    int yb = KB_Y;
    tft.fillRect(0, yb, TFT_WIDTH, KB_HEIGHT, KB_BG);
    { int x=row_start_x(10),y=yb+KEY_GAP; for(int i=0;i<10;i++){char l[2]={row0[i],0};if(!shift_active)l[0]+=32;draw_key(x,y,KEY_W,KEY_H,l,KB_KEY);x+=KEY_W+KEY_GAP;} }
    { int x=row_start_x(9),y=yb+KEY_GAP+KEY_H+KEY_GAP; for(int i=0;i<9;i++){char l[2]={row1[i],0};if(!shift_active)l[0]+=32;draw_key(x,y,KEY_W,KEY_H,l,KB_KEY);x+=KEY_W+KEY_GAP;} }
    { int x=row_start_x(7),y=yb+KEY_GAP+2*(KEY_H+KEY_GAP); for(int i=0;i<7;i++){char l[2]={row2[i],0};if(!shift_active)l[0]+=32;draw_key(x,y,KEY_W,KEY_H,l,KB_KEY);x+=KEY_W+KEY_GAP;} }
    /* Bottom row: SHFT SPACE BKSP FN SEND */
    { int y=yb+KEY_GAP+3*(KEY_H+KEY_GAP); int sw=38,sp=TFT_WIDTH-5*sw-6*KEY_GAP,x=KEY_GAP;
      tft.setTextSize(1);
      tft.fillRoundRect(x,y,sw,KEY_H,4,shift_active?KB_KEY_HL:KB_SPECIAL); tft.setTextColor(KB_TEXT,shift_active?KB_KEY_HL:KB_SPECIAL);
      tft.setCursor(x+(sw-4*6)/2,y+(KEY_H-8)/2); tft.print("SHFT"); x+=sw+KEY_GAP;
      tft.fillRoundRect(x,y,sp,KEY_H,4,KB_KEY); tft.setTextColor(KB_TEXT,KB_KEY);
      tft.setCursor(x+(sp-3*6)/2,y+(KEY_H-8)/2); tft.print("SPC"); x+=sp+KEY_GAP;
      tft.fillRoundRect(x,y,sw,KEY_H,4,KB_SPECIAL); tft.setTextColor(KB_TEXT,KB_SPECIAL);
      tft.setCursor(x+(sw-3*6)/2,y+(KEY_H-8)/2); tft.print("DEL"); x+=sw+KEY_GAP;
      tft.fillRoundRect(x,y,sw,KEY_H,4,0x4A49); tft.setTextColor(KB_TEXT,0x4A49);
      tft.setCursor(x+(sw-2*6)/2,y+(KEY_H-8)/2); tft.print("FN"); x+=sw+KEY_GAP;
      tft.fillRoundRect(x,y,sw,KEY_H,4,KB_SPECIAL); tft.setTextColor(KB_TEXT,KB_SPECIAL);
      tft.setCursor(x+(sw-4*6)/2,y+(KEY_H-8)/2); tft.print("SEND"); }
}

kb_event_t keyboard_poll(void) {
    kb_event_t evt = {KB_NONE, 0};
    int16_t tx, ty;
    if (!tft.getTouch(&tx, &ty)) return evt;
    uint32_t now = millis();
    if (now - last_touch_ms < DEBOUNCE_MS) return evt;
    last_touch_ms = now;
    int yb = KB_Y;
    if (ty < yb) return evt;
    int row = (ty - yb - KEY_GAP) / (KEY_H + KEY_GAP);
    if (row==0) { int xs=row_start_x(10),col=(tx-xs)/(KEY_W+KEY_GAP); if(col>=0&&col<10&&tx>=xs){evt.type=KB_CHAR;evt.ch=row0[col];if(!shift_active)evt.ch+=32;} }
    else if (row==1) { int xs=row_start_x(9),col=(tx-xs)/(KEY_W+KEY_GAP); if(col>=0&&col<9&&tx>=xs){evt.type=KB_CHAR;evt.ch=row1[col];if(!shift_active)evt.ch+=32;} }
    else if (row==2) { int xs=row_start_x(7),col=(tx-xs)/(KEY_W+KEY_GAP); if(col>=0&&col<7&&tx>=xs){evt.type=KB_CHAR;evt.ch=row2[col];if(!shift_active)evt.ch+=32;} }
    else if (row==3) { int sw=38,sp=TFT_WIDTH-5*sw-6*KEY_GAP,x=KEY_GAP;
        if(tx>=x&&tx<x+sw){shift_active=!shift_active;evt.type=KB_SHIFT;keyboard_draw();}x+=sw+KEY_GAP;
        if(tx>=x&&tx<x+sp){evt.type=KB_CHAR;evt.ch=' ';}x+=sp+KEY_GAP;
        if(tx>=x&&tx<x+sw){evt.type=KB_BACKSPACE;}x+=sw+KEY_GAP;
        if(tx>=x&&tx<x+sw){evt.type=KB_FUNC;}x+=sw+KEY_GAP;
        if(tx>=x&&tx<x+sw){evt.type=KB_ENTER;} }
    return evt;
}
