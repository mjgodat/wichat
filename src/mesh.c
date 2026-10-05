/* mesh.c — WiChat mesh engine implementation */
#include "mesh.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ---- Static helpers ---- */

/* Add a message to the ring buffer for the GUI */
static void add_message(mesh_t *m, const char *sender, const char *text)
{
    mesh_message_t *msg = &m->messages[m->msg_head];
    memset(msg, 0, sizeof(*msg));

    strncpy(msg->sender, sender, WICHAT_MAX_NAME_LEN - 1);
    msg->sender[WICHAT_MAX_NAME_LEN - 1] = '\0';

    strncpy(msg->text, text, WICHAT_MAX_PAYLOAD);
    msg->text[WICHAT_MAX_PAYLOAD] = '\0';

    msg->timestamp = (uint32_t)time(NULL);

    m->msg_head = (m->msg_head + 1) % WICHAT_MAX_MESSAGES;
    if (m->msg_count < WICHAT_MAX_MESSAGES)
        m->msg_count++;
}

/* Find an existing node by MAC, or allocate an empty slot.
 * Returns pointer to the node, or NULL if table is full. */
static mesh_node_t *find_or_add_node(mesh_t *m, const uint8_t *mac)
{
    int i;
    int empty_slot = -1;

    for (i = 0; i < MESH_MAX_NODES; i++) {
        if (m->nodes[i].active &&
            memcmp(m->nodes[i].mac, mac, 6) == 0) {
            return &m->nodes[i];
        }
        if (!m->nodes[i].active && empty_slot < 0)
            empty_slot = i;
    }

    /* Not found — allocate empty slot */
    if (empty_slot < 0)
        return NULL;  /* table full */

    mesh_node_t *n = &m->nodes[empty_slot];
    memset(n, 0, sizeof(*n));
    memcpy(n->mac, mac, 6);
    n->active = 1;
    m->node_count++;
    return n;
}

/* Return callsign if known, else hex string of last 3 MAC bytes */
static const char *resolve_sender(mesh_t *m, const uint8_t *mac)
{
    static char hex_buf[16];
    int i;

    for (i = 0; i < MESH_MAX_NODES; i++) {
        if (m->nodes[i].active &&
            memcmp(m->nodes[i].mac, mac, 6) == 0 &&
            m->nodes[i].callsign[0] != '\0') {
            return m->nodes[i].callsign;
        }
    }

    /* Unknown — format last 3 bytes as hex */
    snprintf(hex_buf, sizeof(hex_buf), "%02X:%02X:%02X",
             mac[3], mac[4], mac[5]);
    return hex_buf;
}

/* Transmit a frame via the radio, if radio is available.
 * Returns bytes sent, or -1 on error / no radio. */
static int mesh_transmit(mesh_t *m, const wichat_frame_t *frame)
{
    uint8_t buf[512];
    int n;

    if (!m->radio || !m->radio->tx_frame)
        return -1;

    n = frame_encode(frame, m->local_mac, buf, sizeof(buf));
    if (n < 0)
        return -1;

    return m->radio->tx_frame(m->radio, buf, (size_t)n);
}

/* ---- Public API ---- */

void mesh_init(mesh_t *m, radio_t *radio, const char *callsign)
{
    memset(m, 0, sizeof(*m));
    m->radio   = radio;
    m->channel = WICHAT_DEFAULT_CH;
    m->ttl     = WICHAT_DEFAULT_TTL;

    if (callsign) {
        strncpy(m->callsign, callsign, WICHAT_MAX_NAME_LEN - 1);
        m->callsign[WICHAT_MAX_NAME_LEN - 1] = '\0';
    } else {
        strncpy(m->callsign, WICHAT_DEFAULT_NAME, WICHAT_MAX_NAME_LEN - 1);
        m->callsign[WICHAT_MAX_NAME_LEN - 1] = '\0';
    }

    /* Generate a pseudo-random local MAC if radio doesn't provide one */
    {
        uint32_t r1 = frame_gen_msg_id();
        uint32_t r2 = frame_gen_msg_id();
        m->local_mac[0] = (uint8_t)(r1 & 0xFE) | 0x02;  /* locally administered */
        m->local_mac[1] = (uint8_t)(r1 >> 8);
        m->local_mac[2] = (uint8_t)(r1 >> 16);
        m->local_mac[3] = (uint8_t)(r2);
        m->local_mac[4] = (uint8_t)(r2 >> 8);
        m->local_mac[5] = (uint8_t)(r2 >> 16);
    }
}

int mesh_is_seen(mesh_t *m, uint32_t msg_id)
{
    int i;
    for (i = 0; i < WICHAT_DEDUP_SIZE; i++) {
        if (m->seen_ids[i] == msg_id && msg_id != 0)
            return 1;
    }
    return 0;
}

void mesh_mark_seen(mesh_t *m, uint32_t msg_id)
{
    m->seen_ids[m->seen_head] = msg_id;
    m->seen_head = (m->seen_head + 1) % WICHAT_DEDUP_SIZE;
}

int mesh_process_rx(mesh_t *m, const uint8_t *raw, size_t len, int8_t rssi)
{
    wichat_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    /* Decode the raw 802.11 frame */
    if (frame_decode(raw, len, &frame) != 0)
        return -1;

    /* Skip our own frames */
    if (memcmp(frame.hdr.sender, m->local_mac, 6) == 0)
        return -1;

    /* Dedup check */
    if (mesh_is_seen(m, frame.hdr.msg_id))
        return -1;

    mesh_mark_seen(m, frame.hdr.msg_id);

    /* Handle by type */
    switch (frame.hdr.type) {

    case WICHAT_TYPE_BEACON: {
        /* Beacon payload: callsign (16) + flags (1) + uptime (4) */
        mesh_node_t *node = find_or_add_node(m, frame.hdr.sender);
        if (node) {
            node->last_seen = (uint32_t)time(NULL);
            node->rssi = rssi;
            if (frame.payload_len >= WICHAT_MAX_NAME_LEN) {
                memcpy(node->callsign, frame.payload, WICHAT_MAX_NAME_LEN - 1);
                node->callsign[WICHAT_MAX_NAME_LEN - 1] = '\0';
            }
        }
        break;
    }

    case WICHAT_TYPE_CHAT: {
        /* Chat payload is the text message, null-terminated or up to payload_len */
        const char *sender_name = resolve_sender(m, frame.hdr.sender);
        char text_buf[WICHAT_MAX_PAYLOAD + 1];
        memset(text_buf, 0, sizeof(text_buf));
        if (frame.payload_len > 0) {
            size_t copy_len = frame.payload_len;
            if (copy_len > WICHAT_MAX_PAYLOAD)
                copy_len = WICHAT_MAX_PAYLOAD;
            memcpy(text_buf, frame.payload, copy_len);
        }
        add_message(m, sender_name, text_buf);
        break;
    }

    case WICHAT_TYPE_PING: {
        /* Respond with a pong — copy sender's MAC into pong payload */
        wichat_frame_t pong;
        memset(&pong, 0, sizeof(pong));
        pong.hdr.magic  = WICHAT_MAGIC;
        pong.hdr.msg_id = frame_gen_msg_id();
        pong.hdr.ttl    = m->ttl;
        pong.hdr.type   = WICHAT_TYPE_PONG;
        memcpy(pong.hdr.sender, m->local_mac, 6);

        /* Pong payload = 6-byte MAC of the ping sender */
        memcpy(pong.payload, frame.hdr.sender, 6);
        pong.payload_len = 6;

        mesh_mark_seen(m, pong.hdr.msg_id);
        mesh_transmit(m, &pong);

        /* Display system message */
        {
            const char *from = resolve_sender(m, frame.hdr.sender);
            char sys_buf[128];
            snprintf(sys_buf, sizeof(sys_buf), "PING from %s (RSSI %d) — PONG sent",
                     from, (int)rssi);
            add_message(m, "[system]", sys_buf);
        }
        break;
    }

    case WICHAT_TYPE_PONG: {
        /* Pong payload = 6-byte MAC of who the pong is for */
        if (frame.payload_len >= 6 &&
            memcmp(frame.payload, m->local_mac, 6) == 0) {
            /* It's for us */
            const char *from = resolve_sender(m, frame.hdr.sender);
            char sys_buf[128];
            snprintf(sys_buf, sizeof(sys_buf), "PONG from %s (RSSI %d)",
                     from, (int)rssi);
            add_message(m, "[system]", sys_buf);
        }
        /* Update node tracking for the pong sender too */
        {
            mesh_node_t *node = find_or_add_node(m, frame.hdr.sender);
            if (node) {
                node->last_seen = (uint32_t)time(NULL);
                node->rssi = rssi;
            }
        }
        break;
    }

    default:
        break;
    }

    /* ---- Flood relay ---- */
    if (frame.hdr.ttl > 1) {
        wichat_frame_t relay;
        memcpy(&relay, &frame, sizeof(relay));
        relay.hdr.ttl = frame.hdr.ttl - 1;
        mesh_transmit(m, &relay);
    }

    return 0;
}

int mesh_send_chat(mesh_t *m, const char *text)
{
    wichat_frame_t frame;
    size_t text_len;

    if (!text || text[0] == '\0')
        return -1;

    memset(&frame, 0, sizeof(frame));
    frame.hdr.magic  = WICHAT_MAGIC;
    frame.hdr.msg_id = frame_gen_msg_id();
    frame.hdr.ttl    = m->ttl;
    frame.hdr.type   = WICHAT_TYPE_CHAT;
    memcpy(frame.hdr.sender, m->local_mac, 6);

    text_len = strlen(text);
    if (text_len > WICHAT_MAX_PAYLOAD)
        text_len = WICHAT_MAX_PAYLOAD;

    memcpy(frame.payload, text, text_len);
    frame.payload_len = text_len;

    /* Store locally */
    add_message(m, m->callsign, text);

    /* Mark seen so we don't process our own relayed copies */
    mesh_mark_seen(m, frame.hdr.msg_id);

    /* Transmit */
    mesh_transmit(m, &frame);

    return 0;
}

int mesh_send_beacon(mesh_t *m)
{
    wichat_frame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.hdr.magic  = WICHAT_MAGIC;
    frame.hdr.msg_id = frame_gen_msg_id();
    frame.hdr.ttl    = m->ttl;
    frame.hdr.type   = WICHAT_TYPE_BEACON;
    memcpy(frame.hdr.sender, m->local_mac, 6);

    /* Beacon payload: callsign + flags + uptime */
    {
        wichat_beacon_t beacon;
        memset(&beacon, 0, sizeof(beacon));
        strncpy(beacon.callsign, m->callsign, sizeof(beacon.callsign) - 1);
        beacon.flags = 0;
        beacon.uptime_sec = (uint32_t)time(NULL);

        memcpy(frame.payload, &beacon, sizeof(beacon));
        frame.payload_len = sizeof(beacon);
    }

    mesh_mark_seen(m, frame.hdr.msg_id);
    mesh_transmit(m, &frame);

    return 0;
}

int mesh_send_ping(mesh_t *m)
{
    wichat_frame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.hdr.magic  = WICHAT_MAGIC;
    frame.hdr.msg_id = frame_gen_msg_id();
    frame.hdr.ttl    = m->ttl;
    frame.hdr.type   = WICHAT_TYPE_PING;
    memcpy(frame.hdr.sender, m->local_mac, 6);
    frame.payload_len = 0;

    mesh_mark_seen(m, frame.hdr.msg_id);

    /* System message */
    add_message(m, "[system]", "PING sent...");

    mesh_transmit(m, &frame);

    return 0;
}

void mesh_tick(mesh_t *m, uint32_t now_ms)
{
    static uint32_t last_expire_ms = 0;

    /* Send beacon if interval elapsed */
    if (now_ms - m->last_beacon_ms >= WICHAT_BEACON_INTERVAL_MS) {
        mesh_send_beacon(m);
        m->last_beacon_ms = now_ms;
    }

    /* Expire stale nodes — check every 10 seconds */
    if (now_ms - last_expire_ms >= 10000) {
        uint32_t now_sec = (uint32_t)time(NULL);
        int i;

        last_expire_ms = now_ms;

        for (i = 0; i < MESH_MAX_NODES; i++) {
            if (m->nodes[i].active) {
                uint32_t age_sec = now_sec - m->nodes[i].last_seen;
                if (age_sec > (WICHAT_NODE_EXPIRE_MS / 1000)) {
                    /* Node expired — clear slot */
                    memset(&m->nodes[i], 0, sizeof(mesh_node_t));
                    if (m->node_count > 0)
                        m->node_count--;
                }
            }
        }
    }
}
