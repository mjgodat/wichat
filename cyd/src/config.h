/* config.h -- CYD WiChat compile-time defaults (Freenove ESP32-S3 3.5" FNK0104N)
 * ST77922 over QSPI, touch integrated in display chip. */
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

/* Display -- 3.5" ST77922 QSPI, landscape after rotation */
#define WICHAT_MAX_MESSAGES   64
#define TFT_WIDTH             480
#define TFT_HEIGHT            320

/* ST77922 QSPI pins (FSPI IO_MUX) */
#define LCD_SCLK  12
#define LCD_D0    11
#define LCD_D1    13
#define LCD_D2    14
#define LCD_D3    9
#define LCD_CS    10
#define LCD_BL    41

/* Touch I2C (ST77922 TDDI — touch in display chip) */
#define TOUCH_SDA   38
#define TOUCH_SCL   39
#define TOUCH_RST   48
#define TOUCH_IRQ   47
#define TOUCH_ADDR  0x55

/* Chat area layout */
#define CHAT_LINES      3
#define KB_HEIGHT       160
#define CHAT_Y          24

#define MESH_MAX_NODES  16

#endif /* WICHAT_CONFIG_H */
