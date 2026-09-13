# Yard Birds — live BirdNET-Go display for the Cheap Yellow Display

A dedicated desk display for [BirdNET-Go](https://github.com/tphakala/birdnet-go).
It shows a live list of the birds your microphone has identified today, and when a
**new** species is identified it fills the screen with that bird's photo for three
seconds before returning to the list.

Built for the **ESP32-2432S028** ("Cheap Yellow Display") — a ~$15 2.8" 320×240
ILI9341 touchscreen with an ESP32 attached.

```
┌────────────────────────┐
│ Yard Birds         live│
│ species     today   id │
│ Blue Jay      379   93%│
│ Northern Card…221   95%│
│ Cedar Waxwing 148   91%│
│ American Crow  19   92%│
│ …                      │
└────────────────────────┘
        ↓ new identification
┌────────────────────────┐
│                        │
│    [ full-screen       │
│      bird photo ]      │
│                        │
│      Blue Jay          │
│   379 today - 93% id   │
└────────────────────────┘
```

## Requirements

- **BirdNET-Go** running and reachable on your LAN. It must be listening on a
  port your WiFi network can reach (default `8085`).
- An **ESP32-2432S028** board and a USB data cable (plenty of USB cables are
  charge-only — if the board doesn't appear, try another cable first).
- No SD card is needed.

## Building and flashing

PlatformIO:

```bash
pio run -e cyd -t upload
```

Or build and flash separately (useful when the board is on a different machine
than your toolchain):

```bash
pio run -e cyd                                  # produces .pio/build/cyd/firmware.bin
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  write-flash -z --flash-mode dio --flash-freq 40m --flash-size 4MB \
  0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```

`boot_app0.bin` lives in your PlatformIO framework package, not the build output:

```
~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
```

## First-time setup

On first boot the display opens a WiFi captive portal:

1. On your phone, join the WiFi network **`CYD-Birds-Setup`**.
2. A setup page should open automatically. If it doesn't, browse to
   **`http://192.168.4.1`**.
3. Enter your **WiFi network and password**.
4. Enter your **BirdNET-Go host** (an IP address like `192.168.1.50` or a
   hostname) and **port** (default `8085`).
5. Save. The display connects and starts showing birds.

Both the WiFi credentials and the server address are stored in the ESP32's NVS
flash. **This is a one-time step** — the portal will not appear again on a normal
boot. It only reopens if WiFi fails, or if BirdNET-Go can't be reached (which is
how you correct a wrong address).

To start over from scratch, erase the flash first:

```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 erase-flash
```

## How it works

**Data sources** (all from BirdNET-Go's HTTP API):

| Purpose | Endpoint |
|---|---|
| The list | `GET /api/v2/analytics/species/daily` — name, count today, max confidence |
| New IDs | `GET /api/v2/detections/stream` — Server-Sent Events, fires on identification |
| Photos | `GET /api/v2/media/image/<scientificName>` — 320×240 JPEG |

The list is refreshed every 5 minutes, and the SSE stream is held open
continuously. When an identification arrives, the list is re-pulled (so the count
is current) and the hero photo is shown for 3 seconds.

Repeat identifications of the same species within 45 seconds are suppressed, so
a bird calling repeatedly doesn't strobe the screen.

**No SD card.** Species photos are cached in the ESP32's internal LittleFS
partition (~896 KB = roughly 110 photos at ~8 KB each). Each species is
downloaded once and then renders instantly, including while offline.

## Hardware notes

Two traps that cost real debugging time, in case you fork this:

**Backlight.** `TFT_eSPI` does not drive the backlight unless told to. The pin is
GPIO 21 and the firmware sets it explicitly — otherwise the board runs perfectly
into a dark screen.

**JPEG output format.** The vendored `TJpg_Decoder` is configured with
`JD_FORMAT 0`, which in TJpgDec R0.03 means **RGB888 (3 bytes per pixel)**, not
the RGB565 you might expect. The decode callback converts to RGB565 and
byte-swaps by hand. Feeding the buffer straight to `pushImage` produces garbled
photos with correct-looking text.

Touch is not used by this project. Note that the XPT2046 sits on
**different pins** to the display (25/39/32/33/36 vs 13/12/14/15), so if you add
touch you must bit-bang it — the ESP32's two user SPI peripherals are already
used by the TFT and SD.

## Configuration

Defaults live in `src/config.h` and are overridden at runtime by the setup portal:

| Setting | Default | Notes |
|---|---|---|
| `BIRDNET_DEFAULT_HOST` | `birdnet.local` | Overridden by the portal |
| `BIRDNET_DEFAULT_PORT` | `8085` | |
| `LIST_ROWS` | `12` | Rows on screen |
| `HERO_HOLD_MS` | `3000` | How long the photo stays up |
| `HERO_DEDUPE_MS` | `45000` | Suppress repeats of the same species |
| `LIST_REFRESH_MS` | `300000` | List refresh interval |

There is no API key support — the project assumes BirdNET-Go is on a trusted
LAN with authentication disabled. If your instance requires auth, the two
`HTTPClient` call sites in `src/birdnet.cpp` need a header added.

## Credits and licensing

- **TJpg_Decoder** by [Bodmer](https://github.com/Bodmer/TJpg_Decoder), wrapping
  **TJpgDec** by ChaN (R0.03, "free software ... education, research and
  commercial developments", no warranty). Vendored in `lib/` because the
  configuration differs from the upstream library default.
- **TFT_eSPI** by Bodmer — pulled via PlatformIO.
- **WiFiManager** by tzapu — MIT.
- **ArduinoJson** by Benoît Blanchon — MIT.

Add your own `LICENSE` file before publishing.
