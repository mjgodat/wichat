/* display.h -- Freenove ST77922 display (TFT_eSprite + QSPI flush) */
#ifndef WICHAT_DISPLAY_H
#define WICHAT_DISPLAY_H

#include <TFT_eSPI.h>
#include "ST77922.h"
#include "config.h"

extern TFT_eSPI tft_qspi;
extern TFT_eSprite tft;
extern ST77922 tft_st77922;

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

#ifdef __cplusplus
}
#endif

#endif
