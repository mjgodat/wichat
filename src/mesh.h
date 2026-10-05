/* mesh.h — WiChat mesh engine: flood relay, dedup, beacons, range test */
#ifndef WICHAT_MESH_H
#define WICHAT_MESH_H

#include "frame.h"
#include "radio.h"

/* ---- Node tracking ---- */
typedef struct {
    uint8_t  mac[6];
    char     callsign[WICHAT_MAX_NAME_LEN];  /* 16 */
    uint32_t last_seen;      /* time_t truncated */
    int8_t   rssi;
    uint8_t  active;         /* 1 = active, 0 = slot empty */
} mesh_node_t;

#define MESH_MAX_NODES 64

/* ---- Message storage for GUI ---- */
typedef struct {
    char     sender[WICHAT_MAX_NAME_LEN];
    char     text[WICHAT_MAX_PAYLOAD + 1];
    uint32_t timestamp;
} mesh_message_t;

/* ---- Mesh state ---- */
typedef struct {
    uint32_t    seen_ids[WICHAT_DEDUP_SIZE];  /* 1024 */
    int         seen_head;
    mesh_node_t nodes[MESH_MAX_NODES];
    int         node_count;
    uint8_t     local_mac[6];
    char        callsign[WICHAT_MAX_NAME_LEN];
    uint32_t    last_beacon_ms;
    mesh_message_t messages[WICHAT_MAX_MESSAGES];  /* 256 */
    int            msg_head;
    int            msg_count;
    radio_t    *radio;
    uint8_t     channel;
    uint8_t     ttl;
} mesh_t;

/* ---- Public API ---- */

/* Initialize mesh state — zero out, set defaults */
void mesh_init(mesh_t *m, radio_t *radio, const char *callsign);

/* Process a received raw frame. Returns 0 if processed, -1 if dropped. */
int mesh_process_rx(mesh_t *m, const uint8_t *raw, size_t len, int8_t rssi);

/* Send a chat message */
int mesh_send_chat(mesh_t *m, const char *text);

/* Send a beacon frame with callsign */
int mesh_send_beacon(mesh_t *m);

/* Send a ping frame */
int mesh_send_ping(mesh_t *m);

/* Periodic tick — send beacon if interval elapsed, expire stale nodes */
void mesh_tick(mesh_t *m, uint32_t now_ms);

/* Check if a message ID has been seen before */
int mesh_is_seen(mesh_t *m, uint32_t msg_id);

/* Mark a message ID as seen (ring buffer) */
void mesh_mark_seen(mesh_t *m, uint32_t msg_id);

#endif /* WICHAT_MESH_H */
