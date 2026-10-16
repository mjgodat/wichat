/* touch_cal.cpp -- ST77922 touch via Freenove ST77922_Touch */
#include "touch_cal.h"
#include "config.h"
#include "ST77922_Touch.h"
#include <Arduino.h>

static ST77922_TOUCH touch;
static bool s_up = false;

void touch_init(void) {
    touch.init();
    touch.Set_Rotation(1);  /* landscape */
    s_up = true;
    Serial.println("[touch] ST77922_Touch initialized");
}

bool touch_read(int16_t *x, int16_t *y) {
    if (!s_up) return false;
    if (touch.Get_Touch()) {
        if (touch.touch.x[0] > 0 || touch.touch.y[0] > 0) {
            *x = touch.touch.x[0];
            *y = touch.touch.y[0];
            return true;
        }
    }
    return false;
}
