# WiChat — Off-Grid Mesh Chat

**Communicate without infrastructure.** WiChat is a mesh chat system using raw 802.11 frames and ESP-NOW Long Range. No WiFi network, no internet, no servers — just radio.

## How It Works

WiChat devices communicate by injecting and sniffing raw 802.11 data frames on a shared BSSID (`57:43:57:43:57:43`). Any WiChat device on the same channel sees every message. The mesh engine provides:

- **Multi-hop relay** — messages forward through intermediate nodes (TTL-based)
- **Deduplication** — seen message IDs are tracked to prevent loops
- **ACK/delivery status** — sent messages show pending → acked → failed
- **Node tracking** — see who's in range with callsign, RSSI, last seen
- **Beacon discovery** — periodic beacons announce presence
- **FEC (Forward Error Correction)** — WSPR codec for reliability
- **Dual transport** — raw 802.11 + ESP-NOW LR (~1km range between ESP32s)

## Components

### ESP32 CYD Firmware (`cyd/`)
Standalone chat device on Freenove ESP32-S3 3.5" Display (FNK0104N).
- Touch QWERTY keyboard
- Chat message display with delivery status
- Node list with RSSI
- Function menu (long press): channel, ping, beacon
- ST77922 QSPI display driver
- Integrated touch (TDDI, I2C)

**Build & Flash:**
```bash
cd cyd
pio run -e cyd
# Hold BOOT, press RESET, release BOOT
python -m esptool --chip esp32s3 --port COMx --baud 460800 write_flash \
  0x0 .pio/build/cyd/bootloader.bin \
  0x8000 .pio/build/cyd/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/cyd/firmware.bin
```

### ESP32 CYD 2.8" Firmware (`cyd_28/`)
Same protocol on the 2.8" Cheap Yellow Display (ESP32, ILI9341/ST7789).

### PC Client (`src/`)
Windows/Linux desktop client in C. Two radio backends:
1. **RTL8811AU USB dongle** — direct userspace USB via libusb/WinUSB (work in progress)
2. **Serial bridge** — ESP32 acts as a radio modem, PC sends frames over USB serial

### Serial Bridge (`bridge/`)
ESP32 firmware that relays WiChat frames between USB serial and WiFi. Turns any ESP32 into a radio modem for the PC client.

### CLI Module (`cli_module/`)
Serial command interface for remote control and automation. Drop into any ESP32 build. Commands for touch simulation, WiFi/BLE scanning, mesh chat, HID injection, system info. See `cli.h` for full command list.

## Pre-Built Firmware

Ready-to-flash binaries in the repo root:
| File | Board | Description |
|------|-------|-------------|
| `wichat_freenove_s3_35_merged.bin` | Freenove S3 3.5" | Flash at 0x0 with esptool |
| `wichat_cyd28_2usb_merged.bin` | CYD 2.8" 2-USB | Flash at 0x0 with esptool |
| `firmware_freenove_s3_35.bin` | Freenove S3 3.5" | App only (0x10000) |
| `firmware_cyd28_2usb_wichat.bin` | CYD 2.8" 2-USB | App only (0x10000) |

## Wire Protocol

```
[802.11 Data Frame Header (24 bytes)]
  BSSID: 57:43:57:43:57:43
[WiChat Header (14 bytes)]
  magic: 0x5743
  msg_id: uint32 (random, for dedup)
  ttl: uint8 (hop count remaining)
  type: uint8 (CHAT=1, BEACON=2, ACK=3, PING=4, PONG=5, FEC=6)
  sender: uint8[6] (MAC address)
[Payload (variable, max 236 bytes)]
  For CHAT: raw text
  For BEACON: callsign + flags + uptime
  For ACK: acked msg_id
```

## Architecture

```
┌─────────────┐     raw 802.11      ┌─────────────┐
│  ESP32 CYD  │◄───────────────────►│  ESP32 CYD  │
│  (WiChat)   │     ESP-NOW LR      │  (WiChat)   │
└─────────────┘                     └─────────────┘
       │                                   │
       │ USB serial                        │
       ▼                                   │
┌─────────────┐     raw 802.11      ┌──────┴──────┐
│  PC Client  │◄───────────────────►│  Any WiChat │
│  (bridge)   │     via bridge      │   device    │
└─────────────┘                     └─────────────┘
```

## Status

| Component | Status |
|-----------|--------|
| Core protocol (mesh.c, frame.c) | **Working** |
| ESP32 CYD 3.5" firmware | **Working** |
| ESP32 CYD 2.8" firmware | **Working** |
| ESP-NOW LR transport | **Working** |
| Raw 802.11 transport | **Working** |
| Serial bridge | **Working** |
| FEC codec (wspr.c) | **Working** |
| CLI module | **Working** |
| PC client (serial bridge backend) | **Working** |
| PC client (RTL8811AU direct USB) | **In Progress** — register access works, frame TX/RX not yet |
| Encryption | **Planned** — AES-CCM (crypto.c skeleton exists) |
| SquatchWatch integration | **Working** — runs as a tool inside SquatchWatch Toolbox Edition |

## Roadmap

- [ ] RTL8811AU direct USB radio (no kernel driver)
- [ ] AES-256-CCM message encryption
- [ ] Key exchange protocol (DH or PSK)
- [ ] File transfer over mesh
- [ ] GPS integration for location sharing
- [ ] Multi-channel hopping for range extension
- [ ] Web UI for PC client
- [ ] Android app via USB OTG + serial bridge
- [ ] Message store-and-forward (offline relay)

## License

GPL v3. See [LICENSE](LICENSE).

## Related Projects

- [SquachWatch Toolbox Edition](https://github.com/mjgodat/SquachWatch-CYD) — WiChat integrated as a pentest tool inside SquachWatch
- [SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD) — the detection platform by TalkingSasquach
- [Meshtastic](https://meshtastic.org/) — LoRa mesh (different radio, similar concept)
