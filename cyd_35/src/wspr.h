/* wspr.h — WiChat FEC codec: chunked TX with reassembly for range extension */
#ifndef WICHAT_WSPR_H
#define WICHAT_WSPR_H

#include <stdint.h>
#include <stddef.h>
#include "frame.h"

/* ---- Constants ---- */
#define WSPR_MAX_CHUNKS    16
#define WSPR_CHUNK_DATA_SIZE (WICHAT_MAX_PAYLOAD - 6)  /* 230 bytes: payload minus parent_msg_id(4)+seq(1)+total(1) */
#define WSPR_MAX_PENDING   8   /* max simultaneous reassembly sessions */

/* ---- TX context ---- */
typedef struct {
    uint32_t parent_msg_id;
    uint8_t  chunks[WSPR_MAX_CHUNKS][WSPR_CHUNK_DATA_SIZE];
    size_t   chunk_lens[WSPR_MAX_CHUNKS];
    uint8_t  total_chunks;
} wspr_tx_t;

/* ---- RX context (single reassembly session) ---- */
typedef struct {
    uint32_t parent_msg_id;
    uint8_t  received[WSPR_MAX_CHUNKS];   /* 1 if chunk arrived */
    uint8_t  data[WSPR_MAX_CHUNKS][WSPR_CHUNK_DATA_SIZE];
    size_t   data_lens[WSPR_MAX_CHUNKS];
    uint8_t  total_chunks;
    uint32_t first_seen_ms;
    uint8_t  complete;
    uint8_t  active;
} wspr_rx_t;

/* ---- Global FEC context ---- */
typedef struct {
    wspr_rx_t pending[WSPR_MAX_PENDING];
    int       enabled;
} wspr_ctx_t;

/* ---- Functions ---- */

/*
 * Initialize FEC context: zero all pending sessions, set enabled flag.
 */
void wspr_init(wspr_ctx_t *ctx, int enabled);

/*
 * Split a message into chunks for FEC transmission.
 * Returns number of chunks (1..WSPR_MAX_CHUNKS), or -1 on error.
 */
int wspr_encode(wspr_tx_t *tx, const uint8_t *msg, size_t msg_len,
                uint32_t parent_msg_id);

/*
 * Build a WiChat frame for one chunk of a TX context.
 * Chunk payload format: parent_msg_id(4) + seq(1) + total(1) + data.
 * Frame type = WICHAT_TYPE_FEC_CHUNK. Each chunk gets its own msg_id.
 * Returns 0 on success, -1 on error.
 */
int wspr_build_chunk_frame(const wspr_tx_t *tx, int chunk_idx,
                           const uint8_t *local_mac, wichat_frame_t *out);

/*
 * Feed a received FEC chunk frame into the reassembly engine.
 * Returns 1 if the parent message is now complete,
 *         0 if still waiting for more chunks,
 *        -1 on error (FEC disabled, bad frame, no free slots).
 */
int wspr_feed_chunk(wspr_ctx_t *ctx, const wichat_frame_t *frame);

/*
 * Extract a fully (or partially) reassembled message.
 * Concatenates all received chunks in order; missing chunks become "???".
 * Frees the reassembly slot afterwards.
 * Returns total message length written, or -1 on error.
 */
int wspr_get_message(wspr_ctx_t *ctx, uint32_t parent_msg_id,
                     uint8_t *out, size_t out_max);

/*
 * Periodic tick — check for timed-out reassembly sessions.
 * If a session exceeds WICHAT_FEC_TIMEOUT_MS, mark it complete and
 * write its parent_msg_id to *expired_id. Otherwise *expired_id = 0.
 * Only reports one expiry per call (oldest first).
 */
void wspr_tick(wspr_ctx_t *ctx, uint32_t now_ms, uint32_t *expired_id);

#endif /* WICHAT_WSPR_H */
