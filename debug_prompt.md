# RTL8811AU USB Radio Debug — Need Help

I'm building a standalone mesh chat app (WiChat) that talks directly to an RTL8811AU USB WiFi dongle via libusb/WinUSB on Windows. No kernel driver — pure userspace USB control.

## Hardware
- **Dongle:** Realtek RTL8811AU (USB VID=0x0bda PID=0x0811)
- **Chip:** RTL8821AU (same silicon, different marketing name)
- **USB endpoints (enumerated from device):**
  - EP 0x84 IN BULK (maxpkt=512)
  - EP 0x05 OUT BULK (maxpkt=512)
  - EP 0x06 OUT BULK (maxpkt=512)
  - EP 0x87 IN INT (maxpkt=64)
  - EP 0x08 OUT BULK (maxpkt=512)
  - EP 0x09 OUT BULK (maxpkt=512)

## What works
- `libusb_open_device_with_vid_pid()` finds the device
- `libusb_claim_interface(dev, 0)` succeeds
- Register reads via USB vendor control transfers work (tested reading multiple registers)
- Firmware is detected as already loaded (MCUFWDL_RDY bit set — the Windows Realtek driver loaded it before Zadig rebind to WinUSB)

## Current chip state after USB claim (no other init)
```
MCUFWDL (reg 0x0080) = 0x00070007
  - Byte 0: 0x07 = MCUFWDL_EN(1) | MCUFWDL_RDY(1) | FWDL_ChkSum_rpt(1)
  - Byte 2: 0x07 = page bits from firmware upload

REG_CR (reg 0x0100) = 0x000003ff (MAC/DMA/WMAC blocks appear enabled)

MAC @0x0050: 00:00:0e:80:00:00 (garbage — not a real MAC)
MAC @0x0610: 00:00:00:00:00:00
MAC @0x0700: 00:00:00:00:00:00
```

## What does NOT work

### TX — LIBUSB_ERROR_TIMEOUT on bulk OUT
```c
// Building a 40-byte TX descriptor (from Linux driver rtl8812a_xmit.h):
// DWORD 0: pktlen | offset=40 | bmc=1 | ls=1 | fs=1
// DWORD 1: qsel=0x12 (MGNT queue)
// DWORD 4: userate=1 | datarate=0 (1Mbps CCK)
uint8_t tx_buf[40 + frame_len];
build_txdesc(tx_buf, frame_len);
memcpy(tx_buf + 40, frame_data, frame_len);

// This always times out:
libusb_bulk_transfer(dev, 0x05, tx_buf, 40 + frame_len, &actual, 1000);
// Returns LIBUSB_ERROR_TIMEOUT
```

### RX — zero frames received
```c
// 5-second listen on bulk IN:
libusb_bulk_transfer(dev, 0x84, rx_buf, 2048, &actual, 100);
// Always times out — never receives any data
```

## What I've tried
1. **Power-on sequence** — ported RTL8821A CARDEMU_TO_ACT from Linux driver (Hal8821APwrSeq.h). All register writes succeed but doesn't help TX/RX.
2. **Firmware upload** — successfully uploads all 8 pages (30,880 bytes) in 196-byte chunks via control transfers. But 8051 WINTINI_RDY (bit 6 of reg 0x0080) never sets after reset.
3. **Skipping firmware** — since MCUFWDL_RDY is already set (Realtek driver loaded it), skip upload to preserve working state. No change.
4. **MAC TRX enable** — wrote 0xFF to reg 0x0100, 0x03 to reg 0x0101, cleared TX pause at 0x0522. No change.
5. **Monitor mode** — wrote 0xFFFFFFFF to RX filter regs 0x0608 and 0x060C. No change.
6. **TX descriptor** — prepended 40-byte RTL8812A TX descriptor with pktlen, offset, bmc, qsel, userate, datarate. Still times out.

## Questions
1. Is the bulk OUT timeout because the TX DMA queue isn't initialized? The Linux driver does extensive queue setup (InitQueueReservedPage, InitTxBufferBoundary, InitQueuePriority, InitPageBoundary, InitTransferPageSize). Do I need all of that, or is there a minimal set?

2. Why does bulk IN receive nothing? If the chip was working under the Realtek driver and firmware is loaded, shouldn't the RX path be active? Do I need to send a specific H2C (host-to-chip) command to enable RX?

3. The MAC address at reg 0x0050 is garbage. Where does the RTL8821A actually store its MAC for reading? EFUSE autoload to which register?

4. Could the chip be in a sleep/suspend state after the Realtek driver unloaded? Is there a wake-up register I need to hit?

5. Are there any known issues with RTL8811AU and WinUSB/libusb on Windows that I should know about?

## Reference
The Linux driver source I'm working from: https://github.com/aircrack-ng/rtl8812au
Key files:
- `hal/rtl8812a/usb/usb_halinit.c` — full USB HAL init
- `hal/rtl8812a/rtl8812a_hal_init.c` — power on, firmware, 8051 reset
- `include/Hal8821APwrSeq.h` — power sequence tables
- `include/rtl8812a_xmit.h` — TX descriptor format
- `hal/rtl8812a/hal8821a_fw.c` — firmware blob

I want the MINIMUM set of register writes needed to get TX and RX working via bulk USB endpoints on an already-firmware-loaded RTL8821A chip. Not the full driver — just enough to send and receive raw 802.11 frames.
