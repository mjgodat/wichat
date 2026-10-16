/* display.h -- LovyanGFX driver for CYD ILI9341 2.8" display */
#ifndef WICHAT_DISPLAY_H
#define WICHAT_DISPLAY_H

#include <LovyanGFX.hpp>

/* ---- LovyanGFX device class for 2.8" CYD (ESP32-2432S028) ---- */
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789   _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Light_PWM      _light;
    lgfx::Touch_XPT2046  _touch;

public:
    LGFX(void) {
        /* SPI bus config (display) */
        {
            auto cfg = _bus.config();
            cfg.spi_host   = VSPI_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;   /* ILI9341 max 40MHz */
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = 14;
            cfg.pin_mosi   = 13;
            cfg.pin_miso   = 12;
            cfg.pin_dc     = 2;
            _bus.config(cfg);
        }
        _panel.setBus(&_bus);

        /* Panel config */
        {
            auto cfg = _panel.config();
            cfg.pin_cs           = 15;
            cfg.pin_rst          = -1;
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.readable         = true;
            cfg.invert           = false;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;
            _panel.config(cfg);
        }

        /* Backlight config -- pin 21 on 2.8" CYD */
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 21;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        /* Touch config (XPT2046 resistive, separate SPI bus) */
        {
            auto cfg = _touch.config();
            cfg.x_min      = 0;
            cfg.x_max      = 239;
            cfg.y_min      = 0;
            cfg.y_max      = 319;
            cfg.pin_int    = 36;
            cfg.pin_cs     = 33;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.spi_host   = HSPI_HOST;
            cfg.freq       = 1000000;
            cfg.pin_sclk   = 25;
            cfg.pin_mosi   = 32;
            cfg.pin_miso   = 39;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }

        setPanel(&_panel);
    }
};

/* Global display instance */
extern LGFX tft;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize display: clear screen, set backlight, draw status bar. */
void display_init(void);

/* Clear entire screen to black. */
void display_clear(void);

/* Draw status bar at top: callsign, channel, node count.
 * Call with current mesh state. */
void display_status_bar(const char *callsign, uint8_t channel,
                        int node_count, uint32_t uptime_ms);

/* Draw a single chat message at line index (0..CHAT_LINES-1).
 * sender = callsign or hex MAC, text = message body, status = delivery status. */
void display_message(int line, const char *sender, const char *text, uint8_t status);

/* Clear the chat area (below status bar, above keyboard). */
void display_clear_chat(void);

/* Draw the input line at bottom of chat area showing current typed text. */
void display_input(const char *text, int cursor_pos);

#ifdef __cplusplus
}
#endif

#endif /* WICHAT_DISPLAY_H */
