/* touch_cal.cpp -- Touch calibration for CYD XPT2046
 *
 * 4-point crosshair calibration.  Raw GT911 coordinates at four known
 * display positions are recorded, then an affine transform is computed
 * and applied to LovyanGFX via setCalibrateAffine().
 *
 * The affine maps raw touch -> portrait panel coordinates.
 * LovyanGFX convertRawXY then applies display rotation (rotation 1)
 * to get final landscape coordinates.
 *
 * Calibration is persisted in NVS (ESP32 Preferences) so it survives
 * reboot.  Hold BOOT button at startup to force recalibration. */

#include "touch_cal.h"
#include "display.h"
#include "config.h"
#include <Preferences.h>
#include <Arduino.h>
#include <math.h>

/* NVS namespace and keys */
static const char *NVS_NS    = "tcal";
static const char *NVS_VALID = "valid";
static const char *NVS_AFF   = "affine";   /* float[6] */

/* Crosshair target positions in DISPLAY (landscape) coordinates.
 * Inset by 40px from edges so the user is not pressing at the bezel. */
static const int16_t TARGET_X[4] = {  30, 290,  30, 290 };
static const int16_t TARGET_Y[4] = {  30,  30, 210, 210 };

/* Samples averaged per target point */
#define SAMPLES_PER_TARGET 5

/* Colors */
#define COL_CAL_BG    0x0000
#define COL_CROSSHAIR 0xF800   /* red */
#define COL_TEXT       0xFFFF
#define COL_DONE       0x07E0  /* green */
#define COL_DOT        0x07FF  /* cyan */

/* --------------------------------------------------------------- */
/* Draw a crosshair at (cx, cy)                                    */
/* --------------------------------------------------------------- */
static void draw_crosshair(int16_t cx, int16_t cy, uint16_t color)
{
    tft.drawLine(cx - 15, cy, cx + 15, cy, color);
    tft.drawLine(cx, cy - 15, cx, cy + 15, color);
    tft.drawCircle(cx, cy, 8, color);
}

/* --------------------------------------------------------------- */
/* Collect averaged raw touch at the current crosshair position.   */
/* Waits for finger lift, then collects SAMPLES_PER_TARGET samples.*/
/* Returns false on 15-second timeout.                             */
/* --------------------------------------------------------------- */
static bool collect_raw(int32_t *out_x, int32_t *out_y)
{
    lgfx::touch_point_t tp;

    /* Wait for any existing touch to be released */
    while (tft.getTouchRaw(&tp)) { delay(10); }
    delay(200);

    int32_t sum_x = 0, sum_y = 0;
    int collected = 0;
    uint32_t start = millis();

    while (collected < SAMPLES_PER_TARGET) {
        if (tft.getTouchRaw(&tp)) {
            sum_x += tp.x;
            sum_y += tp.y;
            collected++;
            Serial.printf("[CAL] sample %d: raw_x=%d raw_y=%d\n",
                          collected, tp.x, tp.y);
            delay(60);
        }
        delay(10);

        if (millis() - start > 15000) {
            Serial.println("[CAL] Timeout waiting for touch!");
            return false;
        }
    }

    *out_x = sum_x / SAMPLES_PER_TARGET;
    *out_y = sum_y / SAMPLES_PER_TARGET;

    /* Wait for finger release */
    while (tft.getTouchRaw(&tp)) { delay(10); }
    delay(200);

    return true;
}

/* --------------------------------------------------------------- */
/* Solve 3x3 linear system and compute affine[6] from 4 points.   */
/* Maps: portrait_coord = affine * [raw_x, raw_y, 1]              */
/* --------------------------------------------------------------- */
static bool solve_affine(const int32_t raw_x[4], const int32_t raw_y[4],
                         const float port_x[4], const float port_y[4],
                         float affine[6])
{
    float mat[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
    float vect[6]   = {0,0,0,0,0,0};

    for (int i = 0; i < 4; i++) {
        float px = (float)raw_x[i];
        float py = (float)raw_y[i];
        float tx = port_x[i];
        float ty = port_y[i];

        mat[0][0] += px * px;
        mat[0][1] += px * py;
        mat[1][0] += px * py;
        mat[0][2] += px;
        mat[2][0] += px;
        mat[1][1] += py * py;
        mat[1][2] += py;
        mat[2][1] += py;
        mat[2][2] += 1.0f;

        vect[0] += px * tx;
        vect[1] += py * tx;
        vect[2] +=      tx;
        vect[3] += px * ty;
        vect[4] += py * ty;
        vect[5] +=      ty;
    }

    /* Gauss-Jordan elimination */
    for (int k = 0; k < 3; k++) {
        float t = mat[k][k];
        if (fabsf(t) < 1e-10f) {
            Serial.println("[CAL] Singular matrix -- bad calibration data.");
            return false;
        }
        for (int i = 0; i < 3; i++) mat[k][i] /= t;
        mat[k][k] = 1.0f / t;
        for (int j = 0; j < 3; j++) {
            if (j == k) continue;
            float u = mat[j][k];
            for (int i = 0; i < 3; i++) {
                if (i != k) mat[j][i] -= mat[k][i] * u;
                else        mat[j][i] = -u / t;
            }
        }
    }

    affine[0] = mat[0][0]*vect[0] + mat[0][1]*vect[1] + mat[0][2]*vect[2];
    affine[1] = mat[1][0]*vect[0] + mat[1][1]*vect[1] + mat[1][2]*vect[2];
    affine[2] = mat[2][0]*vect[0] + mat[2][1]*vect[1] + mat[2][2]*vect[2];
    affine[3] = mat[0][0]*vect[3] + mat[0][1]*vect[4] + mat[0][2]*vect[5];
    affine[4] = mat[1][0]*vect[3] + mat[1][1]*vect[4] + mat[1][2]*vect[5];
    affine[5] = mat[2][0]*vect[3] + mat[2][1]*vect[4] + mat[2][2]*vect[5];

    return true;
}

/* --------------------------------------------------------------- */
/* Public: load calibration from NVS                                */
/* --------------------------------------------------------------- */
bool touch_cal_load(void)
{
    Preferences prefs;
    prefs.begin(NVS_NS, true);  /* read-only */

    if (!prefs.getBool(NVS_VALID, false)) {
        prefs.end();
        Serial.println("[CAL] No stored calibration found.");
        return false;
    }

    float affine[6];
    size_t len = prefs.getBytes(NVS_AFF, affine, sizeof(affine));
    prefs.end();

    if (len != sizeof(affine)) {
        Serial.println("[CAL] Stored calibration data corrupt.");
        return false;
    }

    tft.panel()->setCalibrateAffine(affine);
    Serial.printf("[CAL] Loaded affine from NVS: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f]\n",
                  affine[0], affine[1], affine[2],
                  affine[3], affine[4], affine[5]);
    return true;
}

/* --------------------------------------------------------------- */
/* Public: run interactive 4-point calibration                     */
/* --------------------------------------------------------------- */
void touch_cal_run(void)
{
    Serial.println("[CAL] === Starting 4-point touch calibration ===");

    int32_t raw_x[4], raw_y[4];

    for (int i = 0; i < 4; i++) {
        tft.fillScreen(COL_CAL_BG);
        tft.setTextColor(COL_TEXT, COL_CAL_BG);
        tft.setTextSize(2);

        char msg[32];
        snprintf(msg, sizeof(msg), "Touch point %d of 4", i + 1);
        int16_t tw = (int16_t)strlen(msg) * 12;  /* textSize 2 = 12px/char */
        tft.setCursor((TFT_WIDTH - tw) / 2, 140);
        tft.print(msg);

        tft.setTextSize(1);
        tft.setCursor(120, 170);
        tft.print("Tap the crosshair precisely");

        draw_crosshair(TARGET_X[i], TARGET_Y[i], COL_CROSSHAIR);

        if (!collect_raw(&raw_x[i], &raw_y[i])) {
            tft.fillScreen(COL_CAL_BG);
            tft.setTextColor(0xF800, COL_CAL_BG);
            tft.setTextSize(2);
            tft.setCursor(80, 150);
            tft.print("Calibration timeout!");
            delay(2000);
            return;
        }

        Serial.printf("[CAL] Point %d: display(%d,%d) -> raw(%ld,%ld)\n",
                      i, TARGET_X[i], TARGET_Y[i], raw_x[i], raw_y[i]);

        draw_crosshair(TARGET_X[i], TARGET_Y[i], COL_DONE);
        delay(400);
    }

    /* Convert landscape display targets to portrait panel coordinates.
     *
     * ILI9341: 240w x 320h native (portrait).
     * With rotation 1: landscape 320w x 240h.
     * _height after rotation = panel_width = 240.
     *
     * convertRawXY rotation 1:
     *   lx = py
     *   ly = (239) - px
     *
     * Inverse (display landscape -> portrait for affine target):
     *   py = lx
     *   px = 239 - ly
     */
    float port_x[4], port_y[4];
    for (int i = 0; i < 4; i++) {
        port_x[i] = 239.0f - (float)TARGET_Y[i];   /* px = 239 - ly */
        port_y[i] = (float)TARGET_X[i];              /* py = lx */
    }

    float affine[6];
    if (!solve_affine(raw_x, raw_y, port_x, port_y, affine)) {
        tft.fillScreen(COL_CAL_BG);
        tft.setTextColor(0xF800, COL_CAL_BG);
        tft.setTextSize(2);
        tft.setCursor(80, 150);
        tft.print("Calibration FAILED!");
        delay(2000);
        return;
    }

    Serial.printf("[CAL] Affine: [%.4f, %.4f, %.4f, %.4f, %.4f, %.4f]\n",
                  affine[0], affine[1], affine[2],
                  affine[3], affine[4], affine[5]);

    /* Apply to panel */
    tft.panel()->setCalibrateAffine(affine);

    /* Persist in NVS */
    {
        Preferences prefs;
        prefs.begin(NVS_NS, false);
        prefs.putBytes(NVS_AFF, affine, sizeof(affine));
        prefs.putBool(NVS_VALID, true);
        prefs.end();
    }

    /* Success + verification screen */
    tft.fillScreen(COL_CAL_BG);
    tft.setTextColor(COL_DONE, COL_CAL_BG);
    tft.setTextSize(2);
    tft.setCursor(80, 110);
    tft.print("Calibration saved!");
    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT, COL_CAL_BG);
    tft.setCursor(60, 150);
    tft.print("Touch screen to verify (5 sec)...");

    /* Draw reference grid */
    for (int x = 0; x < TFT_WIDTH; x += 80) {
        tft.drawFastVLine(x, 0, TFT_HEIGHT, 0x2104);
    }
    for (int y = 0; y < TFT_HEIGHT; y += 80) {
        tft.drawFastHLine(0, y, TFT_WIDTH, 0x2104);
    }

    uint32_t verify_end = millis() + 5000;
    while (millis() < verify_end) {
        int16_t tx, ty;
        if (tft.getTouch(&tx, &ty)) {
            tft.fillCircle(tx, ty, 3, COL_DOT);
            Serial.printf("[CAL] Verify: display(%d, %d)\n", tx, ty);
        }
        delay(20);
    }

    Serial.println("[CAL] Calibration complete.");
}

/* --------------------------------------------------------------- */
/* Public: touch test screen                                       */
/* Shows raw + mapped coords. Press BOOT (GPIO 0) to exit.         */
/* --------------------------------------------------------------- */
void touch_test_run(void)
{
    Serial.println("[TEST] Touch test mode. Press BOOT button to exit.");

    tft.fillScreen(COL_CAL_BG);
    tft.setTextColor(COL_TEXT, COL_CAL_BG);
    tft.setTextSize(1);
    tft.setCursor(10, 10);
    tft.print("TOUCH TEST - press BOOT to exit");

    /* Draw grid for reference */
    for (int x = 0; x < TFT_WIDTH; x += 48) {
        tft.drawFastVLine(x, 24, TFT_HEIGHT - 24, 0x2104);
    }
    for (int y = 24; y < TFT_HEIGHT; y += 40) {
        tft.drawFastHLine(0, y, TFT_WIDTH, 0x2104);
    }

    /* Label edges */
    tft.setCursor(2, 24);   tft.print("0");
    tft.setCursor(296, 24); tft.print("319");
    tft.setCursor(2, 228);  tft.print("239");

    pinMode(0, INPUT_PULLUP);  /* BOOT button */

    while (digitalRead(0) == HIGH) {
        lgfx::touch_point_t raw_tp;
        if (tft.getTouchRaw(&raw_tp)) {
            /* Get mapped coords too */
            int16_t mx, my;
            tft.getTouch(&mx, &my);

            /* Draw dot at mapped position */
            tft.fillCircle(mx, my, 3, COL_DOT);

            /* Print coords at top */
            tft.fillRect(0, 0, TFT_WIDTH, 20, COL_CAL_BG);
            tft.setTextColor(COL_TEXT, COL_CAL_BG);
            tft.setCursor(10, 4);
            tft.printf("RAW: x=%4d y=%4d  MAPPED: x=%4d y=%4d",
                       raw_tp.x, raw_tp.y, mx, my);

            Serial.printf("[TEST] raw(%d,%d) -> mapped(%d,%d)\n",
                          raw_tp.x, raw_tp.y, mx, my);
        }
        delay(20);
    }

    /* Wait for button release */
    while (digitalRead(0) == LOW) { delay(10); }
    delay(100);

    Serial.println("[TEST] Exiting touch test.");
}
