/* radio_rtl8811au.h — RTL8811AU USB radio backend */
#ifndef WICHAT_RADIO_RTL8811AU_H
#define WICHAT_RADIO_RTL8811AU_H

#include "radio.h"
#include <libusb.h>

/* ---- Known RTL8811AU / RTL8812AU device VID/PIDs ---- */
static const struct { uint16_t vid; uint16_t pid; const char *name; } RTL_DEVICES[] = {
    {0x0bda, 0xa811, "RTL8811AU"},
    {0x0bda, 0x8812, "RTL8812AU"},
    {0x0bda, 0x0811, "RTL8811AU (alt)"},
    {0x0bda, 0xa812, "RTL8812AU (alt)"},
    {0, 0, NULL}
};

/* ---- USB endpoints (enumerated from actual device) ---- */
#define RTL_EP_BULK_OUT  0x05
#define RTL_EP_BULK_IN   0x84

/* ---- Register access via vendor control transfers ---- */
#define RTL_REQ_READ     0x05
#define RTL_REQ_WRITE    0x05
#define RTL_REQTYPE_READ  (LIBUSB_ENDPOINT_IN  | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE)
#define RTL_REQTYPE_WRITE (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE)

/* ---- USB transfer timeout (ms) ---- */
#define RTL_USB_TIMEOUT  1000

/* ---- MAC address register (from capture: 0x0610-0x0615) ---- */
#define RTL_REG_MAC      0x0610

/* ---- Private state ---- */
typedef struct {
    libusb_context       *usb_ctx;
    libusb_device_handle *dev;
    uint8_t               local_mac[6];
    uint8_t               channel;
    phy_rate_t            rate;
    uint8_t               tx_power;
    int                   initialized;
} rtl8811au_priv_t;

/* ---- Public constructor ---- */
int rtl8811au_create(radio_t *r);

#endif /* WICHAT_RADIO_RTL8811AU_H */
