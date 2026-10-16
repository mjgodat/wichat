/* display.h -- ST77922 QSPI display driver for Freenove ESP32-S3 3.5" */
#ifndef WICHAT_DISPLAY_H
#define WICHAT_DISPLAY_H

#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

void display_init(void);
void display_clear(void);
void display_flush(void);
void display_status_bar(const char *callsign, uint8_t channel,
                        int node_count, uint32_t uptime_ms);
void display_message(int line, const char *sender, const char *text, uint8_t status);
void display_clear_chat(void);
void display_input(const char *text, int cursor_pos);

/* Direct panel drawing (landscape coords, handles rotation internally) */
void display_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_setCursor(int16_t x, int16_t y);
void display_setTextColor(uint16_t fg, uint16_t bg);
void display_setTextSize(uint8_t s);
void display_print(const char *str);
void display_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* WICHAT_DISPLAY_H */
