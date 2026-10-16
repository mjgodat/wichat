/* touch_cal.h -- ST77922 TDDI touch driver (I2C @ 0x55) */
#ifndef WICHAT_TOUCH_CAL_H
#define WICHAT_TOUCH_CAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void touch_init(void);
bool touch_read(int16_t *x, int16_t *y);

static inline bool touch_cal_load(void) { return true; }
static inline void touch_cal_run(void) { }
static inline void touch_test_run(void) { }

#ifdef __cplusplus
}
#endif

#endif
