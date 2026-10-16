/* touch_cal.h -- Touch calibration for CYD GT911
 * 4-point crosshair calibration stored in NVS.
 * Hold BOOT button during startup to force recalibration. */
#ifndef WICHAT_TOUCH_CAL_H
#define WICHAT_TOUCH_CAL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Check NVS for stored calibration. Returns true if valid cal exists. */
bool touch_cal_load(void);

/* Run interactive 4-point calibration (draws on screen, blocks until done).
 * Stores result in NVS. Applies to LovyanGFX panel. */
void touch_cal_run(void);

/* Run a touch test screen: draws colored dots where touches land,
 * prints raw + mapped coords to Serial. Press BOOT button to exit. */
void touch_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* WICHAT_TOUCH_CAL_H */
