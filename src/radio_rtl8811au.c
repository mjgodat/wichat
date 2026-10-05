/* radio_rtl8811au.c — RTL8811AU USB radio backend implementation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "radio_rtl8811au.h"

/* ================================================================
 * Static helpers — USB register access
 * ================================================================ */

/* Read len bytes from device register at addr into val buffer */
static int rtl_read_reg(rtl8811au_priv_t *priv, uint16_t addr,
                         uint8_t *val, uint16_t len)
{
    int rc = libusb_control_transfer(priv->dev,
        RTL_REQTYPE_READ, RTL_REQ_READ,
        addr, 0,
        val, len, RTL_USB_TIMEOUT);
    if (rc < 0) {
        fprintf(stderr, "rtl_read_reg(0x%04x): %s\n",
                addr, libusb_error_name(rc));
        return -1;
    }
    return rc;
}

/* Write len bytes from val buffer to device register at addr */
static int rtl_write_reg(rtl8811au_priv_t *priv, uint16_t addr,
                          const uint8_t *val, uint16_t len)
{
    /* libusb_control_transfer takes non-const data pointer for OUT transfers,
       but does not modify it. Cast away const safely. */
    int rc = libusb_control_transfer(priv->dev,
        RTL_REQTYPE_WRITE, RTL_REQ_WRITE,
        addr, 0,
        (unsigned char *)val, len, RTL_USB_TIMEOUT);
    if (rc < 0) {
        fprintf(stderr, "rtl_write_reg(0x%04x): %s\n",
                addr, libusb_error_name(rc));
        return -1;
    }
    return rc;
}

/* Convenience: write a single byte */
static int rtl_write_reg8(rtl8811au_priv_t *priv, uint16_t addr, uint8_t val)
{
    return rtl_write_reg(priv, addr, &val, 1);
}

/* Convenience: write 16-bit value (little-endian) */
static int rtl_write_reg16(rtl8811au_priv_t *priv, uint16_t addr, uint16_t val)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    return rtl_write_reg(priv, addr, buf, 2);
}

/* Convenience: write 32-bit value (little-endian) */
static int rtl_write_reg32(rtl8811au_priv_t *priv, uint16_t addr, uint32_t val)
{
    uint8_t buf[4];
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
    return rtl_write_reg(priv, addr, buf, 4);
}

/* Convenience: read single byte */
static int rtl_read_reg8(rtl8811au_priv_t *priv, uint16_t addr, uint8_t *val)
{
    return rtl_read_reg(priv, addr, val, 1);
}

/* Convenience: read 32-bit value (little-endian) */
static int rtl_read_reg32(rtl8811au_priv_t *priv, uint16_t addr, uint32_t *val)
{
    uint8_t buf[4];
    int rc = rtl_read_reg(priv, addr, buf, 4);
    if (rc < 0) return -1;
    *val = (uint32_t)buf[0]
         | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16)
         | ((uint32_t)buf[3] << 24);
    return 0;
}

/* ================================================================
 * Static helpers — Device discovery and MAC
 * ================================================================ */

/* Try each known VID/PID until one opens */
static libusb_device_handle *find_rtl_device(libusb_context *ctx)
{
    int i;
    for (i = 0; RTL_DEVICES[i].name != NULL; i++) {
        libusb_device_handle *h = libusb_open_device_with_vid_pid(
            ctx, RTL_DEVICES[i].vid, RTL_DEVICES[i].pid);
        if (h) {
            printf("  Found %s (VID=%04x PID=%04x)\n",
                   RTL_DEVICES[i].name,
                   RTL_DEVICES[i].vid,
                   RTL_DEVICES[i].pid);
            return h;
        }
    }
    return NULL;
}

/* Read 6-byte MAC address from register 0x0050 */
static int rtl_read_mac(rtl8811au_priv_t *priv)
{
    int rc = rtl_read_reg(priv, RTL_REG_MAC, priv->local_mac, 6);
    if (rc < 0) {
        fprintf(stderr, "  Failed to read MAC address\n");
        return -1;
    }
    printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           priv->local_mac[0], priv->local_mac[1], priv->local_mac[2],
           priv->local_mac[3], priv->local_mac[4], priv->local_mac[5]);
    return 0;
}

/* ================================================================
 * Radio vtable functions
 * ================================================================ */

static int rtl_init(radio_t *r)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;
    int rc;

    printf("[RTL8811AU] Initializing USB...\n");

    /* Init libusb */
    rc = libusb_init(&priv->usb_ctx);
    if (rc != 0) {
        fprintf(stderr, "  libusb_init failed: %s\n", libusb_error_name(rc));
        return -1;
    }

    /* Find device */
    printf("  Scanning for RTL8811AU/8812AU devices...\n");
    priv->dev = find_rtl_device(priv->usb_ctx);
    if (!priv->dev) {
        fprintf(stderr, "  No RTL8811AU device found.\n");
        fprintf(stderr, "  (Is the dongle plugged in? Is WinUSB driver installed via Zadig?)\n");
        libusb_exit(priv->usb_ctx);
        priv->usb_ctx = NULL;
        return -1;
    }

    /* Detach kernel driver on Linux */
#ifndef _WIN32
    if (libusb_detach_kernel_driver(priv->dev, 0) == 0) {
        printf("  Detached kernel driver from interface 0\n");
    }
#endif

    /* Claim interface 0 */
    rc = libusb_claim_interface(priv->dev, 0);
    if (rc != 0) {
        fprintf(stderr, "  Failed to claim interface 0: %s\n",
                libusb_error_name(rc));
        libusb_close(priv->dev);
        priv->dev = NULL;
        libusb_exit(priv->usb_ctx);
        priv->usb_ctx = NULL;
        return -1;
    }
    printf("  Claimed interface 0\n");

    /* Read MAC address */
    if (rtl_read_mac(priv) < 0) {
        fprintf(stderr, "  Warning: Could not read MAC (firmware may not be loaded yet)\n");
        /* Not fatal — MAC read will work after firmware upload in Task 6 */
    }

    priv->channel = 1;
    priv->rate = PHY_RATE_1M;
    priv->tx_power = 20;
    priv->initialized = 1;

    printf("[RTL8811AU] USB init complete.\n");
    return 0;
}

static int rtl_set_channel(radio_t *r, uint8_t ch)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;
    priv->channel = ch;
    printf("[RTL8811AU] Channel set to %d (register write deferred to PHY init)\n", ch);
    /* Actual register write comes in Task 6 (firmware + PHY init) */
    return 0;
}

static int rtl_set_rate(radio_t *r, phy_rate_t rate)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;
    priv->rate = rate;
    return 0;
}

static int rtl_set_tx_power(radio_t *r, uint8_t dbm)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;
    priv->tx_power = dbm;
    return 0;
}

static int rtl_tx_frame(radio_t *r, const uint8_t *data, size_t len)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;
    int actual = 0;

    if (!priv->dev) return -1;

    int rc = libusb_bulk_transfer(priv->dev, RTL_EP_BULK_OUT,
        (unsigned char *)data, (int)len, &actual, RTL_USB_TIMEOUT);
    if (rc != 0) {
        fprintf(stderr, "rtl_tx_frame: %s\n", libusb_error_name(rc));
        return -1;
    }
    return actual;
}

static int rtl_rx_frame(radio_t *r, uint8_t *buf, size_t max_len, int timeout_ms)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;
    int actual = 0;

    if (!priv->dev) return -1;

    int rc = libusb_bulk_transfer(priv->dev, RTL_EP_BULK_IN,
        buf, (int)max_len, &actual,
        (unsigned int)(timeout_ms > 0 ? timeout_ms : RTL_USB_TIMEOUT));

    if (rc == LIBUSB_ERROR_TIMEOUT) {
        return 0;  /* timeout — no data */
    }
    if (rc != 0) {
        fprintf(stderr, "rtl_rx_frame: %s\n", libusb_error_name(rc));
        return -1;
    }
    return actual;
}

static void rtl_shutdown(radio_t *r)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;

    if (!priv) return;

    printf("[RTL8811AU] Shutting down...\n");

    if (priv->dev) {
        libusb_release_interface(priv->dev, 0);
        libusb_close(priv->dev);
        priv->dev = NULL;
    }

    if (priv->usb_ctx) {
        libusb_exit(priv->usb_ctx);
        priv->usb_ctx = NULL;
    }

    priv->initialized = 0;
    free(priv);
    r->priv = NULL;

    printf("[RTL8811AU] Shutdown complete.\n");
}

/* ================================================================
 * Public constructor
 * ================================================================ */

int rtl8811au_create(radio_t *r)
{
    rtl8811au_priv_t *priv;

    if (!r) return -1;

    memset(r, 0, sizeof(*r));

    priv = (rtl8811au_priv_t *)calloc(1, sizeof(rtl8811au_priv_t));
    if (!priv) {
        fprintf(stderr, "rtl8811au_create: out of memory\n");
        return -1;
    }

    r->init        = rtl_init;
    r->set_channel = rtl_set_channel;
    r->set_rate    = rtl_set_rate;
    r->set_tx_power = rtl_set_tx_power;
    r->tx_frame    = rtl_tx_frame;
    r->rx_frame    = rtl_rx_frame;
    r->shutdown    = rtl_shutdown;
    r->priv        = priv;

    return 0;
}

/* Suppress unused-function warnings for convenience wrappers
   that will be used by Task 6 (firmware upload + PHY init) */
static void rtl_suppress_unused_warnings_(void) {
    (void)rtl_write_reg8;
    (void)rtl_write_reg16;
    (void)rtl_write_reg32;
    (void)rtl_read_reg8;
    (void)rtl_read_reg32;
    (void)rtl_suppress_unused_warnings_;
}
