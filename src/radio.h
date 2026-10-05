#ifndef WICHAT_RADIO_H
#define WICHAT_RADIO_H
#include <stdint.h>
#include <stddef.h>

typedef enum {
    PHY_RATE_1M = 0,
    PHY_RATE_2M,
    PHY_RATE_5M5,
    PHY_RATE_11M
} phy_rate_t;

typedef struct radio radio_t;

struct radio {
    int  (*init)(radio_t *r);
    int  (*set_channel)(radio_t *r, uint8_t ch);
    int  (*set_rate)(radio_t *r, phy_rate_t rate);
    int  (*set_tx_power)(radio_t *r, uint8_t dbm);
    int  (*tx_frame)(radio_t *r, const uint8_t *data, size_t len);
    int  (*rx_frame)(radio_t *r, uint8_t *buf, size_t max_len, int timeout_ms);
    void (*shutdown)(radio_t *r);
    void *priv;
};

#endif
