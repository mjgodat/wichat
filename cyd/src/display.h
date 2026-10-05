/* display.h -- LovyanGFX driver for CYD ST7796 3.5" display */
#ifndef WICHAT_DISPLAY_H
#define WICHAT_DISPLAY_H

#include <LovyanGFX.hpp>

/* ---- LovyanGFX device class for this CYD ---- */
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796  _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;
    lgfx::Touch_GT911   _touch;

public:
    LGFX(void) {
        /* SPI bus config */
        {
            auto cfg = _bus.config();
            cfg.spi_host   = VSPI_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 80000000;
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
            cfg.panel_width      = 320;
            cfg.panel_height     = 480;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 1;  /* landscape */
            cfg.readable         = true;
            cfg.invert           = false;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;
            _panel.config(cfg);
        }

        /* Backlight config */
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 27;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        /* Touch config (GT911 capacitive) */
        {
            auto cfg = _touch.config();
            cfg.x_min      = 0;
            cfg.x_max      = 479;
            cfg.y_min      = 0;
            cfg.y_max      = 319;
            cfg.pin_int    = -1;    /* R25 unpopulated */
            cfg.pin_rst    = 25;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port   = 1;
            cfg.i2c_addr   = 0x5D;
            cfg.pin_sda    = 33;
            cfg.pin_scl    = 32;
            cfg.freq       = 400000;
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
 * sender = callsign or hex MAC, text = message body. */
void display_message(int line, const char *sender, const char *text);

/* Clear the chat area (below status bar, above keyboard). */
void display_clear_chat(void);

/* Draw the input line at bottom of chat area showing current typed text. */
void display_input(const char *text, int cursor_pos);

#ifdef __cplusplus
}
#endif

#endif /* WICHAT_DISPLAY_H */
