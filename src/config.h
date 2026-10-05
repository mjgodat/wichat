/* config.h — WiChat compile-time defaults */
#ifndef WICHAT_CONFIG_H
#define WICHAT_CONFIG_H

#define WICHAT_VERSION       "1.0.0"
#define WICHAT_DEFAULT_CH    1
#define WICHAT_DEFAULT_NAME  "WiChat"
#define WICHAT_MAX_NAME_LEN  16
#define WICHAT_MAX_PAYLOAD   236
#define WICHAT_FRAME_MAX     250
#define WICHAT_DEDUP_SIZE    1024
#define WICHAT_DEFAULT_TTL   7
#define WICHAT_BEACON_INTERVAL_MS  30000
#define WICHAT_NODE_STALE_MS       120000
#define WICHAT_NODE_EXPIRE_MS      300000
#define WICHAT_FEC_RATE      3       /* 1/3 convolutional code */
#define WICHAT_FEC_REPEAT    3       /* repeat each chunk N times */
#define WICHAT_FEC_TIMEOUT_MS 10000  /* reassembly timeout */

/* RTL8811AU USB IDs */
#define RTL8811AU_VID  0x0bda
#define RTL8811AU_PID  0xa811

/* 802.11 frame constants */
#define WICHAT_BSSID  {0x57, 0x43, 0x57, 0x43, 0x57, 0x43}
#define WICHAT_MAGIC  0x5743  /* "WC" */

/* GUI */
#define WICHAT_WINDOW_W  600
#define WICHAT_WINDOW_H  450
#define WICHAT_MAX_MESSAGES 256

#endif
