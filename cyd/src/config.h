/* config.h -- CYD WiChat compile-time defaults
 * Same protocol constants as PC client, smaller buffers for ESP32. */
#ifndef WICHAT_CONFIG_H
#define WICHAT_CONFIG_H

#define WICHAT_VERSION       "1.0.0-cyd"
#define WICHAT_DEFAULT_CH    1
#define WICHAT_DEFAULT_NAME  "CYD"
#define WICHAT_MAX_NAME_LEN  16
#define WICHAT_MAX_PAYLOAD   236
#define WICHAT_FRAME_MAX     250
#define WICHAT_DEDUP_SIZE    256        /* smaller than PC (1024) */
#define WICHAT_DEFAULT_TTL   7
#define WICHAT_BEACON_INTERVAL_MS  30000
#define WICHAT_NODE_STALE_MS       120000
#define WICHAT_NODE_EXPIRE_MS      300000

/* 802.11 frame constants -- MUST match PC client exactly */
#define WICHAT_BSSID  {0x57, 0x43, 0x57, 0x43, 0x57, 0x43}
#define WICHAT_MAGIC  0x5743  /* "WC" */

/* Frame type constants -- MUST match PC client exactly */
#define WICHAT_TYPE_CHAT      0x01
#define WICHAT_TYPE_BEACON    0x02
#define WICHAT_TYPE_ACK       0x03
#define WICHAT_TYPE_PING      0x04
#define WICHAT_TYPE_PONG      0x05
#define WICHAT_TYPE_FEC_CHUNK 0x06

/* GUI / display */
#define WICHAT_MAX_MESSAGES   64       /* smaller than PC (256) */
#define TFT_WIDTH             480
#define TFT_HEIGHT            320

/* GT911 touch I2C */
#define TOUCH_SDA       33
#define TOUCH_SCL       32
#define TOUCH_INT       -1    /* R25 unpopulated on this board */
#define TOUCH_RST       25
#define TOUCH_I2C_PORT  1

/* Chat area layout */
#define CHAT_LINES      8
#define KB_HEIGHT       160
#define CHAT_Y          24     /* below status bar */

/* Mesh node tracking */
#define MESH_MAX_NODES  16     /* smaller than PC (64) */

#endif /* WICHAT_CONFIG_H */
