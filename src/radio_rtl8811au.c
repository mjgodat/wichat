/* radio_rtl8811au.c — RTL8811AU USB radio backend implementation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "radio_rtl8811au.h"

#ifdef _WIN32
#include <windows.h>
#define rtl_sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define rtl_sleep_ms(ms) usleep((ms) * 1000)
#endif

/* ================================================================
 * Embedded firmware blob (captured from working Windows driver via USBPcap)
 * Size: 32768 bytes (8 pages x 4096)
 * ================================================================ */
static const uint8_t rtl_firmware[] = {
#include "../firmware/rtl8811au_fw_captured.h"
};
static const size_t rtl_firmware_len = sizeof(rtl_firmware);

/* ================================================================
 * Captured init sequences and channel tuning tables
 * ================================================================ */
#include "../rtl8811au_init.h"
#include "../rtl8811au_channels.h"

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

/* Read 6-byte MAC address — try multiple locations */
static int rtl_read_mac(rtl8811au_priv_t *priv)
{
    uint8_t mac[6];
    uint16_t addrs[] = {0x0610, 0x0700, 0x0050};
    int i;

    for (i = 0; i < 3; i++) {
        int rc = rtl_read_reg(priv, addrs[i], mac, 6);
        if (rc < 0) continue;
        printf("  MAC @0x%04X: %02x:%02x:%02x:%02x:%02x:%02x\n",
               addrs[i], mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        /* Use first non-zero MAC we find */
        if (!priv->local_mac[0] && !priv->local_mac[1] &&
            (mac[0] || mac[1] || mac[2])) {
            memcpy(priv->local_mac, mac, 6);
        }
    }

    if (!priv->local_mac[0] && !priv->local_mac[1] && !priv->local_mac[2]) {
        fprintf(stderr, "  Warning: no valid MAC found, using default\n");
        priv->local_mac[0] = 0x00;
        priv->local_mac[1] = 0xC0;
        priv->local_mac[2] = 0xCA;
        priv->local_mac[3] = 0xB6;
        priv->local_mac[4] = 0x3A;
        priv->local_mac[5] = 0xDF;
    }
    return 0;
}

/* ================================================================
 * Firmware upload + PHY init
 * Based on Linux driver core/rtw_fw.c and hal/rtl8811au/ sources.
 * Register addresses and bit positions are from the driver; they MAY
 * need adjustment during hardware testing on actual silicon.
 * ================================================================ */

/* Firmware register addresses (from rtw_fw.c / rtl8821a) */
#define REG_MCUFWDL       0x0080   /* MCU firmware download control */
#define REG_MCUFWDL_PAGE  0x0081   /* Firmware page register (high addr bits) */
#define REG_8051FW_CTRL   0x0082   /* 8051 CPU reset/start control */
#define RTL_FW_PAGE_SIZE  4096     /* Firmware page size */
#define RTL_FW_BLOCK_SIZE 64       /* Max bytes per USB control transfer (from capture) */
#define RTL_FW_BASE_ADDR  0x1000   /* USB control transfer base address */

#define REG_SYS_FUNC_EN   0x0002   /* System function enable */
#define REG_CHANNEL       0x0018   /* RF channel register */
#define REG_RX_FILTER_0   0x0608   /* RX filter control register 0 */
#define REG_RX_FILTER_1   0x060C   /* RX filter control register 1 */

/* ---- LLT (Link List Table) registers and constants ----
 * The TX FIFO is divided into 256 pages (128 bytes each on 8812AU).
 * The LLT chains these pages into a free list for TX data and a
 * reserved ring for beacons/management. Without LLT init, TX DMA
 * has no page chain and frames never leave the chip.
 *
 * Reference: rtl8812a_hal_init.c InitLLTTable8812A()
 *            hal_com_reg.h _LLT_INIT_ADDR / _LLT_INIT_DATA / _LLT_OP
 */
#define REG_LLT_INIT       0x01E0  /* LLT read/write port */
#define REG_CR              0x0100  /* MAC Control Register */
#define REG_RQPN            0x0200  /* Reserved Queue Page Number */
#define REG_RQPN_NPQ        0x0214  /* RQPN Normal Priority Queue */
#define REG_TDECTRL          0x0208  /* TX DMA Engine Control */
#define REG_BCNQ_BDNY       0x0424  /* Beacon Queue Boundary */
#define REG_MGQ_BDNY         0x0425  /* Management Queue Boundary */
#define REG_WMAC_LBK_BF_HD  0x045D  /* WMAC Loopback Buffer Head */
#define REG_TXPAUSE          0x0522  /* TX Pause */

/* LLT register field encoding (32-bit write to REG_LLT_INIT):
 *   [7:0]   = data (next page pointer)
 *   [15:8]  = address (page number)
 *   [31:30] = operation (0=idle, 1=write, 2=read) */
#define LLT_OP_WRITE        1
#define LLT_OP_READ         2
#define LLT_OP_IDLE         0

/* 8812AU/8811AU TX buffer: 256 pages total, boundary at page 248 (0xF8).
 * Pages 0-247 = TX data free list. Pages 248-255 = reserved ring (beacon). */
#define TX_PAGE_BOUNDARY_8812  0xF8   /* First reserved page */
#define LAST_ENTRY_OF_TX_PKT_BUFFER_8812  255  /* Last page index */

/* Polling limit for LLT write completion */
#define LLT_POLLING_LIMIT  100

/*
 * rtl_llt_write — Write a single LLT entry.
 *
 * Each entry maps page_addr -> next_page (the "next pointer" in the page chain).
 * Write format at REG_LLT_INIT (0x01E0):
 *   [7:0]   = next_page (data)
 *   [15:8]  = page_addr (address)
 *   [31:30] = 01 (write operation)
 *
 * After writing, poll until [31:30] returns 00 (idle = operation complete).
 *
 * Returns 0 on success, -1 on timeout.
 */
static int rtl_llt_write(rtl8811au_priv_t *priv, uint8_t page_addr, uint8_t next_page)
{
    uint32_t cmd = ((uint32_t)next_page)
                 | ((uint32_t)page_addr << 8)
                 | ((uint32_t)LLT_OP_WRITE << 30);
    int i;

    rtl_write_reg32(priv, REG_LLT_INIT, cmd);

    /* Poll for operation complete: bits [31:30] == 0 (idle) */
    for (i = 0; i < LLT_POLLING_LIMIT; i++) {
        uint32_t val;
        rtl_read_reg32(priv, REG_LLT_INIT, &val);
        if (((val >> 30) & 0x3) == LLT_OP_IDLE) {
            return 0;
        }
    }
    fprintf(stderr, "  LLT write timeout: page %d -> %d\n", page_addr, next_page);
    return -1;
}

/*
 * rtl_llt_read — Read a single LLT entry (for verification).
 * Returns the "next page" value, or -1 on error.
 */
static int rtl_llt_read(rtl8811au_priv_t *priv, uint8_t page_addr)
{
    uint32_t cmd = ((uint32_t)page_addr << 8)
                 | ((uint32_t)LLT_OP_READ << 30);
    int i;

    rtl_write_reg32(priv, REG_LLT_INIT, cmd);

    for (i = 0; i < LLT_POLLING_LIMIT; i++) {
        uint32_t val;
        rtl_read_reg32(priv, REG_LLT_INIT, &val);
        if (((val >> 30) & 0x3) == LLT_OP_IDLE) {
            return (int)(val & 0xFF);  /* data field = next page */
        }
    }
    return -1;
}

/*
 * rtl_init_llt_table — Initialize the Link List Table for TX DMA.
 *
 * The RTL8812AU/8811AU TX FIFO has 256 pages. The LLT creates two
 * linked lists:
 *
 *   1. TX data free list (pages 0 to boundary-1):
 *      Page 0 -> 1 -> 2 -> ... -> (boundary-2) -> (boundary-1) -> 0xFF (end)
 *
 *   2. Reserved ring buffer (pages boundary to 255):
 *      Page boundary -> (boundary+1) -> ... -> 254 -> 255 -> boundary (ring)
 *
 * The TX DMA engine walks list #1 to find free pages for outgoing frames.
 * List #2 is used for beacons/probe responses.
 *
 * WITHOUT this initialization, all LLT entries are 0x00, meaning every page
 * points to page 0 — the TX DMA engine has no valid free list and frames
 * written via USB bulk OUT never get transmitted over the air.
 *
 * Reference: InitLLTTable8812A() in rtl8812a_hal_init.c
 *
 * Returns 0 on success, -1 on failure.
 */
static int rtl_init_llt_table(rtl8811au_priv_t *priv, uint8_t boundary)
{
    uint32_t i;
    int rc;

    printf("[RTL8811AU] Initializing LLT table (boundary=%d/0x%02X)...\n",
           boundary, boundary);

    /* Part 1: TX data free list — pages 0 to (boundary-2) each point to next */
    for (i = 0; i < (uint32_t)(boundary - 1); i++) {
        rc = rtl_llt_write(priv, (uint8_t)i, (uint8_t)(i + 1));
        if (rc < 0) return -1;
    }

    /* End of TX free list: page (boundary-1) points to 0xFF (end marker) */
    rc = rtl_llt_write(priv, (uint8_t)(boundary - 1), 0xFF);
    if (rc < 0) return -1;

    /* Part 2: Reserved ring buffer — pages boundary to 254 each point to next */
    for (i = boundary; i < LAST_ENTRY_OF_TX_PKT_BUFFER_8812; i++) {
        rc = rtl_llt_write(priv, (uint8_t)i, (uint8_t)(i + 1));
        if (rc < 0) return -1;
    }

    /* Close the ring: page 255 points back to boundary */
    rc = rtl_llt_write(priv, LAST_ENTRY_OF_TX_PKT_BUFFER_8812, boundary);
    if (rc < 0) return -1;

    /* Verify a few entries */
    {
        int v0 = rtl_llt_read(priv, 0);
        int v1 = rtl_llt_read(priv, boundary - 1);
        int v2 = rtl_llt_read(priv, LAST_ENTRY_OF_TX_PKT_BUFFER_8812);
        printf("  LLT verify: page[0]->%d (expect 1), page[%d]->%d (expect 255), "
               "page[255]->%d (expect %d)\n",
               v0, boundary - 1, v1, v2, boundary);
        if (v0 != 1 || v1 != 255 || v2 != boundary) {
            fprintf(stderr, "  LLT verification FAILED!\n");
            return -1;
        }
    }

    printf("  LLT table initialized: %d TX pages + %d reserved pages\n",
           boundary, 256 - boundary);
    return 0;
}

/*
 * rtl_power_on — RTL8821A CARDEMU_TO_ACT power-on sequence.
 * Ported from Hal8821APwrSeq.h RTL8821A_TRANS_CARDEMU_TO_ACT.
 * Only USB-relevant steps are included.
 *
 * This MUST run before firmware upload.
 */
static int rtl_power_on(rtl8811au_priv_t *priv)
{
    uint8_t val;
    int i, rc;

    printf("[RTL8811AU] Power-on sequence...\n");

    /* 0x20[0] = 1 — enable LDOA12 MACRO block */
    rc = rtl_read_reg8(priv, 0x0020, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0020, val | 0x01);

    /* 0x67[4] = 0 — disable BT_GPS_SEL pins */
    rc = rtl_read_reg8(priv, 0x0067, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0067, val & ~0x10);

    /* Delay 1ms */
    rtl_sleep_ms(1);

    /* 0x00[5] = 0 — release analog IPs to digital */
    rc = rtl_read_reg8(priv, 0x0000, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0000, val & ~0x20);

    /* 0x05[4:2] = 0 — disable SW LPS and WLSUS_EN */
    rc = rtl_read_reg8(priv, 0x0005, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0005, val & ~0x1C);

    /* Poll 0x06[1] == 1 — wait for power ready */
    printf("  Polling power ready...\n");
    for (i = 0; i < 200; i++) {
        rc = rtl_read_reg8(priv, 0x0006, &val); if (rc < 0) return -1;
        if (val & 0x02) break;
        rtl_sleep_ms(5);
    }
    if (!(val & 0x02)) {
        fprintf(stderr, "  Power ready timeout\n");
        return -1;
    }
    printf("  Power ready (poll %d)\n", i + 1);

    /* 0x06[0] = 1 — release WLON reset */
    rc = rtl_read_reg8(priv, 0x0006, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0006, val | 0x01);

    /* 0x05[7] = 0 — disable HWPDN */
    rc = rtl_read_reg8(priv, 0x0005, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0005, val & ~0x80);

    /* 0x05[4:3] = 0 — disable WL suspend */
    rc = rtl_read_reg8(priv, 0x0005, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0005, val & ~0x18);

    /* 0x05[0] = 1 then poll 0x05[0] == 0 — MAC power on */
    rc = rtl_read_reg8(priv, 0x0005, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0005, val | 0x01);
    for (i = 0; i < 200; i++) {
        rc = rtl_read_reg8(priv, 0x0005, &val); if (rc < 0) return -1;
        if (!(val & 0x01)) break;
        rtl_sleep_ms(5);
    }
    if (val & 0x01) {
        fprintf(stderr, "  MAC power-on timeout\n");
        return -1;
    }

    /* 0x4F[0] = 1 — switch DPDT_SEL_P from WL BB */
    rc = rtl_read_reg8(priv, 0x004F, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x004F, val | 0x01);

    /* 0x67[5:4] = 3 — switch PAPE_G/PAPE_A and LNAON from WL BB */
    rc = rtl_read_reg8(priv, 0x0067, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0067, val | 0x30);

    /* 0x25[6] = 0 — anapar_mac, for WLAN single function */
    rc = rtl_read_reg8(priv, 0x0025, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0025, val & ~0x40);

    /* 0x49[1] = 1 — enable falling edge triggering interrupt */
    rc = rtl_read_reg8(priv, 0x0049, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0049, val | 0x02);

    /* 0x63[1] = 1 — enable GPIO9 interrupt mode */
    rc = rtl_read_reg8(priv, 0x0063, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0063, val | 0x02);

    /* 0x62[1] = 0 — enable GPIO9 input mode */
    rc = rtl_read_reg8(priv, 0x0062, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0062, val & ~0x02);

    /* 0x58[0] = 1 — enable HSISR GPIO interrupt */
    rc = rtl_read_reg8(priv, 0x0058, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x0058, val | 0x01);

    /* 0x5A[1] = 1 — enable HSISR GPIO9 interrupt */
    rc = rtl_read_reg8(priv, 0x005A, &val); if (rc < 0) return -1;
    rtl_write_reg8(priv, 0x005A, val | 0x02);

    /* 0x2E = 0x82 — XTAL trim */
    rtl_write_reg8(priv, 0x002E, 0x82);

    /* REG_CR (0x0100) = 0x2ff — Enable MAC DMA/WMAC/SCHEDULE/SEC blocks.
     * Without this, the 8051 has no clocks and cannot execute firmware.
     * From _InitPowerOn_8812AU in usb_halinit.c */
    rtl_write_reg16(priv, 0x0100, 0x02FF);
    printf("  MAC/DMA blocks enabled (REG_CR=0x02FF)\n");

    /* Small delay for clocks to stabilize */
    rtl_sleep_ms(2);

    printf("[RTL8811AU] Power-on complete.\n");
    return 0;
}

/*
 * rtl_fw_download — Upload firmware blob to the 8051 co-processor.
 *
 * Sequence (from Linux driver rtw_fw.c):
 *   1. Enable MCUFWDL (reg 0x0080 bit 0)
 *   2. Reset 8051 CPU (reg 0x0082 clear bit 3)
 *   3. Write firmware in 4096-byte blocks via control transfer at 0x1000+offset
 *   4. For addresses > 0xFFFF, set page register at 0x0081
 *   5. Clear MCUFWDL enable (reg 0x0080 clear bit 0)
 *   6. Start 8051 (reg 0x0082 set bit 3)
 *   7. Poll WINTINI_RDY (reg 0x0080 bit 7) for up to 100 attempts
 *
 * Returns 0 on success, -1 on failure.
 */
static int rtl_fw_download(rtl8811au_priv_t *priv)
{
    uint8_t reg_val;
    size_t offset;
    size_t block_len;
    int i, rc;

    printf("[RTL8811AU] Firmware upload: %u bytes\n", (unsigned)rtl_firmware_len);

    /* Step 1: Enable MCUFWDL — set bit 0 of reg 0x0080 */
    rc = rtl_read_reg8(priv, REG_MCUFWDL, &reg_val);
    if (rc < 0) {
        fprintf(stderr, "  FW: failed to read MCUFWDL register\n");
        return -1;
    }
    reg_val |= 0x01;  /* MCUFWDL enable = bit 0 */
    rc = rtl_write_reg8(priv, REG_MCUFWDL, reg_val);
    if (rc < 0) {
        fprintf(stderr, "  FW: failed to enable MCUFWDL\n");
        return -1;
    }
    printf("  FW: MCUFWDL enabled\n");

    /* Step 2: Reset 8051 CPU — clear bit 3 of reg 0x0082 */
    rc = rtl_read_reg8(priv, REG_8051FW_CTRL, &reg_val);
    if (rc < 0) {
        fprintf(stderr, "  FW: failed to read 8051 control register\n");
        return -1;
    }
    reg_val &= ~0x08;  /* Clear bit 3 = hold 8051 in reset */
    rc = rtl_write_reg8(priv, REG_8051FW_CTRL, reg_val);
    if (rc < 0) {
        fprintf(stderr, "  FW: failed to reset 8051\n");
        return -1;
    }
    printf("  FW: 8051 held in reset\n");

    /* Step 3+4: Download firmware page-by-page.
     *
     * The chip maps a 4KB window at address 0x1000. The page register
     * (bits [2:0] of reg 0x0082) selects WHICH 4KB of chip memory is
     * visible at that window. So we:
     *   - Set page register to page N
     *   - Write data to addresses 0x1000..0x1FFF (always same range)
     *   - Repeat for each page
     *
     * Each USB control transfer is limited to RTL_FW_BLOCK_SIZE (196) bytes.
     */
    {
        size_t total_pages = (rtl_firmware_len + RTL_FW_PAGE_SIZE - 1) / RTL_FW_PAGE_SIZE;
        size_t page_num;

        for (page_num = 0; page_num < total_pages; page_num++) {
            /* Set page register: bits [2:0] of REG_8051FW_CTRL (0x0082) */
            rc = rtl_read_reg8(priv, REG_8051FW_CTRL, &reg_val);
            if (rc < 0) return -1;
            reg_val = (reg_val & 0xF8) | (uint8_t)(page_num & 0x07);
            rtl_write_reg8(priv, REG_8051FW_CTRL, reg_val);

            /* Calculate how much data is in this page */
            size_t page_offset = page_num * RTL_FW_PAGE_SIZE;
            size_t page_remain = rtl_firmware_len - page_offset;
            if (page_remain > RTL_FW_PAGE_SIZE)
                page_remain = RTL_FW_PAGE_SIZE;

            /* Write this page in small chunks to address 0x1000+ */
            size_t written = 0;
            while (written < page_remain) {
                size_t chunk = page_remain - written;
                if (chunk > RTL_FW_BLOCK_SIZE)
                    chunk = RTL_FW_BLOCK_SIZE;

                uint16_t dl_addr = (uint16_t)(RTL_FW_BASE_ADDR + written);
                rc = rtl_write_reg(priv, dl_addr,
                                   rtl_firmware + page_offset + written,
                                   (uint16_t)chunk);
                if (rc < 0) {
                    fprintf(stderr, "  FW: write failed page %u offset %u\n",
                            (unsigned)page_num, (unsigned)written);
                    return -1;
                }
                written += chunk;
            }

            printf("  FW: page %u/%u (%u bytes)\n",
                   (unsigned)(page_num + 1), (unsigned)total_pages,
                   (unsigned)page_remain);
        }
    }

    /* Step 5: Clear MCUFWDL enable — clear bit 0 of reg 0x0080 */
    rc = rtl_read_reg8(priv, REG_MCUFWDL, &reg_val);
    if (rc < 0) return -1;
    reg_val &= ~0x01;
    rtl_write_reg8(priv, REG_MCUFWDL, reg_val);

    /* Step 6: Poll checksum report — bit 2 of reg 0x0080 (FWDL_ChkSum_rpt) */
    printf("  FW: polling checksum (up to 10s)...\n");
    for (i = 0; i < 1000; i++) {
        uint32_t val32;
        rc = rtl_read_reg32(priv, REG_MCUFWDL, &val32);
        if (rc < 0) return -1;
        if (val32 & 0x04) {  /* FWDL_ChkSum_rpt = bit 2 */
            printf("  FW: checksum OK (poll %d, reg=0x%08x)\n", i + 1, val32);
            break;
        }
        rtl_sleep_ms(5);
    }
    if (i >= 200) {
        fprintf(stderr, "  FW: checksum report timeout\n");
        /* Continue anyway — some chips don't report checksum */
    }

    /* Step 7: Set MCUFWDL_RDY (bit 1), clear WINTINI_RDY (bit 7) */
    {
        uint32_t val32;
        rc = rtl_read_reg32(priv, REG_MCUFWDL, &val32);
        if (rc < 0) return -1;
        val32 |= 0x02;    /* MCUFWDL_RDY = bit 1 */
        val32 &= ~0x80;   /* WINTINI_RDY = bit 7, clear it */
        rtl_write_reg32(priv, REG_MCUFWDL, val32);
        printf("  FW: MCUFWDL_RDY set, WINTINI_RDY cleared\n");
    }

    /* Step 8: 8051 Reset (RTL8821 variant from _8051Reset8812)
     * REG_RSV_CTRL = 0x001C, REG_SYS_FUNC_EN = 0x0002 */
    printf("  FW: resetting 8051...\n");
    {
        /* Disable MCU IO wrapper */
        rc = rtl_read_reg8(priv, 0x001C, &reg_val); if (rc < 0) return -1;
        rtl_write_reg8(priv, 0x001C, reg_val & ~0x02);  /* RSV_CTRL bit 1 clear */

        rc = rtl_read_reg8(priv, 0x001D, &reg_val); if (rc < 0) return -1;
        rtl_write_reg8(priv, 0x001D, reg_val & ~0x01);  /* RSV_CTRL+1 bit 0 clear (8821) */

        /* Disable FW clock */
        rc = rtl_read_reg8(priv, 0x0003, &reg_val); if (rc < 0) return -1;
        rtl_write_reg8(priv, 0x0003, reg_val & ~0x04);  /* SYS_FUNC_EN+1 bit 2 clear */

        /* Enable MCU IO wrapper */
        rc = rtl_read_reg8(priv, 0x001C, &reg_val); if (rc < 0) return -1;
        rtl_write_reg8(priv, 0x001C, reg_val & ~0x02);  /* RSV_CTRL bit 1 clear */

        rc = rtl_read_reg8(priv, 0x001D, &reg_val); if (rc < 0) return -1;
        rtl_write_reg8(priv, 0x001D, reg_val | 0x01);   /* RSV_CTRL+1 bit 0 set (8821) */

        /* Re-enable FW clock */
        rc = rtl_read_reg8(priv, 0x0003, &reg_val); if (rc < 0) return -1;
        rtl_write_reg8(priv, 0x0003, reg_val | 0x04);   /* SYS_FUNC_EN+1 bit 2 set */
    }
    printf("  FW: 8051 reset complete, polling WINTINI_RDY...\n");

    /* Step 9: Poll WINTINI_RDY (bit 7 of reg 0x0080)
     * Capture shows 114 polls needed (~1.1s). Poll for 10s. */
    for (i = 0; i < 1000; i++) {
        uint32_t val32;
        rc = rtl_read_reg32(priv, REG_MCUFWDL, &val32);
        if (rc < 0) return -1;
        if (val32 & 0x80) {  /* WINTINI_RDY = bit 7 */
            printf("  FW: firmware READY! (poll %d, reg=0x%08x)\n", i + 1, val32);
            return 0;
        }
        rtl_sleep_ms(10);
    }

    /* Print final register value for debugging */
    {
        uint32_t val32;
        rtl_read_reg32(priv, REG_MCUFWDL, &val32);
        fprintf(stderr, "  FW: WINTINI_RDY timeout (reg=0x%08x)\n", val32);
    }
    return -1;
}

/* ================================================================
 * Replay captured register init tables
 * ================================================================ */
static int rtl_replay_init_table(rtl8811au_priv_t *priv,
                                  const rtl_reg_write_t *table, int count,
                                  const char *label)
{
    int i;
    for (i = 0; i < count; i++) {
        int rc;
        if (table[i].len == 1) {
            rc = rtl_write_reg8(priv, table[i].reg, (uint8_t)table[i].val);
        } else if (table[i].len == 2) {
            rc = rtl_write_reg16(priv, table[i].reg, (uint16_t)table[i].val);
        } else {
            rc = rtl_write_reg32(priv, table[i].reg, table[i].val);
        }
        if (rc < 0) {
            fprintf(stderr, "  %s: write failed at entry %d (reg=0x%04X)\n",
                    label, i, table[i].reg);
            return -1;
        }
    }
    printf("  %s: %d register writes OK\n", label, count);
    return 0;
}

/*
 * rtl_set_channel_regs — Set RF channel using captured PLL tuning table.
 * Uses RF indirect writes through register 0x0C90.
 */
static int rtl_set_channel_regs(rtl8811au_priv_t *priv, uint8_t ch)
{
    int i;
    const rtl_channel_t *target = NULL;

    /* Look up channel in captured table */
    for (i = 0; i < RTL_NUM_CHANNELS; i++) {
        if (rtl_channels[i].channel == ch) {
            target = &rtl_channels[i];
            break;
        }
    }
    if (!target) {
        fprintf(stderr, "[RTL8811AU] Unknown channel %d\n", ch);
        return -1;
    }

    /* Write RF reg 0x18 (PLL synthesizer) via indirect port 0x0C90 */
    uint32_t rf18_write = RTL_RF_WRITE(0x18, target->rf18_val);
    int rc = rtl_write_reg32(priv, RTL_RF_PORT, rf18_write);
    if (rc < 0) {
        fprintf(stderr, "  Failed to write RF PLL for channel %d\n", ch);
        return -1;
    }

    /* AGC reset after channel change */
    rtl_write_reg32(priv, 0x08B0, 0x00000018);

    printf("[RTL8811AU] Channel %d (%d MHz, %s)\n",
           ch, target->freq_mhz, target->band_5ghz ? "5GHz" : "2.4GHz");
    return 0;
}

/*
 * rtl_set_monitor_mode — Configure RX filters to accept all frames.
 *
 * Writes 0xFFFFFFFF to RX filter registers to disable all frame filtering,
 * which is needed for monitor / promiscuous mode operation.
 * NOTE: The full driver does much more for monitor mode (disabling CRC
 * checks, setting specific PHY registers, etc.). This is sufficient for
 * initial hardware testing.
 */
static int rtl_set_monitor_mode(rtl8811au_priv_t *priv)
{
    int rc;

    /* Accept all frames — RX filter 0 */
    rc = rtl_write_reg32(priv, REG_RX_FILTER_0, 0xFFFFFFFF);
    if (rc < 0) {
        fprintf(stderr, "  Failed to set RX filter register 0\n");
        return -1;
    }

    /* Accept all frames — RX filter 1 */
    rc = rtl_write_reg32(priv, REG_RX_FILTER_1, 0xFFFFFFFF);
    if (rc < 0) {
        fprintf(stderr, "  Failed to set RX filter register 1\n");
        return -1;
    }

    printf("[RTL8811AU] Monitor mode enabled (all-pass RX filter)\n");
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

    /* ---- Step 0: USB device reset for clean state ---- */
    printf("  Resetting USB device...\n");
    rc = libusb_reset_device(priv->dev);
    if (rc != 0) {
        fprintf(stderr, "  USB reset: %s (continuing anyway)\n", libusb_error_name(rc));
        /* Re-claim interface after reset */
    }
    /* Re-claim interface (reset releases it) */
    libusb_claim_interface(priv->dev, 0);
    rtl_sleep_ms(100);

    /* ---- Step 1: Dump chip state after reset ---- */
    {
        uint32_t mcufwdl, reg_cr, aps_fsmco;
        uint8_t reg05;
        rtl_read_reg32(priv, REG_MCUFWDL, &mcufwdl);
        rtl_read_reg32(priv, 0x0100, &reg_cr);
        rtl_read_reg32(priv, 0x0004, &aps_fsmco);
        rtl_read_reg8(priv, 0x0005, &reg05);
        printf("  After reset: MCUFWDL=0x%08x CR=0x%08x APS=0x%08x R05=0x%02x\n",
               mcufwdl, reg_cr, aps_fsmco, reg05);
    }

    /* ---- Step 2: Full power-on (always, ignore stale MCUFWDL_RDY) ---- */
    printf("  Running full power-on sequence...\n");
    rtl_power_on(priv);

    /* ---- Step 3: Upload firmware (always, don't trust stale state) ---- */
    printf("  Uploading firmware...\n");
    if (rtl_fw_download(priv) < 0) {
        fprintf(stderr, "  Firmware upload failed - chip may not be fully operational\n");
    }

    /* ---- Step 3: Replay captured post-FW HAL init ----
     * 1300 register writes: EDCA, WMAC, BB, RF — the full HAL init
     * extracted from the working Windows driver via USBPcap. */
    printf("  Replaying captured HAL init sequence...\n");
    if (rtl_replay_init_table(priv, rtl_post_fw_init, RTL_POST_FW_INIT_COUNT,
                               "Post-FW HAL init") < 0) {
        fprintf(stderr, "  Warning: HAL init replay had errors\n");
    }

    /* ---- Step 3b: Initialize LLT table ----
     * CRITICAL: The captured init sequence sets boundary registers (RQPN,
     * BCNQ_BDNY, MGQ_BDNY etc.) but does NOT initialize the LLT page chain.
     * The LLT write operations use vendor control transfers to REG_LLT_INIT
     * (0x01E0) which were either done before the USBPcap trace started or
     * via a mechanism not captured.
     *
     * Without LLT init, all 256 LLT entries read as 0x00, meaning every
     * page "points to page 0." The TX DMA engine has no valid free-page
     * chain and cannot transmit frames. This is why RX works but TX doesn't.
     *
     * The boundary value 0xF8 (248) comes from the captured register state:
     *   0x0424 (BCNQ_BDNY) = 0xF8
     *   0x0425 (MGQ_BDNY)  = 0xF8
     *   0x0209 (TDECTRL+1) = 0xF8
     *
     * Per devourer PR #451, PROTOCOL_EN (REG_CR bit 4) must be set before
     * LLT init for the hardware page allocator to properly terminate the
     * data ring at the boundary. The captured init already set REG_CR with
     * PROTOCOL_EN, so we're good.
     */

    /* First: ensure REG_CR has ALL enable bits set, including MACTXEN and
     * MACRXEN. The captured init leaves CR[7:0] = 0x3F (MACTXEN/MACRXEN
     * not set). The devourer project found that 0xFF is needed for proper
     * LLT termination and TX operation. */
    {
        uint8_t cr_lo;
        rtl_read_reg8(priv, 0x0100, &cr_lo);
        printf("  REG_CR[0] before LLT: 0x%02x", cr_lo);
        if (cr_lo != 0xFF) {
            rtl_write_reg8(priv, 0x0100, 0xFF);
            printf(" -> set to 0xFF (enable MACTXEN+MACRXEN+PROTOCOL+SCHEDULE+DMA)");
        }
        printf("\n");
    }

    /* Now init the LLT table with boundary = 0xF8 */
    if (rtl_init_llt_table(priv, TX_PAGE_BOUNDARY_8812) < 0) {
        fprintf(stderr, "  LLT init failed — TX will not work!\n");
    }

    /* ---- Step 4: Unpause TX (captured init leaves TXPAUSE=0x0F) ---- */
    rtl_write_reg8(priv, 0x0522, 0x00);

    /* Readback verify */
    {
        uint8_t txpause, cr0, cr1;
        uint32_t rqpn, trxdma;
        rtl_read_reg8(priv, 0x0522, &txpause);
        rtl_read_reg8(priv, 0x0100, &cr0);
        rtl_read_reg8(priv, 0x0101, &cr1);
        rtl_read_reg32(priv, 0x0200, &rqpn);
        rtl_read_reg32(priv, 0x020C, &trxdma);
        printf("  TX state: TXPAUSE=0x%02x CR=0x%02x%02x RQPN=0x%08x TXDMA_OFF=0x%08x\n",
               txpause, cr1, cr0, rqpn, trxdma);
    }

    /* ---- Step 5: RCR for monitor/mesh mode ---- */
    {
        uint32_t rcr = 0;
        rcr |= (1 << 0);   /* AAP: accept all physical address */
        rcr |= (1 << 1);   /* APM: accept physical match */
        rcr |= (1 << 2);   /* AM: accept multicast */
        rcr |= (1 << 3);   /* AB: accept broadcast */
        rcr |= (1 << 5);   /* ACRC32: accept CRC error frames */
        rcr |= (1u << 31); /* APPFCS: append FCS */
        rtl_write_reg32(priv, 0x0608, rcr);
        printf("  RCR set for mesh mode (0x%08x)\n", rcr);
    }

    /* ---- Step 6: Enable TX RF path + set TX power ---- */
    {
        uint32_t val32;
        /* Read OFDM TX path enable (0x0C04) */
        rtl_read_reg32(priv, 0x0C04, &val32);
        printf("  RF_TX_PATH (0x0C04) = 0x%08x\n", val32);

        /* Read TX power index regs */
        uint32_t txpwr_a, txpwr_b;
        rtl_read_reg32(priv, 0x0C20, &txpwr_a);  /* TX AGC A */
        rtl_read_reg32(priv, 0x0E20, &txpwr_b);  /* TX AGC B */
        printf("  TX_AGC_A (0x0C20) = 0x%08x, TX_AGC_B (0x0E20) = 0x%08x\n", txpwr_a, txpwr_b);

        /* Read antenna config */
        uint8_t ant_sel;
        rtl_read_reg8(priv, 0x0092, &ant_sel);
        printf("  ANT_SEL (0x0092) = 0x%02x\n", ant_sel);

        /* Enable TX antenna path A: 0x080C bit 0 = OFDM PA_A enable */
        rtl_read_reg32(priv, 0x080C, &val32);
        printf("  BB_TXPATH (0x080C) = 0x%08x\n", val32);
    }

    /* ---- Step 7a: HWSEQ_CTRL — enable HW sequence for all queues ---- */
    rtl_write_reg8(priv, 0x0423, 0xFF);
    printf("  HWSEQ_CTRL = 0xFF\n");

    /* ---- Step 7b: Verify LLT table is still valid ---- */
    {
        int llt0 = rtl_llt_read(priv, 0);
        int llt_end = rtl_llt_read(priv, TX_PAGE_BOUNDARY_8812 - 1);
        int llt_last = rtl_llt_read(priv, LAST_ENTRY_OF_TX_PKT_BUFFER_8812);
        printf("  LLT check: [0]->%d [%d]->%d [255]->%d (expect 1, 255, %d)\n",
               llt0, TX_PAGE_BOUNDARY_8812 - 1, llt_end, llt_last,
               TX_PAGE_BOUNDARY_8812);
    }

    /* ---- Step 7c: H2C media status — tell firmware we're "associated" ----
     * Writing to REG_HMEBOX_0 (0x01D0) IS the trigger.
     * Do NOT write to REG_HMETFR (0x01CC) — that's read-only status. */
    {
        uint8_t hmetfr;
        rtl_read_reg8(priv, 0x01CC, &hmetfr);
        if (hmetfr & 0x01) {
            printf("  H2C box 0 busy, waiting...\n");
            rtl_sleep_ms(100);
        }
        /* CMD_ID=0x01(MEDIA_STATUS), param=0x01(connected), macid=0 */
        uint32_t h2c = 0x01 | (0x01 << 8);
        rtl_write_reg32(priv, 0x01D0, h2c);
        printf("  H2C MEDIA_STATUS sent (associated)\n");
    }

    /* ---- Step 8: Set initial channel ---- */
    rtl_set_channel_regs(priv, 1);

    /* ---- Step 10: Dump endpoint mapping for debug ---- */
    {
        struct libusb_config_descriptor *cfg = NULL;
        int ep_rc = libusb_get_config_descriptor(
            libusb_get_device(priv->dev), 0, &cfg);
        if (ep_rc == 0 && cfg) {
            const struct libusb_interface_descriptor *alt =
                &cfg->interface[0].altsetting[0];
            int ei;
            printf("  USB endpoints (%d):\n", alt->bNumEndpoints);
            for (ei = 0; ei < alt->bNumEndpoints; ei++) {
                const struct libusb_endpoint_descriptor *ep = &alt->endpoint[ei];
                printf("    EP 0x%02x %s %s maxpkt=%d\n",
                    ep->bEndpointAddress,
                    (ep->bEndpointAddress & 0x80) ? "IN " : "OUT",
                    (ep->bmAttributes & 0x03) == 2 ? "BULK" : "OTHER",
                    ep->wMaxPacketSize);
            }
            libusb_free_config_descriptor(cfg);
        }
    }

    /* ---- RX BSSID scan (10 seconds) ----
     * Scan for WiChat BSSID in all received frames.
     * RTL8812AU RX: bulk transfer returns RX desc + frame data.
     * RX desc DWORD0[13:0] = pktlen. Desc is 24 bytes.
     * Check addr1(offset 4), addr2(offset 10), addr3(offset 16) in 802.11 header. */
    /* ---- RX brute-force BSSID scan (10 seconds) ----
     * Search raw USB data for our BSSID bytes anywhere. */
    printf("  Brute-force scanning RX for BSSID 57:43:57:43:57:43 (10s)...\n");
    {
        static const uint8_t bssid[6] = {0x57, 0x43, 0x57, 0x43, 0x57, 0x43};
        uint8_t rx_test[8192];
        int total_xfers = 0;
        int total_bytes = 0;
        int bssid_hits = 0;
        int i;
        for (i = 0; i < 100; i++) {
            int actual = 0;
            int rc2 = libusb_bulk_transfer(priv->dev, RTL_EP_BULK_IN,
                rx_test, (int)sizeof(rx_test), &actual, 100);
            if (rc2 == LIBUSB_ERROR_TIMEOUT) continue;
            if (rc2 != 0) break;
            if (actual <= 0) continue;
            total_xfers++;
            total_bytes += actual;

            /* Brute-force: scan every byte position for our BSSID */
            int j;
            for (j = 0; j <= actual - 6; j++) {
                if (memcmp(rx_test + j, bssid, 6) == 0) {
                    bssid_hits++;
                    printf("  ** BSSID found at offset %d in xfer #%d (size=%d)\n", j, total_xfers, actual);
                    /* Dump surrounding context */
                    int start = (j > 16) ? j - 16 : 0;
                    int end = (j + 40 < actual) ? j + 40 : actual;
                    int k;
                    printf("     context: ");
                    for (k = start; k < end; k++)
                        printf("%02x ", rx_test[k]);
                    printf("\n");
                    break;  /* one hit per transfer is enough */
                }
            }
        }
        printf("  Scan done: %d xfers, %d bytes, %d BSSID hits\n",
               total_xfers, total_bytes, bssid_hits);
    }

    /* Read MAC address */
    if (rtl_read_mac(priv) < 0) {
        fprintf(stderr, "  Warning: Could not read MAC address\n");
        /* Not fatal — continue init, device may still be usable */
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

    if (rtl_set_channel_regs(priv, ch) < 0) {
        return -1;
    }
    priv->channel = ch;
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

/*
 * Build a 40-byte RTL8812A/8821A USB TX descriptor.
 *
 * Canonical layout from Realtek rtl8812a_xmit.h (txdescriptor_8812 struct):
 *
 *   DWORD 0 (offset  0): pktlen[15:0], offset[23:16], bmc[24], htc[25],
 *                         ls[26], fs[27], ..., own[31]
 *   DWORD 1 (offset  4): macid[5:0], qsel[12:8], rate_id[19:16],
 *                         sectype[23:22], pkt_offset[30:26]
 *   DWORD 2 (offset  8): agg_en[12], bk/agg_break[16], ampdu_density[22:20],
 *                         ant_sel_a[24], ant_sel_b[25]
 *   DWORD 3 (offset 12): nextheadpage[7:0], tailpage[15:8], seq[27:16],
 *                         hwseq_en[31]
 *   DWORD 4 (offset 16): rtsrate[4:0], userate[8], disdatafb[10],
 *                         cts2self[11], rtsen[12], data_short[24], data_bw[25]
 *   DWORD 5 (offset 20): datarate[5:0], sgi[6], rty_lmt_en[17],
 *                         data_rt_lmt[23:18], usb_txagg_num[31:24]
 *   DWORD 6 (offset 24): (aggregation params)
 *   DWORD 7 (offset 28): checksum[15:0] (USB: XOR of first 32 bytes as LE u16)
 *   DWORD 8 (offset 32): (reserved)
 *   DWORD 9 (offset 36): (reserved)
 */
#define RTL_TXDESC_SIZE 40
#define RTL_PKT_OFFSET  8     /* USB packet offset after descriptor */

static void rtl_calc_txdesc_checksum(uint8_t *desc)
{
    uint16_t checksum = 0;
    int i;
    /* Clear checksum field first (DWORD 7 low 16 bits = bytes 28-29) */
    desc[28] = 0;
    desc[29] = 0;
    /* XOR first 32 bytes as 16-bit LE words */
    for (i = 0; i < 32; i += 2) {
        checksum ^= (uint16_t)(desc[i] | (desc[i + 1] << 8));
    }
    desc[28] = (uint8_t)(checksum & 0xFF);
    desc[29] = (uint8_t)(checksum >> 8);
}

static void rtl_write_le32(uint8_t *p, uint32_t val)
{
    p[0] = (uint8_t)(val);
    p[1] = (uint8_t)(val >> 8);
    p[2] = (uint8_t)(val >> 16);
    p[3] = (uint8_t)(val >> 24);
}

static void rtl_build_txdesc(uint8_t *desc, size_t pkt_len)
{
    memset(desc, 0, RTL_TXDESC_SIZE);

    /* DWORD 0 (offset 0): pktlen + offset=40 + flags
     * NO pkt_offset padding — frame immediately follows 40-byte descriptor.
     * Confirmed by janlueders/rtl8812au-macos and OpenHD/devourer. */
    uint32_t dw0 = 0;
    dw0 |= (pkt_len & 0xFFFF);              /* pktlen [15:0] */
    dw0 |= (RTL_TXDESC_SIZE << 16);         /* offset [23:16] = 40 (NOT 48) */
    dw0 |= (1 << 24);                       /* bmc = 1 (broadcast) */
    dw0 |= (1 << 26);                       /* ls = 1 (last segment) */
    dw0 |= (1 << 27);                       /* fs = 1 (first segment) */
    dw0 |= (1u << 31);                      /* OWN = 1 */
    rtl_write_le32(desc + 0, dw0);

    /* DWORD 1 (offset 4): qsel=MGNT, pkt_offset=0, rate_id=1 */
    uint32_t dw1 = 0;
    dw1 |= (0x12 << 8);                     /* qsel [12:8] = QSLT_MGNT */
    dw1 |= (1 << 16);                       /* rate_id [20:16] = 1 (CCK/B) */
    /* pkt_offset = 0 — no extra padding */
    rtl_write_le32(desc + 4, dw1);

    /* DWORD 2 (offset 8): leave zero (no aggregation flags) */

    /* DWORD 3 (offset 12): USE_RATE + DISABLE_FB
     * Per SET_TX_DESC macros (ground truth), NOT the struct layout. */
    uint32_t dw3 = 0;
    dw3 |= (1 << 8);                        /* USE_RATE [8] */
    dw3 |= (1 << 10);                       /* DISABLE_FB [10] */
    rtl_write_le32(desc + 12, dw3);

    /* DWORD 4 (offset 16): TX_RATE = 0x04 (6M OFDM)
     * 6M OFDM is mandatory baseline, works on all channels. */
    uint32_t dw4 = 0;
    dw4 |= 0x04;                            /* TX_RATE [6:0] = 6Mbps OFDM */
    rtl_write_le32(desc + 16, dw4);

    /* DWORD 8 (offset 32): HWSEQ_EN at bit 15 */
    uint32_t dw8 = 0;
    dw8 |= (1 << 15);                       /* HWSEQ_EN [15] */
    rtl_write_le32(desc + 32, dw8);

    /* DWORD 7 (offset 28): USB TX checksum (must be last) */
    rtl_calc_txdesc_checksum(desc);
}

static int rtl_tx_frame(radio_t *r, const uint8_t *data, size_t len)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;
    int actual = 0;
    uint8_t tx_buf[RTL_TXDESC_SIZE + 512];
    static int tx_count = 0;
    size_t total = RTL_TXDESC_SIZE + len;  /* no padding — frame right after desc */

    if (!priv->dev) return -1;
    if (total > sizeof(tx_buf)) return -1;

    /* Build TX descriptor, frame immediately follows */
    rtl_build_txdesc(tx_buf, len);
    memcpy(tx_buf + RTL_TXDESC_SIZE, data, len);

    uint8_t tx_ep = RTL_EP_BULK_OUT;  /* 0x05 = first OUT = HIGH/MGNT queue */

    int rc = libusb_bulk_transfer(priv->dev, tx_ep,
        tx_buf, (int)total, &actual, RTL_USB_TIMEOUT);

    tx_count++;
    if (tx_count <= 10 || rc != 0) {
        printf("[TX] #%d: ep=0x%02x len=%d+%d=%d, rc=%s, actual=%d\n",
               tx_count, tx_ep, RTL_TXDESC_SIZE, (int)len,
               (int)total, rc == 0 ? "OK" : libusb_error_name(rc), actual);
        if (tx_count == 1) {
            int di;
            printf("     desc: ");
            for (di = 0; di < RTL_TXDESC_SIZE; di++)
                printf("%02x ", tx_buf[di]);
            printf("\n");
        }
    }

    if (rc != 0) {
        return -1;
    }
    return (int)len;
}

/*
 * RTL8812AU/8821AU USB RX frame extraction.
 *
 * USB bulk IN transfers contain one or more aggregated frames, each preceded
 * by a 24-byte RX descriptor (RXDESC). The descriptor layout (from Realtek
 * driver rtl8812a_recv.h / rtw_recv.h):
 *
 *   DWORD 0 (offset 0, little-endian):
 *     [13:0]  pkt_len      — 802.11 frame length in bytes
 *     [14]    crc32 error
 *     [15]    icv error
 *     [19:16] drvinfo_sz   — driver/PHY info size in 8-byte units
 *     [25:24] shift        — byte shift for payload alignment
 *
 *   DWORD 3 (offset 12, little-endian):
 *     [23:16] usb_agg_pktnum — number of aggregated packets (first desc only)
 *
 * Frame layout within USB transfer:
 *   [24-byte RXDESC] [drvinfo_sz*8 bytes] [shift bytes] [pkt_len bytes of 802.11]
 *   ... padded to 8-byte alignment, then next RXDESC ...
 *
 * Next frame offset = ROUND_UP_8(24 + drvinfo_sz*8 + shift + pkt_len)
 *
 * 802.11 data frame layout (within pkt_len bytes):
 *   offset  0: frame_control (2)
 *   offset  2: duration (2)
 *   offset  4: addr1/DA (6)
 *   offset 10: addr2/SA (6)
 *   offset 16: addr3/BSSID (6)
 *   offset 22: seq_ctrl (2)
 *   offset 24: payload...
 */

#define RTL_RXDESC_SIZE  24
#define RTL_RX_AGG_BUF   16384

/* Round up to next 8-byte boundary (matches _RND8 in Realtek driver) */
#define RTL_RND8(x) (((x) + 7) & ~7)

static const uint8_t WICHAT_BSSID_PATTERN[6] = {0x57, 0x43, 0x57, 0x43, 0x57, 0x43};

/* Read a little-endian uint32 from a byte pointer */
static uint32_t rtl_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/*
 * Internal RX state: one USB transfer is buffered; we walk through it
 * returning one WiChat frame per call. When exhausted, the next call
 * fetches a new USB transfer.
 */
static uint8_t rx_agg_buf[RTL_RX_AGG_BUF];
static int     rx_agg_len  = 0;   /* total bytes in current USB transfer  */

/* Secondary queue: extracted 802.11 frames waiting to be returned.
 * We parse the entire USB transfer at once (finding all WiChat frames)
 * and hand them back one per call. This avoids re-parsing.             */
#define RX_QUEUE_MAX   16
#define RX_FRAME_MAX   300   /* WiChat frames are <256, add margin */
static uint8_t  rx_queue_data[RX_QUEUE_MAX][RX_FRAME_MAX];
static int      rx_queue_len[RX_QUEUE_MAX];
static int      rx_queue_count = 0;
static int      rx_queue_head  = 0;

/* Debug counters (printed periodically from main loop via [MAIN] stats) */
static int rx_dbg_usb_xfers  = 0;
static int rx_dbg_desc_frames = 0;
static int rx_dbg_wichat_hits = 0;
static int rx_dbg_brute_hits  = 0;
static int rx_dbg_printed     = 0;

/*
 * Parse one USB transfer in rx_agg_buf[0..rx_agg_len) using RX descriptor
 * walking. For each frame whose BSSID matches WiChat, enqueue it.
 * Then fall back to brute-force scan for any frames that descriptor
 * parsing might have missed (e.g. if descriptor fields are garbled).
 */
static void rtl_parse_usb_transfer(void)
{
    int pos = 0;
    int desc_count = 0;
    int bssid_offsets_found[64];  /* track where we found frames via desc */
    int bssid_found_count = 0;

    rx_queue_count = 0;
    rx_queue_head  = 0;

    /* ---- Pass 1: RX descriptor walking ---- */
    while (pos + RTL_RXDESC_SIZE <= rx_agg_len) {
        const uint8_t *desc = rx_agg_buf + pos;
        uint32_t dw0 = rtl_read_le32(desc);

        /* Extract fields from DWORD 0 */
        int pkt_len    = (int)(dw0 & 0x3FFF);          /* bits [13:0]  */
        int drvinfo_sz = (int)((dw0 >> 16) & 0xF) * 8; /* bits [19:16] in 8-byte units */
        int shift      = (int)((dw0 >> 24) & 0x3);     /* bits [25:24] */

        /* Sanity: pkt_len must be > 0 and the frame must fit in the buffer */
        if (pkt_len == 0)
            break;

        int frame_offset = pos + RTL_RXDESC_SIZE + drvinfo_sz + shift;
        int frame_end    = frame_offset + pkt_len;

        if (frame_offset < 0 || frame_end > rx_agg_len || pkt_len > 2400) {
            /* Descriptor looks corrupt — stop walking, fall through to brute force */
            break;
        }

        desc_count++;
        rx_dbg_desc_frames++;

        /* Check if this 802.11 frame is a WiChat frame:
         * Need at least 24 bytes for 802.11 header, BSSID at offset 16 */
        if (pkt_len >= 24) {
            const uint8_t *dot11 = rx_agg_buf + frame_offset;

            if (memcmp(dot11 + 16, WICHAT_BSSID_PATTERN, 6) == 0) {
                /* WiChat frame found via descriptor parsing */
                if (rx_queue_count < RX_QUEUE_MAX) {
                    int copy = pkt_len;
                    if (copy > RX_FRAME_MAX) copy = RX_FRAME_MAX;
                    memcpy(rx_queue_data[rx_queue_count], dot11, copy);
                    rx_queue_len[rx_queue_count] = copy;
                    rx_queue_count++;
                    rx_dbg_wichat_hits++;
                }
                /* Track this offset so brute-force doesn't re-find it */
                if (bssid_found_count < 64)
                    bssid_offsets_found[bssid_found_count++] = frame_offset;
            }
        }

        /* Advance to next aggregated frame (8-byte aligned) */
        int total = RTL_RXDESC_SIZE + drvinfo_sz + shift + pkt_len;
        pos += RTL_RND8(total);
    }

    /* ---- Pass 2: Brute-force BSSID scan as fallback ----
     * Scan the entire USB transfer for BSSID pattern at the addr3 position
     * (offset 16 in 802.11 header). This catches frames that descriptor
     * parsing missed due to corrupt/unexpected descriptor format.          */
    int scan;
    for (scan = 0; scan + 22 <= rx_agg_len; scan++) {
        if (memcmp(rx_agg_buf + scan, WICHAT_BSSID_PATTERN, 6) != 0)
            continue;

        /* Found BSSID at scan. The 802.11 header starts 16 bytes before. */
        int frame_start = scan - 16;
        if (frame_start < 0)
            continue;

        /* Check if descriptor parsing already found this frame */
        int already = 0;
        int k;
        for (k = 0; k < bssid_found_count; k++) {
            int diff = frame_start - bssid_offsets_found[k];
            if (diff < 0) diff = -diff;
            if (diff < 8) { already = 1; break; }  /* same frame (within alignment) */
        }
        if (already)
            continue;

        /* Verify FC looks like a data frame: type bits [3:2] of byte 0 == 0b10 */
        uint8_t fc0 = rx_agg_buf[frame_start];
        if ((fc0 & 0x0C) != 0x08)
            continue;

        /* Determine frame length: scan forward for the WiChat header to get
         * exact payload boundaries. Cap at 256 bytes (WiChat max). */
        int avail = rx_agg_len - frame_start;
        if (avail < 38)  /* need dot11(24) + wichat_hdr(14) minimum */
            continue;
        int frame_len = avail;
        if (frame_len > 256) frame_len = 256;

        if (rx_queue_count < RX_QUEUE_MAX) {
            int copy = frame_len;
            if (copy > RX_FRAME_MAX) copy = RX_FRAME_MAX;
            memcpy(rx_queue_data[rx_queue_count], rx_agg_buf + frame_start, copy);
            rx_queue_len[rx_queue_count] = copy;
            rx_queue_count++;
            rx_dbg_brute_hits++;
        }

        /* Skip past this BSSID match to avoid duplicate hits on the same frame */
        scan += 5;
    }

    /* Print debug info for first few transfers */
    if (rx_dbg_printed < 20) {
        rx_dbg_printed++;
        printf("[RX-PARSE] xfer %d bytes, %d desc-frames, %d wichat(desc), %d wichat(brute), %d queued\n",
               rx_agg_len, desc_count, rx_dbg_wichat_hits, rx_dbg_brute_hits, rx_queue_count);
        /* Dump first RX descriptor for debugging */
        if (rx_agg_len >= RTL_RXDESC_SIZE) {
            uint32_t dw0 = rtl_read_le32(rx_agg_buf);
            printf("  RXDESC dw0=0x%08x pkt_len=%d drvinfo_sz=%d shift=%d\n",
                   dw0,
                   (int)(dw0 & 0x3FFF),
                   (int)((dw0 >> 16) & 0xF) * 8,
                   (int)((dw0 >> 24) & 0x3));
        }
    }
}

static int rtl_rx_frame(radio_t *r, uint8_t *buf, size_t max_len, int timeout_ms)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;

    if (!priv->dev) return -1;

    /* Return a queued frame if we have one */
    if (rx_queue_head < rx_queue_count) {
        int idx = rx_queue_head++;
        int len = rx_queue_len[idx];
        if ((size_t)len > max_len) len = (int)max_len;
        memcpy(buf, rx_queue_data[idx], len);
        return len;
    }

    /* No queued frames — fetch a new USB transfer */
    int actual = 0;
    int rc = libusb_bulk_transfer(priv->dev, RTL_EP_BULK_IN,
        rx_agg_buf, RTL_RX_AGG_BUF, &actual,
        (unsigned int)(timeout_ms > 0 ? timeout_ms : RTL_USB_TIMEOUT));

    if (rc == LIBUSB_ERROR_TIMEOUT)
        return 0;
    if (rc != 0)
        return -1;
    if (actual < RTL_RXDESC_SIZE)
        return 0;

    rx_dbg_usb_xfers++;
    rx_agg_len = actual;

    /* Parse this transfer: walk RX descriptors + brute-force fallback */
    rtl_parse_usb_transfer();

    /* Return the first queued frame, if any */
    if (rx_queue_head < rx_queue_count) {
        int idx = rx_queue_head++;
        int len = rx_queue_len[idx];
        if ((size_t)len > max_len) len = (int)max_len;
        memcpy(buf, rx_queue_data[idx], len);
        return len;
    }

    return 0;
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

/* Note: rtl_write_reg16 is available but not yet used — suppress warning */
static void rtl_suppress_unused_warnings_(void) {
    (void)rtl_write_reg16;
    (void)rtl_suppress_unused_warnings_;
}
