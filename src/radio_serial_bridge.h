/* radio_serial_bridge.h -- ESP32 USB serial bridge radio backend
 *
 * Implements the radio_t interface by talking to an ESP32 running
 * the WiChat bridge firmware over a serial port with SLIP framing.
 *
 * The ESP32 handles all raw 802.11 TX/RX. The PC just sends/receives
 * frames over the serial link.
 */
#ifndef WICHAT_RADIO_SERIAL_BRIDGE_H
#define WICHAT_RADIO_SERIAL_BRIDGE_H

#include "radio.h"

#ifdef _WIN32
#include <windows.h>
typedef HANDLE serial_handle_t;
#define INVALID_SERIAL INVALID_HANDLE_VALUE
#else
typedef int serial_handle_t;
#define INVALID_SERIAL (-1)
#endif

/* ---- Private state ---- */
typedef struct {
    serial_handle_t port;
    uint8_t         local_mac[6];
    uint8_t         channel;
    int             initialized;
    int             mac_received;

    /* RX ring buffer: filled by rx_thread, drained by rx_frame() */
    #define BRIDGE_RX_RING_SIZE 64
    struct {
        uint8_t  data[512];
        uint16_t len;
        int8_t   rssi;
    } rx_ring[BRIDGE_RX_RING_SIZE];
    volatile int rx_ring_head;  /* written by rx_thread */
    volatile int rx_ring_tail;  /* read by rx_frame() */

    /* SLIP decoder state (used by rx_thread) */
    uint8_t slip_buf[1100];
    size_t  slip_len;
    int     slip_esc;

    /* Thread handle */
#ifdef _WIN32
    HANDLE  rx_thread;
    volatile int running;
#else
    /* pthread_t rx_thread; */
    volatile int running;
#endif
} serial_bridge_priv_t;

/* ---- Public constructor ---- */

/* Create a serial bridge radio.
 * port_name: e.g. "COM5" on Windows, "/dev/ttyUSB0" on Linux.
 * If port_name is NULL, auto-detect by scanning COM3-COM20.
 * Returns 0 on success, -1 on failure.
 */
int serial_bridge_create(radio_t *r, const char *port_name);

#endif /* WICHAT_RADIO_SERIAL_BRIDGE_H */
