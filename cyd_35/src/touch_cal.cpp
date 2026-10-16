/* touch_cal.cpp -- ST77922 TDDI touch (I2C @ 0x55, 16-bit registers)
 * Based on Freenove ST77922_Touch and SquatchWatch-CYD. */
#include "touch_cal.h"
#include "config.h"
#include <Wire.h>
#include <Arduino.h>

#define REG_FW_VER    0x0000
#define REG_STATUS    0x0001
#define REG_MAX_TOUCH 0x0009
#define REG_TOUCH_INFO 0x0010
#define REG_POINT0    0x0014

static bool s_up = false;
static uint8_t s_maxPoints = 0;

static bool readRegs(uint16_t reg, uint8_t *out, size_t n) {
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint16_t)TOUCH_ADDR, (size_t)n) != n) return false;
    for (size_t i = 0; i < n; i++) out[i] = (uint8_t)Wire.read();
    return true;
}

void touch_init(void) {
    Wire.begin(TOUCH_SDA, TOUCH_SCL, 100000);
    pinMode(TOUCH_IRQ, INPUT);

    /* Reset touch controller */
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, HIGH);
    delay(10);
    digitalWrite(TOUCH_RST, LOW);
    delay(100);
    digitalWrite(TOUCH_RST, HIGH);
    delay(100);

    /* Wait for status to clear */
    uint8_t st = 0xFF;
    for (int i = 0; i < 50 && readRegs(REG_STATUS, &st, 1) && (st & 0x0F); i++) delay(10);

    uint8_t fw[2] = {0, 0};
    s_up = readRegs(REG_FW_VER, fw, 2) && readRegs(REG_MAX_TOUCH, &s_maxPoints, 1) && s_maxPoints > 0;
    if (s_maxPoints > 10) s_maxPoints = 10;

    /* Wait for touch to settle (~1s after boot) */
    if (s_up) {
        uint8_t p[4];
        for (int i = 0; i < 150 && !readRegs(REG_POINT0, p, sizeof p); i++) delay(20);
    }

    Serial.printf("[touch] ST77922 TDDI: %s (fw %u.%u, %u points)\n",
                  s_up ? "OK" : "FAILED", fw[0], fw[1], s_maxPoints);
}

bool touch_read(int16_t *x, int16_t *y) {
    if (!s_up) return false;

    static uint32_t s_nextTry = 0;
    static bool s_down = false;
    static uint16_t s_x = 0, s_y = 0;
    static uint32_t s_seenAt = 0;
    uint32_t now = millis();

    if ((int32_t)(now - s_nextTry) < 0) {
        if (s_down) { *x = s_x; *y = s_y; }
        return s_down;
    }

    uint8_t info = 0;
    if (!readRegs(REG_TOUCH_INFO, &info, 1)) { s_nextTry = now + 50; return false; }

    if (info & 0x08) {
        uint8_t all[70];
        size_t n = (size_t)7 * (s_maxPoints ? s_maxPoints : 1);
        if (n > sizeof all) n = sizeof all;
        if (!readRegs(REG_POINT0, all, n)) { s_nextTry = now + 50; return false; }

        s_seenAt = now;
        bool down = (all[0] & 0x80) != 0;
        if (down) {
            uint16_t raw_x = ((all[0] & 0x3F) << 8) | all[1];
            uint16_t raw_y = ((all[2] & 0x3F) << 8) | all[3];
            /* Portrait to landscape (rotation 1): lx = raw_y, ly = 319 - raw_x */
            s_x = raw_y;
            s_y = 319 - raw_x;
        }
        s_down = down && s_x < 480 && s_y < 320;
    } else if (s_down && now - s_seenAt > 80) {
        s_down = false;
    }

    *x = s_x;
    *y = s_y;
    return s_down;
}
