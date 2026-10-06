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
 * Embedded firmware blob (extracted from Linux driver hal8821a_fw.c)
 * Size: 30880 bytes
 * ================================================================ */
static const uint8_t rtl_firmware[] = {
#include "../firmware/rtl8811au_fw.h"
};
static const size_t rtl_firmware_len = sizeof(rtl_firmware);

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
#define RTL_FW_BLOCK_SIZE 196      /* Max bytes per USB control transfer */
#define RTL_FW_BASE_ADDR  0x1000   /* USB control transfer base address */

#define REG_SYS_FUNC_EN   0x0002   /* System function enable */
#define REG_CHANNEL       0x0018   /* RF channel register */
#define REG_RX_FILTER_0   0x0608   /* RX filter control register 0 */
#define REG_RX_FILTER_1   0x060C   /* RX filter control register 1 */

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

    /* Step 7: Set MCUFWDL_RDY (bit 1), clear WINTINI_RDY (bit 6) */
    {
        uint32_t val32;
        rc = rtl_read_reg32(priv, REG_MCUFWDL, &val32);
        if (rc < 0) return -1;
        val32 |= 0x02;    /* MCUFWDL_RDY = bit 1 */
        val32 &= ~0x40;   /* WINTINI_RDY = bit 6, clear it */
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

    /* Step 9: Poll WINTINI_RDY (bit 6 of 32-bit reg 0x0080)
     * RTL8821A 8051 can take 2-3 seconds to complete startup.
     * Poll for 10 full seconds before declaring failure. */
    for (i = 0; i < 1000; i++) {
        uint32_t val32;
        rc = rtl_read_reg32(priv, REG_MCUFWDL, &val32);
        if (rc < 0) return -1;
        if (val32 & 0x40) {  /* WINTINI_RDY = bit 6 */
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

/*
 * rtl_set_channel_regs — Write RF channel to hardware registers.
 *
 * For 2.4 GHz channels 1-13: freq = ch * 5 + 2407 MHz
 * Channel 14: freq = 2484 MHz
 * NOTE: This is a simplified version. The full driver writes additional
 * PHY/RF registers per channel. Enough for initial hardware testing.
 */
static int rtl_set_channel_regs(rtl8811au_priv_t *priv, uint8_t ch)
{
    int freq;
    int rc;

    if (ch < 1 || ch > 14) {
        fprintf(stderr, "[RTL8811AU] Invalid channel %d (must be 1-14)\n", ch);
        return -1;
    }

    /* Calculate frequency */
    freq = (ch == 14) ? 2484 : (ch * 5 + 2407);

    /* Write channel number to hardware */
    rc = rtl_write_reg8(priv, REG_CHANNEL, ch);
    if (rc < 0) {
        fprintf(stderr, "  Failed to set channel register\n");
        return -1;
    }

    printf("[RTL8811AU] Channel %d (%d MHz)\n", ch, freq);
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

    /* ---- Step 3: Enable MAC/DMA blocks ---- */
    rtl_write_reg16(priv, 0x0100, 0x02FF);  /* REG_CR: enable MAC/WMAC/SEC */
    printf("  MAC/DMA blocks enabled\n");

    /* ---- Step 4: Queue page buffer partition (CRITICAL for TX) ----
     * Without this, TX DMA has no memory and NAKs all bulk OUT.
     * Total buffer: 196 pages (0xC4), 128 bytes each.
     * Allocate: HI=4, MGT=8, rest=RX */
    rtl_write_reg32(priv, 0x0014, 0x00000000);  /* RQPN: VO=0,VI=0,BE=0,BK=0 */
    rtl_write_reg8(priv, 0x0208, 0xC4);         /* BCN boundary at end */
    rtl_write_reg8(priv, 0x0209, 0x0C);         /* MGT queue boundary */
    rtl_write_reg8(priv, 0x020A, 0x0C);         /* WMAC loopback boundary */
    rtl_write_reg8(priv, 0x020B, 0x04);         /* WMM boundary: HI ends at page 4 */
    rtl_write_reg16(priv, 0x0218, 0x0408);       /* RQPN_NPQ: HI=4, MGT=8 pages */
    rtl_write_reg8(priv, 0x021A, 0x0C);         /* RXPKTBUF_BDNY: RX starts at page 12 */
    printf("  TX queue page buffers configured (HI=4, MGT=8, RX from page 12)\n");

    /* ---- Step 5: TRXDMA queue-to-endpoint mapping ---- */
    rtl_write_reg16(priv, 0x010C, 0xF5B1);      /* Standard USB 2-EP mapping */
    printf("  TRXDMA queue mapping set\n");

    /* ---- Step 6: Enable MAC TRX ---- */
    rtl_write_reg8(priv, 0x0100, 0xFF);          /* Enable all WMAC TRX */
    rtl_write_reg8(priv, 0x0101, 0x03);          /* Enable MAC TRX */
    rtl_write_reg16(priv, 0x0522, 0x0000);       /* Clear TX pause */
    printf("  MAC TRX enabled, TX pause cleared\n");

    /* ---- Step 7: RCR for monitor mode ----
     * 0xFFFFFFFF is WRONG - CBSSID bit restricts reception.
     * Set only the bits we need. */
    {
        uint32_t rcr = 0;
        rcr |= (1 << 0);   /* AAP: accept all physical address */
        rcr |= (1 << 1);   /* APM: accept physical match */
        rcr |= (1 << 2);   /* AM: accept multicast */
        rcr |= (1 << 3);   /* AB: accept broadcast */
        rcr |= (1 << 5);   /* ACRC32: accept CRC error frames */
        rcr |= (1u << 31); /* APPFCS: append FCS */
        rtl_write_reg32(priv, 0x0608, rcr);
        printf("  RCR set for monitor mode (0x%08x)\n", rcr);
    }

    /* ---- Step 8: EFUSE autoload for MAC ---- */
    {
        uint8_t val;
        rtl_read_reg8(priv, 0x0001, &val);
        rtl_write_reg8(priv, 0x0001, val | 0x08);  /* trigger EFUSE autoload */
        rtl_sleep_ms(20);
    }

    /* ---- Step 9: Set channel ---- */
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

    /* ---- RX test (3 seconds) ---- */
    printf("  Testing RX (3 second listen)...\n");
    {
        uint8_t rx_test[2048];
        int actual = 0;
        int total_rx = 0;
        int i, rc2;
        for (i = 0; i < 30; i++) {
            actual = 0;
            rc2 = libusb_bulk_transfer(priv->dev, RTL_EP_BULK_IN,
                rx_test, (int)sizeof(rx_test), &actual, 100);
            if (rc2 == LIBUSB_ERROR_TIMEOUT) continue;
            if (rc2 != 0) {
                if (total_rx == 0)
                    printf("  RX error: %s\n", libusb_error_name(rc2));
                break;
            }
            if (actual > 0) {
                total_rx++;
                if (total_rx <= 3) {
                    int j;
                    printf("  RX[%d]: %d bytes. Hex: ", total_rx, actual);
                    for (j = 0; j < 32 && j < actual; j++)
                        printf("%02x ", rx_test[j]);
                    printf("\n");
                }
            }
        }
        printf("  RX test done: %d frames in 3 seconds\n", total_rx);
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
 * Build a 40-byte RTL8812A/8821A TX descriptor.
 * The descriptor tells the DMA engine the packet length, TX rate,
 * queue selection, and other parameters.
 *
 * Format (little-endian 32-bit words):
 *   DWORD 0: pktlen[15:0], offset[23:16], bmc[24], fs[27], ls[26], own[31]
 *   DWORD 1: macid[5:0], qsel[12:8]
 *   DWORD 2-3: various flags
 *   DWORD 4: userate[8], datarate[6:0]
 *   DWORD 5-9: unused for our purposes
 */
#define RTL_TXDESC_SIZE 40

static void rtl_build_txdesc(uint8_t *desc, size_t pkt_len)
{
    memset(desc, 0, RTL_TXDESC_SIZE);

    /* DWORD 0: pktlen + offset + flags */
    uint32_t dw0 = 0;
    dw0 |= (pkt_len & 0xFFFF);         /* pktlen [15:0] */
    dw0 |= (RTL_TXDESC_SIZE << 16);    /* offset [23:16] = 40 */
    dw0 |= (1 << 24);                  /* bmc = 1 (broadcast) */
    dw0 |= (1 << 26);                  /* ls = 1 (last segment) */
    dw0 |= (1 << 27);                  /* fs = 1 (first segment) */
    dw0 |= (1u << 31);                 /* OWN = 1 (hand to hardware) */
    desc[0] = (uint8_t)(dw0);
    desc[1] = (uint8_t)(dw0 >> 8);
    desc[2] = (uint8_t)(dw0 >> 16);
    desc[3] = (uint8_t)(dw0 >> 24);

    /* DWORD 1: macid=0, qsel=0x12 (beacon/high queue) */
    uint32_t dw1 = 0;
    dw1 |= (0x12 << 8);                /* qsel [12:8] = 0x12 */
    desc[4] = (uint8_t)(dw1);
    desc[5] = (uint8_t)(dw1 >> 8);
    desc[6] = (uint8_t)(dw1 >> 16);
    desc[7] = (uint8_t)(dw1 >> 24);

    /* DWORD 4: userate=1, datarate=0 (1Mbps CCK) */
    uint32_t dw4 = 0;
    dw4 |= (1 << 8);                   /* userate = 1 (fixed rate) */
    dw4 |= 0;                          /* datarate = 0 (1M CCK) */
    desc[16] = (uint8_t)(dw4);
    desc[17] = (uint8_t)(dw4 >> 8);
    desc[18] = (uint8_t)(dw4 >> 16);
    desc[19] = (uint8_t)(dw4 >> 24);
}

static int rtl_tx_frame(radio_t *r, const uint8_t *data, size_t len)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;
    int actual = 0;
    uint8_t tx_buf[RTL_TXDESC_SIZE + 512];

    if (!priv->dev) return -1;
    if (len + RTL_TXDESC_SIZE > sizeof(tx_buf)) return -1;

    /* Prepend TX descriptor */
    rtl_build_txdesc(tx_buf, len);
    memcpy(tx_buf + RTL_TXDESC_SIZE, data, len);

    int rc = libusb_bulk_transfer(priv->dev, RTL_EP_BULK_OUT,
        tx_buf, (int)(RTL_TXDESC_SIZE + len), &actual, RTL_USB_TIMEOUT);
    if (rc != 0) {
        static int tx_err_count = 0;
        if (tx_err_count < 3) {
            fprintf(stderr, "rtl_tx_frame: %s\n", libusb_error_name(rc));
            tx_err_count++;
        }
        return -1;
    }
    return (int)len;
}

/*
 * RTL8812A/8821A RX descriptor is 24 bytes prepended to each received frame.
 * DWORD 0[13:0] = pktlen (frame length excluding RX desc)
 * DWORD 5[7:0] = RSSI (for signal strength)
 */
#define RTL_RXDESC_SIZE 24

static int rtl_rx_frame(radio_t *r, uint8_t *buf, size_t max_len, int timeout_ms)
{
    rtl8811au_priv_t *priv = (rtl8811au_priv_t *)r->priv;
    int actual = 0;
    uint8_t rx_buf[RTL_RXDESC_SIZE + 2048];

    if (!priv->dev) return -1;

    int rc = libusb_bulk_transfer(priv->dev, RTL_EP_BULK_IN,
        rx_buf, (int)sizeof(rx_buf), &actual,
        (unsigned int)(timeout_ms > 0 ? timeout_ms : RTL_USB_TIMEOUT));

    if (rc == LIBUSB_ERROR_TIMEOUT) {
        return 0;  /* timeout — no data */
    }
    if (rc != 0) {
        static int rx_err_count = 0;
        if (rx_err_count < 3) {
            fprintf(stderr, "rtl_rx_frame: %s\n", libusb_error_name(rc));
            rx_err_count++;
            if (rx_err_count == 3)
                fprintf(stderr, "  (suppressing further RX errors)\n");
        }
        return -1;
    }

    /* Need at least the RX descriptor */
    if (actual <= RTL_RXDESC_SIZE)
        return 0;

    /* Extract packet length from RX descriptor DWORD 0[13:0] */
    uint16_t pktlen = (uint16_t)(rx_buf[0] | (rx_buf[1] << 8)) & 0x3FFF;
    if (pktlen == 0 || pktlen > (uint16_t)(actual - RTL_RXDESC_SIZE))
        return 0;

    /* Copy frame data (after RX descriptor) to caller's buffer */
    size_t copy_len = pktlen;
    if (copy_len > max_len)
        copy_len = max_len;
    memcpy(buf, rx_buf + RTL_RXDESC_SIZE, copy_len);

    return (int)copy_len;
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
