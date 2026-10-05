/* mesh.h -- WiChat mesh engine for ESP32 CYD
 * Simplified port of PC mesh.h -- calls radio_esp32_tx() directly
 * instead of using a radio_t vtable. Uses millis() for timing. */
#ifndef WICHAT_MESH_H
#define WICHAT_MESH_H

#include "frame.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Node tracking ---- */
typedef struct {
    uint8_t  mac[6];
    char     callsign[WICHAT_MAX_NAME_LEN];
    uint32_t last_seen_ms;
    int8_t   rssi;
    uint8_t  active;
} cyd_mesh_node_t;

/* ---- Message storage for display ---- */
typedef struct {
    char     sender[WICHAT_MAX_NAME_LEN];
    char     text[WICHAT_MAX_PAYLOAD + 1];
    uint32_t timestamp_ms;
} cyd_mesh_message_t;

/* ---- Mesh state ---- */
typedef struct {
    uint32_t           seen_ids[WICHAT_DEDUP_SIZE];
    int                seen_head;
    cyd_mesh_node_t    nodes[MESH_MAX_NODES];
    int                node_count;
    uint8_t            local_mac[6];
    char               callsign[WICHAT_MAX_NAME_LEN];
    uint32_t           last_beacon_ms;
    cyd_mesh_message_t messages[WICHAT_MAX_MESSAGES];
    int                msg_head;
    int                msg_count;
    uint8_t            channel;
    uint8_t            ttl;
    int                msg_version;  /* incremented on new messages for display refresh */
} cyd_mesh_t;

/* Global mesh instance */
extern cyd_mesh_t g_mesh;

/* Initialize mesh state */
void cyd_mesh_init(const char *callsign);

/* Process a received raw frame. Returns 0 if processed, -1 if dropped. */
int cyd_mesh_process_rx(const uint8_t *raw, size_t len, int8_t rssi);

/* Send a chat message */
int cyd_mesh_send_chat(const char *text);

/* Send a beacon */
int cyd_mesh_send_beacon(void);

/* Send a ping */
int cyd_mesh_send_ping(void);

/* Periodic tick -- beacons + node expiry. now_ms = millis(). */
void cyd_mesh_tick(uint32_t now_ms);

/* Check if message ID was already seen */
int cyd_mesh_is_seen(uint32_t msg_id);

/* Mark a message ID as seen */
void cyd_mesh_mark_seen(uint32_t msg_id);

#ifdef __cplusplus
}
#endif

#endif /* WICHAT_MESH_H */
