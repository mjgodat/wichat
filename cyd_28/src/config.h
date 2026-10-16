/* config.h -- CYD WiChat (2.8" ESP32 ST7789 + XPT2046) */
#ifndef WICHAT_CONFIG_H
#define WICHAT_CONFIG_H

#define WICHAT_VERSION       "1.0.0-cyd"
#define WICHAT_DEFAULT_CH    1
#define WICHAT_DEFAULT_NAME  "CYD"
#define WICHAT_MAX_NAME_LEN  16
#define WICHAT_MAX_PAYLOAD   236
#define WICHAT_FRAME_MAX     250
#define WICHAT_DEDUP_SIZE    256
#define WICHAT_DEFAULT_TTL   7
#define WICHAT_BEACON_INTERVAL_MS  2000
#define WICHAT_FEC_TIMEOUT_MS     10000
#define WICHAT_NODE_STALE_MS       120000
#define WICHAT_NODE_EXPIRE_MS      300000

#define WICHAT_BSSID  {0x57, 0x43, 0x57, 0x43, 0x57, 0x43}
#define WICHAT_MAGIC  0x5743

#define WICHAT_TYPE_CHAT      0x01
#define WICHAT_TYPE_BEACON    0x02
#define WICHAT_TYPE_ACK       0x03
#define WICHAT_TYPE_PING      0x04
#define WICHAT_TYPE_PONG      0x05
#define WICHAT_TYPE_FEC_CHUNK 0x06

/* GUI / display -- 2.8" ST7789 landscape */
#define WICHAT_MAX_MESSAGES   64
#define TFT_WIDTH             320
#define TFT_HEIGHT            240

/* XPT2046 touch SPI pins */
#define TOUCH_CS        33
#define TOUCH_CLK       25
#define TOUCH_MOSI      32
#define TOUCH_MISO      39
#define TOUCH_IRQ       36

/* Chat area layout */
#define CHAT_LINES      4
#define KB_HEIGHT       120
#define CHAT_Y          20

/* Mesh node tracking */
#define MESH_MAX_NODES  16

#endif
