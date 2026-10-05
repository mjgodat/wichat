/* frame.h — WiChat wire format definitions */
#ifndef WICHAT_FRAME_H
#define WICHAT_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"

/* ---- Frame type constants ---- */
#define WICHAT_TYPE_CHAT      0x01
#define WICHAT_TYPE_BEACON    0x02
#define WICHAT_TYPE_ACK       0x03
#define WICHAT_TYPE_PING      0x04
#define WICHAT_TYPE_PONG      0x05
#define WICHAT_TYPE_FEC_CHUNK 0x06

/* ---- Packed on-air structs ---- */
#pragma pack(push, 1)

/* WiChat header — 14 bytes */
typedef struct {
    uint16_t magic;         /* WICHAT_MAGIC = 0x5743 */
    uint32_t msg_id;        /* random 32-bit message ID */
    uint8_t  ttl;           /* hop count remaining */
    uint8_t  type;          /* WICHAT_TYPE_* */
    uint8_t  sender[6];     /* sender MAC address */
} wichat_hdr_t;

/* Beacon payload */
typedef struct {
    char     callsign[16];
    uint8_t  flags;
    uint32_t uptime_sec;
} wichat_beacon_t;

/* FEC chunk payload */
typedef struct {
    uint32_t parent_msg_id;
    uint8_t  seq;
    uint8_t  total;
    uint8_t  data[230];
} wichat_fec_chunk_t;

/* 802.11 data frame header — 24 bytes */
typedef struct {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t  da[6];         /* destination address */
    uint8_t  sa[6];         /* source address */
    uint8_t  bssid[6];      /* BSS identifier */
    uint16_t seq_ctrl;
} dot11_hdr_t;

#pragma pack(pop)

/* ---- Full frame (NOT packed, for in-memory use) ---- */
typedef struct {
    wichat_hdr_t hdr;
    uint8_t      payload[WICHAT_MAX_PAYLOAD];
    size_t       payload_len;
} wichat_frame_t;

/* ---- Functions ---- */

/*
 * Encode a wichat_frame_t into a raw 802.11 buffer.
 * Returns total bytes written, or -1 on error.
 */
int frame_encode(const wichat_frame_t *frame, const uint8_t *local_mac,
                 uint8_t *out_buf, size_t out_max);

/*
 * Decode a raw 802.11 buffer into a wichat_frame_t.
 * Returns 0 on success, -1 if not a valid WiChat frame.
 */
int frame_decode(const uint8_t *raw, size_t raw_len, wichat_frame_t *frame);

/*
 * Generate a random 32-bit message ID.
 */
uint32_t frame_gen_msg_id(void);

#endif /* WICHAT_FRAME_H */
