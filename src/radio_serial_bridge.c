/* radio_serial_bridge.c -- ESP32 USB serial bridge radio backend
 *
 * Implements radio_t by talking to an ESP32 running WiChat bridge
 * firmware over USB serial with SLIP framing.
 *
 * Architecture:
 *   - A background thread continuously reads from the COM port,
 *     decodes SLIP frames, and pushes RX_FRAME packets into a
 *     lock-free ring buffer.
 *   - tx_frame() SLIP-encodes the frame and writes it to the COM port.
 *   - rx_frame() drains the ring buffer (non-blocking or with timeout).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "radio_serial_bridge.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <pthread.h>
#endif

/* ---- SLIP constants (must match bridge firmware) ---- */
#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

/* ---- Command tags (must match bridge firmware) ---- */
#define TAG_TX_FRAME    0x01
#define TAG_RX_FRAME    0x02
#define TAG_SET_CHANNEL 0x03
#define TAG_GET_MAC     0x04
#define TAG_MAC_RESP    0x05
#define TAG_STATUS      0x06

/* ---- Serial port helpers ---- */

#ifdef _WIN32
static serial_handle_t serial_open(const char *name, int baud)
{
    /* Prepend \\.\\ for COM ports above COM9 */
    char full_name[64];
    if (strncmp(name, "\\\\.\\", 4) != 0)
        snprintf(full_name, sizeof(full_name), "\\\\.\\%s", name);
    else
        snprintf(full_name, sizeof(full_name), "%s", name);

    HANDLE h = CreateFileA(full_name,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[BRIDGE] Cannot open %s (error %lu)\n",
                full_name, GetLastError());
        return INVALID_SERIAL;
    }

    /* Configure port */
    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return INVALID_SERIAL;
    }

    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX  = FALSE;

    if (!SetCommState(h, &dcb)) {
        fprintf(stderr, "[BRIDGE] SetCommState failed (error %lu)\n",
                GetLastError());
        CloseHandle(h);
        return INVALID_SERIAL;
    }

    /* Set timeouts: read returns immediately with available data */
    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout         = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant   = 1000;  /* 1s write timeout */
    SetCommTimeouts(h, &timeouts);

    /* Flush any stale data */
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    return h;
}

static void serial_close(serial_handle_t h)
{
    if (h != INVALID_SERIAL)
        CloseHandle(h);
}

static int serial_write(serial_handle_t h, const uint8_t *data, size_t len)
{
    DWORD written;
    if (!WriteFile(h, data, (DWORD)len, &written, NULL))
        return -1;
    return (int)written;
}

static int serial_read(serial_handle_t h, uint8_t *buf, size_t max_len)
{
    DWORD got;
    if (!ReadFile(h, buf, (DWORD)max_len, &got, NULL))
        return -1;
    return (int)got;
}

#else /* Linux/macOS */

static serial_handle_t serial_open(const char *name, int baud)
{
    int fd = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[BRIDGE] Cannot open %s: %s\n", name, strerror(errno));
        return INVALID_SERIAL;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    tcgetattr(fd, &tty);

    speed_t speed;
    switch (baud) {
        case 115200:  speed = B115200; break;
        case 230400:  speed = B230400; break;
        case 460800:  speed = B460800; break;
        case 921600:  speed = B921600; break;
        default:      speed = B115200; break;
    }
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cflag = speed | CS8 | CLOCAL | CREAD;
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1; /* 100ms read timeout */
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);

    return fd;
}

static void serial_close(serial_handle_t h)
{
    if (h != INVALID_SERIAL)
        close(h);
}

static int serial_write(serial_handle_t h, const uint8_t *data, size_t len)
{
    return (int)write(h, data, len);
}

static int serial_read(serial_handle_t h, uint8_t *buf, size_t max_len)
{
    return (int)read(h, buf, max_len);
}
#endif

/* ---- SLIP encoder ---- */

static int slip_encode_and_send(serial_handle_t port, const uint8_t *data, size_t len)
{
    /* Worst case: 2*len + 2 bytes */
    uint8_t enc[1100];
    size_t pos = 0;

    enc[pos++] = SLIP_END;
    for (size_t i = 0; i < len && pos < sizeof(enc) - 2; i++) {
        switch (data[i]) {
        case SLIP_END:
            enc[pos++] = SLIP_ESC;
            enc[pos++] = SLIP_ESC_END;
            break;
        case SLIP_ESC:
            enc[pos++] = SLIP_ESC;
            enc[pos++] = SLIP_ESC_ESC;
            break;
        default:
            enc[pos++] = data[i];
            break;
        }
    }
    enc[pos++] = SLIP_END;

    return serial_write(port, enc, pos);
}

/* ---- SLIP decoder (per-byte, used by RX thread) ---- */

static int slip_feed_byte(serial_bridge_priv_t *priv, uint8_t byte)
{
    if (byte == SLIP_END) {
        if (priv->slip_len > 0)
            return 1; /* complete frame */
        priv->slip_len = 0;
        priv->slip_esc = 0;
        return 0;
    }

    if (priv->slip_esc) {
        priv->slip_esc = 0;
        if (byte == SLIP_ESC_END)
            byte = SLIP_END;
        else if (byte == SLIP_ESC_ESC)
            byte = SLIP_ESC;
    } else if (byte == SLIP_ESC) {
        priv->slip_esc = 1;
        return 0;
    }

    if (priv->slip_len < sizeof(priv->slip_buf))
        priv->slip_buf[priv->slip_len++] = byte;

    return 0;
}

static void slip_decoder_reset(serial_bridge_priv_t *priv)
{
    priv->slip_len = 0;
    priv->slip_esc = 0;
}

/* ---- Process a decoded SLIP frame from the bridge ---- */

static void handle_bridge_frame(serial_bridge_priv_t *priv, const uint8_t *data, size_t len)
{
    if (len < 1)
        return;

    uint8_t tag = data[0];

    switch (tag) {

    case TAG_RX_FRAME: {
        /* Format: [TAG] [rssi] [raw 802.11 frame...] */
        if (len < 3) break;
        int8_t rssi = (int8_t)data[1];
        const uint8_t *frame = data + 2;
        size_t frame_len = len - 2;
        if (frame_len > 512) frame_len = 512;

        /* Push into ring buffer */
        int next = (priv->rx_ring_head + 1) % BRIDGE_RX_RING_SIZE;
        if (next != priv->rx_ring_tail) {
            memcpy(priv->rx_ring[priv->rx_ring_head].data, frame, frame_len);
            priv->rx_ring[priv->rx_ring_head].len = (uint16_t)frame_len;
            priv->rx_ring[priv->rx_ring_head].rssi = rssi;
            /* Memory barrier before advancing head */
#ifdef _WIN32
            MemoryBarrier();
#else
            __sync_synchronize();
#endif
            priv->rx_ring_head = next;
        }
        /* else: ring full, drop frame */
        break;
    }

    case TAG_MAC_RESP:
        if (len >= 7) {
            memcpy(priv->local_mac, data + 1, 6);
            priv->mac_received = 1;
            printf("[BRIDGE] Got MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   priv->local_mac[0], priv->local_mac[1],
                   priv->local_mac[2], priv->local_mac[3],
                   priv->local_mac[4], priv->local_mac[5]);
        }
        break;

    case TAG_STATUS:
        /* Optional: log bridge stats */
        if (len >= 10) {
            uint32_t rx_cnt, tx_cnt;
            memcpy(&rx_cnt, data + 1, 4);
            memcpy(&tx_cnt, data + 5, 4);
            uint8_t ch = data[9];
            printf("[BRIDGE] status: rx=%lu tx=%lu ch=%d\n",
                   (unsigned long)rx_cnt, (unsigned long)tx_cnt, ch);
        }
        break;

    default:
        break;
    }
}

/* ---- RX thread: continuously reads serial and decodes SLIP ---- */

#ifdef _WIN32
static DWORD WINAPI rx_thread_func(LPVOID arg)
{
    serial_bridge_priv_t *priv = (serial_bridge_priv_t *)arg;
    uint8_t chunk[256];

    while (priv->running) {
        int n = serial_read(priv->port, chunk, sizeof(chunk));
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                if (slip_feed_byte(priv, chunk[i])) {
                    handle_bridge_frame(priv, priv->slip_buf, priv->slip_len);
                    slip_decoder_reset(priv);
                }
            }
        } else {
            /* No data available -- brief sleep to avoid busy-wait */
            Sleep(1);
        }
    }
    return 0;
}
#else
static void *rx_thread_func(void *arg)
{
    serial_bridge_priv_t *priv = (serial_bridge_priv_t *)arg;
    uint8_t chunk[256];

    while (priv->running) {
        int n = serial_read(priv->port, chunk, sizeof(chunk));
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                if (slip_feed_byte(priv, chunk[i])) {
                    handle_bridge_frame(priv, priv->slip_buf, priv->slip_len);
                    slip_decoder_reset(priv);
                }
            }
        } else {
            usleep(1000);
        }
    }
    return NULL;
}
#endif

/* ---- radio_t vtable implementations ---- */

static int bridge_init(radio_t *r)
{
    serial_bridge_priv_t *priv = (serial_bridge_priv_t *)r->priv;

    /* Start RX thread */
    priv->running = 1;
#ifdef _WIN32
    priv->rx_thread = CreateThread(NULL, 0, rx_thread_func, priv, 0, NULL);
    if (!priv->rx_thread) {
        fprintf(stderr, "[BRIDGE] Failed to create RX thread\n");
        return -1;
    }
#else
    pthread_t thr;
    if (pthread_create(&thr, NULL, rx_thread_func, priv) != 0) {
        fprintf(stderr, "[BRIDGE] Failed to create RX thread\n");
        return -1;
    }
    pthread_detach(thr);
#endif

    /* Request MAC address from bridge */
    uint8_t cmd[1] = { TAG_GET_MAC };
    slip_encode_and_send(priv->port, cmd, 1);

    /* Wait up to 2 seconds for MAC response */
    for (int i = 0; i < 200 && !priv->mac_received; i++) {
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }

    if (!priv->mac_received) {
        fprintf(stderr, "[BRIDGE] Warning: did not receive MAC from bridge\n");
        /* Generate a random MAC as fallback */
        priv->local_mac[0] = 0x02;
        priv->local_mac[1] = 0xBE;
        priv->local_mac[2] = 0x1D;
#ifdef _WIN32
        {
            DWORD t = GetTickCount();
            priv->local_mac[3] = (uint8_t)(t >> 0);
            priv->local_mac[4] = (uint8_t)(t >> 8);
            priv->local_mac[5] = (uint8_t)(t >> 16);
        }
#else
        {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            priv->local_mac[3] = (uint8_t)(ts.tv_nsec >> 0);
            priv->local_mac[4] = (uint8_t)(ts.tv_nsec >> 8);
            priv->local_mac[5] = (uint8_t)(ts.tv_nsec >> 16);
        }
#endif
    }

    priv->initialized = 1;
    printf("[BRIDGE] Radio initialized via serial bridge.\n");
    return 0;
}

static int bridge_set_channel(radio_t *r, uint8_t ch)
{
    serial_bridge_priv_t *priv = (serial_bridge_priv_t *)r->priv;
    uint8_t cmd[2] = { TAG_SET_CHANNEL, ch };
    slip_encode_and_send(priv->port, cmd, 2);
    priv->channel = ch;
    return 0;
}

static int bridge_set_rate(radio_t *r, phy_rate_t rate)
{
    /* ESP32 rate is fixed at init. Could add a tag if needed. */
    (void)r; (void)rate;
    return 0;
}

static int bridge_set_tx_power(radio_t *r, uint8_t dbm)
{
    /* ESP32 TX power is fixed at max (20dBm). Could add a tag. */
    (void)r; (void)dbm;
    return 0;
}

static int bridge_tx_frame(radio_t *r, const uint8_t *data, size_t len)
{
    serial_bridge_priv_t *priv = (serial_bridge_priv_t *)r->priv;
    if (!priv->initialized || priv->port == INVALID_SERIAL)
        return -1;

    /* Build tagged frame: [TAG_TX_FRAME] [raw 802.11 data...] */
    uint8_t pkt[1 + 512];
    if (len > 512) len = 512;
    pkt[0] = TAG_TX_FRAME;
    memcpy(pkt + 1, data, len);

    return slip_encode_and_send(priv->port, pkt, 1 + len);
}

static int bridge_rx_frame(radio_t *r, uint8_t *buf, size_t max_len, int timeout_ms)
{
    serial_bridge_priv_t *priv = (serial_bridge_priv_t *)r->priv;

    /* Check ring buffer for available frame */
    int elapsed = 0;
    while (elapsed <= timeout_ms) {
        if (priv->rx_ring_tail != priv->rx_ring_head) {
            /* Frame available */
            int idx = priv->rx_ring_tail;
            size_t copy_len = priv->rx_ring[idx].len;
            if (copy_len > max_len)
                copy_len = max_len;
            memcpy(buf, priv->rx_ring[idx].data, copy_len);

            /* Advance tail */
#ifdef _WIN32
            MemoryBarrier();
#else
            __sync_synchronize();
#endif
            priv->rx_ring_tail = (idx + 1) % BRIDGE_RX_RING_SIZE;

            return (int)copy_len;
        }

        if (timeout_ms == 0)
            return 0;

        /* Brief sleep then retry */
#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
        elapsed++;
    }

    return 0; /* timeout, no frame */
}

static void bridge_shutdown(radio_t *r)
{
    serial_bridge_priv_t *priv = (serial_bridge_priv_t *)r->priv;
    if (!priv)
        return;

    priv->running = 0;

#ifdef _WIN32
    if (priv->rx_thread) {
        WaitForSingleObject(priv->rx_thread, 2000);
        CloseHandle(priv->rx_thread);
    }
#endif

    serial_close(priv->port);
    free(priv);
    r->priv = NULL;
    printf("[BRIDGE] Shutdown complete.\n");
}

/* ---- Auto-detect: scan COM ports for a bridge ---- */

#ifdef _WIN32
static const char *auto_detect_port(void)
{
    static char found[16];
    /* Try COM3 through COM20 */
    for (int i = 3; i <= 20; i++) {
        char name[16];
        snprintf(name, sizeof(name), "COM%d", i);
        char full[32];
        snprintf(full, sizeof(full), "\\\\.\\%s", name);

        HANDLE h = CreateFileA(full, GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            snprintf(found, sizeof(found), "%s", name);
            printf("[BRIDGE] Auto-detected port: %s\n", found);
            return found;
        }
    }
    return NULL;
}
#else
static const char *auto_detect_port(void)
{
    /* Try common Linux/macOS serial devices */
    static const char *candidates[] = {
        "/dev/ttyUSB0", "/dev/ttyUSB1",
        "/dev/ttyACM0", "/dev/ttyACM1",
        "/dev/cu.usbserial-0001",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        int fd = open(candidates[i], O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd >= 0) {
            close(fd);
            printf("[BRIDGE] Auto-detected port: %s\n", candidates[i]);
            return candidates[i];
        }
    }
    return NULL;
}
#endif

/* ---- Public constructor ---- */

int serial_bridge_create(radio_t *r, const char *port_name)
{
    serial_bridge_priv_t *priv;

    /* Auto-detect if no port specified */
    if (!port_name) {
        port_name = auto_detect_port();
        if (!port_name) {
            fprintf(stderr, "[BRIDGE] No serial port found.\n");
            return -1;
        }
    }

    /* Open serial port */
    serial_handle_t port = serial_open(port_name, 921600);
    if (port == INVALID_SERIAL)
        return -1;

    printf("[BRIDGE] Opened %s at 921600 baud.\n", port_name);

    /* Allocate private state */
    priv = (serial_bridge_priv_t *)calloc(1, sizeof(*priv));
    if (!priv) {
        serial_close(port);
        return -1;
    }

    priv->port = port;
    priv->channel = 1;
    priv->rx_ring_head = 0;
    priv->rx_ring_tail = 0;
    slip_decoder_reset(priv);

    /* Wire up vtable */
    memset(r, 0, sizeof(*r));
    r->init         = bridge_init;
    r->set_channel  = bridge_set_channel;
    r->set_rate     = bridge_set_rate;
    r->set_tx_power = bridge_set_tx_power;
    r->tx_frame     = bridge_tx_frame;
    r->rx_frame     = bridge_rx_frame;
    r->shutdown     = bridge_shutdown;
    r->priv         = priv;

    return 0;
}
