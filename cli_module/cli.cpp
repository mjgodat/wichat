/* cli.cpp -- Serial command interface for remote operation & test automation */
#include "cli.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

#define CLI_BUF_SIZE 256
#define CLI_BATCH_MAX 32

static char cli_buf[CLI_BUF_SIZE];
static int cli_pos = 0;
static const cli_callbacks_t *cb = nullptr;

/* Batch mode */
static bool batch_mode = false;
static char *batch_cmds[CLI_BATCH_MAX];
static int batch_count = 0;

/* Simulated touch state */
static bool sim_touch_active = false;
static int16_t sim_touch_x = 0, sim_touch_y = 0;

static void cli_exec(const char *line);

void cli_init(void) {
    cli_pos = 0;
    batch_mode = false;
    batch_count = 0;
    Serial.println("[OK] CLI ready. Type 'help' for commands.");
}

void cli_set_callbacks(const cli_callbacks_t *callbacks) {
    cb = callbacks;
}

void cli_poll(void) {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cli_pos > 0) {
                cli_buf[cli_pos] = '\0';

                if (batch_mode) {
                    if (strcmp(cli_buf, "end") == 0) {
                        /* Execute batch */
                        batch_mode = false;
                        Serial.printf("[OK] Executing %d commands\n", batch_count);
                        for (int i = 0; i < batch_count; i++) {
                            cli_exec(batch_cmds[i]);
                            free(batch_cmds[i]);
                        }
                        batch_count = 0;
                        Serial.println("[OK] Batch complete");
                    } else if (batch_count < CLI_BATCH_MAX) {
                        batch_cmds[batch_count++] = strdup(cli_buf);
                    } else {
                        Serial.println("[ERR] Batch full");
                    }
                } else {
                    cli_exec(cli_buf);
                }
                cli_pos = 0;
            }
        } else if (cli_pos < CLI_BUF_SIZE - 1) {
            cli_buf[cli_pos++] = c;
        }
    }

    /* Maintain simulated touch if active */
    if (sim_touch_active && cb && cb->inject_touch) {
        cb->inject_touch(sim_touch_x, sim_touch_y);
    }
}

/* Parse helpers */
static const char *skip_word(const char *s) {
    while (*s && *s != ' ') s++;
    while (*s == ' ') s++;
    return s;
}

static int parse_int(const char *s) { return atoi(s); }

static void cmd_help(void) {
    Serial.println("[DATA] === CLI Commands ===");
    Serial.println("[DATA] tap <x> <y>           Simulate touch");
    Serial.println("[DATA] hold <x> <y> <ms>     Touch and hold");
    Serial.println("[DATA] release               Release touch");
    Serial.println("[DATA] swipe <x1> <y1> <x2> <y2> <ms>");
    Serial.println("[DATA] screen                Screen info");
    Serial.println("[DATA] brightness <0-255>    Backlight");
    Serial.println("[DATA] heap                  Memory info");
    Serial.println("[DATA] uptime                Uptime");
    Serial.println("[DATA] reboot                Restart");
    Serial.println("[DATA] wifi scan|stop|list   WiFi tools");
    Serial.println("[DATA] ble scan|stop|list    BLE tools");
    Serial.println("[DATA] chat send|ping|nodes|channel|name|status");
    Serial.println("[DATA] hid run|type|key      HID injection");
    Serial.println("[DATA] sleep <ms>            Delay");
    Serial.println("[DATA] echo <text>           Echo back");
    Serial.println("[DATA] batch / end           Batch mode");
    Serial.println("[DATA] help                  This help");
}

static void cmd_tap(const char *args) {
    int x = parse_int(args);
    const char *p = skip_word(args);
    int y = parse_int(p);
    if (cb && cb->inject_touch) {
        cb->inject_touch((int16_t)x, (int16_t)y);
        delay(100);
        if (cb->release_touch) cb->release_touch();
        Serial.printf("[OK] tap %d %d\n", x, y);
    } else {
        Serial.println("[ERR] Touch not available");
    }
}

static void cmd_hold(const char *args) {
    int x = parse_int(args);
    const char *p = skip_word(args);
    int y = parse_int(p);
    p = skip_word(p);
    int ms = parse_int(p);
    if (cb && cb->inject_touch) {
        sim_touch_active = true;
        sim_touch_x = (int16_t)x;
        sim_touch_y = (int16_t)y;
        cb->inject_touch(sim_touch_x, sim_touch_y);
        if (ms > 0) {
            delay(ms);
            sim_touch_active = false;
            if (cb->release_touch) cb->release_touch();
        }
        Serial.printf("[OK] hold %d %d %dms\n", x, y, ms);
    } else {
        Serial.println("[ERR] Touch not available");
    }
}

static void cmd_release(void) {
    sim_touch_active = false;
    if (cb && cb->release_touch) cb->release_touch();
    Serial.println("[OK] release");
}

static void cmd_swipe(const char *args) {
    int x1 = parse_int(args);
    const char *p = skip_word(args);
    int y1 = parse_int(p); p = skip_word(p);
    int x2 = parse_int(p); p = skip_word(p);
    int y2 = parse_int(p); p = skip_word(p);
    int ms = parse_int(p);
    if (ms < 50) ms = 200;

    if (cb && cb->inject_touch) {
        int steps = ms / 10;
        if (steps < 5) steps = 5;
        for (int i = 0; i <= steps; i++) {
            int16_t x = (int16_t)(x1 + (x2 - x1) * i / steps);
            int16_t y = (int16_t)(y1 + (y2 - y1) * i / steps);
            cb->inject_touch(x, y);
            delay(10);
        }
        if (cb->release_touch) cb->release_touch();
        Serial.printf("[OK] swipe %d,%d -> %d,%d in %dms\n", x1, y1, x2, y2, ms);
    } else {
        Serial.println("[ERR] Touch not available");
    }
}

static void cmd_screen(void) {
    int id = (cb && cb->get_screen_id) ? cb->get_screen_id() : -1;
    int w = (cb && cb->get_screen_width) ? cb->get_screen_width() : -1;
    int h = (cb && cb->get_screen_height) ? cb->get_screen_height() : -1;
    Serial.printf("[DATA] screen_id=%d width=%d height=%d\n", id, w, h);
}

static void cmd_heap(void) {
    Serial.printf("[DATA] free_heap=%d largest_block=%d",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#ifdef BOARD_HAS_PSRAM
    Serial.printf(" psram_free=%d psram_total=%d", ESP.getFreePsram(), ESP.getPsramSize());
#endif
    Serial.println();
}

static void cmd_uptime(void) {
    uint32_t ms = millis();
    Serial.printf("[DATA] uptime_ms=%lu uptime=%lu:%02lu:%02lu\n",
                  (unsigned long)ms,
                  (unsigned long)(ms / 3600000),
                  (unsigned long)((ms / 60000) % 60),
                  (unsigned long)((ms / 1000) % 60));
}

static void cmd_wifi(const char *args) {
    if (strncmp(args, "scan", 4) == 0) {
        if (cb && cb->wifi_scan_start) { cb->wifi_scan_start(); Serial.println("[OK] wifi scan started"); }
        else Serial.println("[ERR] wifi scan not available");
    } else if (strncmp(args, "stop", 4) == 0) {
        if (cb && cb->wifi_scan_stop) { cb->wifi_scan_stop(); Serial.println("[OK] wifi scan stopped"); }
        else Serial.println("[ERR] wifi stop not available");
    } else if (strncmp(args, "list", 4) == 0) {
        if (cb && cb->wifi_list) { cb->wifi_list(); }
        else Serial.println("[ERR] wifi list not available");
    } else {
        Serial.println("[ERR] wifi: scan|stop|list");
    }
}

static void cmd_ble(const char *args) {
    if (strncmp(args, "scan", 4) == 0) {
        if (cb && cb->ble_scan_start) { cb->ble_scan_start(); Serial.println("[OK] ble scan started"); }
        else Serial.println("[ERR] ble scan not available");
    } else if (strncmp(args, "stop", 4) == 0) {
        if (cb && cb->ble_scan_stop) { cb->ble_scan_stop(); Serial.println("[OK] ble scan stopped"); }
        else Serial.println("[ERR] ble stop not available");
    } else if (strncmp(args, "list", 4) == 0) {
        if (cb && cb->ble_list) { cb->ble_list(); }
        else Serial.println("[ERR] ble list not available");
    } else {
        Serial.println("[ERR] ble: scan|stop|list");
    }
}

static void cmd_chat(const char *args) {
    if (strncmp(args, "send ", 5) == 0) {
        if (cb && cb->chat_send) { cb->chat_send(args + 5); Serial.printf("[OK] chat sent: %s\n", args + 5); }
        else Serial.println("[ERR] chat send not available");
    } else if (strncmp(args, "ping", 4) == 0) {
        if (cb && cb->chat_ping) { cb->chat_ping(); Serial.println("[OK] ping sent"); }
        else Serial.println("[ERR] chat ping not available");
    } else if (strncmp(args, "nodes", 5) == 0) {
        if (cb && cb->chat_nodes) { cb->chat_nodes(); }
        else Serial.println("[ERR] chat nodes not available");
    } else if (strncmp(args, "channel ", 8) == 0) {
        int ch = parse_int(args + 8);
        if (cb && cb->chat_set_channel) { cb->chat_set_channel(ch); Serial.printf("[OK] channel %d\n", ch); }
        else Serial.println("[ERR] chat channel not available");
    } else if (strncmp(args, "name ", 5) == 0) {
        if (cb && cb->chat_set_name) { cb->chat_set_name(args + 5); Serial.printf("[OK] name '%s'\n", args + 5); }
        else Serial.println("[ERR] chat name not available");
    } else if (strncmp(args, "status", 6) == 0) {
        if (cb && cb->chat_status) { cb->chat_status(); }
        else Serial.println("[ERR] chat status not available");
    } else {
        Serial.println("[ERR] chat: send|ping|nodes|channel|name|status");
    }
}

static void cmd_hid(const char *args) {
    if (strncmp(args, "run", 3) == 0) {
        if (cb && cb->hid_run) { cb->hid_run(); Serial.println("[OK] hid run"); }
        else Serial.println("[ERR] hid run not available");
    } else if (strncmp(args, "type ", 5) == 0) {
        if (cb && cb->hid_type) { cb->hid_type(args + 5); Serial.printf("[OK] hid typed: %s\n", args + 5); }
        else Serial.println("[ERR] hid type not available");
    } else if (strncmp(args, "key ", 4) == 0) {
        uint8_t k = (uint8_t)parse_int(args + 4);
        if (cb && cb->hid_key) { cb->hid_key(k); Serial.printf("[OK] hid key %d\n", k); }
        else Serial.println("[ERR] hid key not available");
    } else {
        Serial.println("[ERR] hid: run|type|key");
    }
}

static void cli_exec(const char *line) {
    /* Skip leading whitespace */
    while (*line == ' ') line++;
    if (*line == '\0' || *line == '#') return;  /* empty or comment */

    if (strncmp(line, "help", 4) == 0) { cmd_help(); }
    else if (strncmp(line, "tap ", 4) == 0) { cmd_tap(line + 4); }
    else if (strncmp(line, "hold ", 5) == 0) { cmd_hold(line + 5); }
    else if (strcmp(line, "release") == 0) { cmd_release(); }
    else if (strncmp(line, "swipe ", 6) == 0) { cmd_swipe(line + 6); }
    else if (strcmp(line, "screen") == 0) { cmd_screen(); }
    else if (strncmp(line, "brightness ", 11) == 0) {
        int b = parse_int(line + 11);
        if (cb && cb->set_brightness) { cb->set_brightness(b); Serial.printf("[OK] brightness %d\n", b); }
        else Serial.println("[ERR] brightness not available");
    }
    else if (strcmp(line, "heap") == 0) { cmd_heap(); }
    else if (strcmp(line, "uptime") == 0) { cmd_uptime(); }
    else if (strcmp(line, "reboot") == 0) {
        Serial.println("[OK] rebooting...");
        delay(100);
        if (cb && cb->reboot) cb->reboot();
        else ESP.restart();
    }
    else if (strncmp(line, "wifi ", 5) == 0) { cmd_wifi(line + 5); }
    else if (strncmp(line, "ble ", 4) == 0) { cmd_ble(line + 4); }
    else if (strncmp(line, "chat ", 5) == 0) { cmd_chat(line + 5); }
    else if (strncmp(line, "hid ", 4) == 0) { cmd_hid(line + 4); }
    else if (strncmp(line, "sleep ", 6) == 0) {
        int ms = parse_int(line + 6);
        delay(ms);
        Serial.printf("[OK] slept %dms\n", ms);
    }
    else if (strncmp(line, "echo ", 5) == 0) { Serial.printf("[DATA] %s\n", line + 5); }
    else if (strcmp(line, "batch") == 0) {
        batch_mode = true;
        batch_count = 0;
        Serial.println("[OK] Batch mode. Send commands, then 'end'.");
    }
    else {
        Serial.printf("[ERR] Unknown: '%s'. Type 'help'.\n", line);
    }
}
