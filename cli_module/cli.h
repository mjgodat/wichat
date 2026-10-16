/* cli.h -- Serial command interface for remote operation & test automation
 * Drop into SquatchWatch custom build. Processes commands from Serial.
 *
 * Usage: call cli_init() in setup(), cli_poll() in loop().
 *
 * Commands:
 *   tap <x> <y>           Simulate touch at screen coordinates
 *   hold <x> <y> <ms>     Simulate touch and hold
 *   release                Release simulated touch
 *   swipe <x1> <y1> <x2> <y2> <ms>  Simulate swipe gesture
 *   screen                 Report current screen ID and dimensions
 *   screenshot             Dump screen pixel data (hex) for verification
 *   brightness <0-255>     Set backlight
 *   reboot                 Restart ESP32
 *   heap                   Report free heap / PSRAM
 *   uptime                 Report uptime
 *   wifi scan              Start WiFi scan
 *   wifi stop              Stop WiFi scan
 *   wifi list              List discovered APs
 *   ble scan               Start BLE scan
 *   ble stop               Stop BLE scan
 *   ble list               List discovered BLE devices
 *   chat send <msg>        Send WiChat message
 *   chat ping              Send mesh PING
 *   chat nodes             List mesh nodes
 *   chat channel <n>       Set radio channel
 *   chat name <name>       Set callsign
 *   chat status            Show mesh stats
 *   hid run                Run current HID payload
 *   hid type <text>        Type text via HID
 *   hid key <keycode>      Send single HID keycode
 *   time <epoch>           Set clock
 *   zone <name>            Set timezone
 *   sleep <ms>             Delay (for scripting)
 *   echo <text>            Echo back (for script sync)
 *   batch                  Start batch mode (commands until "end")
 *   end                    End batch mode, execute all
 *   help                   List commands
 *
 * All responses prefixed with [OK], [ERR], or [DATA] for parsing.
 */
#ifndef WICHAT_CLI_H
#define WICHAT_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

void cli_init(void);
void cli_poll(void);  /* Call from loop() — non-blocking */

/* Register feature callbacks. Set to NULL if not available. */
typedef struct {
    /* Touch simulation */
    void (*inject_touch)(int16_t x, int16_t y);
    void (*release_touch)(void);

    /* Screen info */
    int  (*get_screen_id)(void);
    int  (*get_screen_width)(void);
    int  (*get_screen_height)(void);

    /* WiFi */
    void (*wifi_scan_start)(void);
    void (*wifi_scan_stop)(void);
    void (*wifi_list)(void);  /* prints to Serial */

    /* BLE */
    void (*ble_scan_start)(void);
    void (*ble_scan_stop)(void);
    void (*ble_list)(void);

    /* WiChat */
    void (*chat_send)(const char *msg);
    void (*chat_ping)(void);
    void (*chat_nodes)(void);
    void (*chat_set_channel)(int ch);
    void (*chat_set_name)(const char *name);
    void (*chat_status)(void);

    /* HID */
    void (*hid_run)(void);
    void (*hid_type)(const char *text);
    void (*hid_key)(uint8_t keycode);

    /* System */
    void (*set_brightness)(int level);
    void (*reboot)(void);
} cli_callbacks_t;

void cli_set_callbacks(const cli_callbacks_t *cb);

#ifdef __cplusplus
}
#endif

#endif /* WICHAT_CLI_H */
