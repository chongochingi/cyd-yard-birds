# Yard Birds — live BirdNET-Go display for the 2.8" Cheap Yellow Display

A dedicated desk display for [BirdNET-Go](https://github.com/tphakala/birdnet-go).
It shows a live list of the birds your microphone has identified today, and when a
**new** species is identified it fills the screen with that bird's photo for three
seconds before returning to the list.

**Built for the 2.8" CYD only** — the **ESP32-2432S028** ("Cheap Yellow Display"),
a ~$15 board with a 320×240 ILI9341 touchscreen and an ESP32 already attached.
That is the single supported target. This is not a general-purpose ESP32 or
display project: the pin assignments, panel driver, and touch wiring below are all
specific to this board, and the layout is tuned to a 240×320 portrait screen.

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

- **An ESP32-2432S028 "Cheap Yellow Display"** — the 2.8" board specifically.
- A **USB data cable**. Plenty of USB cables are charge-only; if the board never
  appears as a serial port, try another cable before debugging anything else.
- **BirdNET-Go** running and reachable on your LAN, listening on a port your WiFi
  network can reach (default `8085`).
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
5. If your BirdNET-Go has `security.basicauth` enabled, enter the **basic auth
   user and password**. Otherwise leave both blank.
6. Save. The display connects and starts showing birds.

Both the WiFi credentials and the server address are stored in the ESP32's NVS
flash. **This is a one-time step.** On a normal boot the portal does not appear at
all — the device connects silently and goes straight to the list.

The portal only reopens in three cases, and the screen says which:

| Screen | Meaning |
|---|---|
| `WiFi setup` | WiFi credentials are missing or wrong |
| `Server setup` | No server address has been saved yet (first-time setup) |
| `Server setup` | A saved server stayed unreachable for 30 seconds |

That last row is deliberately generous: if BirdNET-Go is briefly down or
restarting, the device retries quietly and shows `Can't reach server` on screen
rather than throwing a setup screen at an already-configured device. It only
offers setup after the grace period, which is also the recovery path if you
mistype the address.

### Reopening setup later

Tap the **SETUP** button in the top-right of the list screen. The portal opens
again with your current values pre-filled, so you can change the server address,
switch WiFi networks, or add credentials without erasing anything.

### Touch calibration

Touch drives only the SETUP button. On first boot the display runs a two-point
calibration — tap each red crosshair firmly — and stores it in NVS. Later boots
skip it.

**If you mis-tap during calibration**, the panel becomes unreliable and you
cannot tap your way out of it. Send `c` over the serial console within 2 seconds
of powering on to force a redo:

```bash
pio device monitor          # then press 'c' as it boots, or:
python3 -c "import serial,time; s=serial.Serial('/dev/ttyUSB0',115200); \
  s.setDTR(False); s.setRTS(True); time.sleep(0.2); s.setRTS(False); \
  [ (s.write(b'c'), time.sleep(0.05)) for _ in range(50) ]"
```

Clearing calibration does not touch your WiFi or server settings — they live in
separate NVS namespaces.

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

Three traps that cost real debugging time. All are specific to the 2.8" CYD.

**Backlight.** `TFT_eSPI` does not drive the backlight unless told to. It is on
GPIO 21 and the firmware sets it explicitly — otherwise the board runs perfectly
into a dark screen, which reads as dead hardware.

**JPEG output format.** The vendored `TJpg_Decoder` is configured with
`JD_FORMAT 0`, which in TJpgDec R0.03 means **RGB888 (3 bytes per pixel)**, not
the RGB565 you might expect. The decode callback converts to RGB565 and
byte-swaps by hand. Feeding the buffer straight to `pushImage` produces garbled
photos with correct-looking text.

**Touch is not on the display's SPI bus.** The XPT2046 sits on different pins to
the ILI9341 (25/39/32/33/36 vs 13/12/14/15), so it has to be bit-banged — the
ESP32's two user SPI peripherals are already taken by the TFT and SD. Because the
panel runs portrait here while the touch driver assumes landscape, the raw axes
are rotated; `TOUCH_SWAP_XY` in `src/touch.cpp` states that mapping explicitly.

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

There is no API key support, because BirdNET-Go doesn't use API keys. If your
instance has `security.basicauth` enabled, enter the username and password in the
setup portal and they are sent as an HTTP Basic `Authorization` header on every
request (list, images, and the event stream). Leave the username blank to send no
header at all.

## Credits and licensing

This project is MIT licensed — see [LICENSE](LICENSE).

Bundled and depended-upon libraries:

- **TJpg_Decoder** by [Bodmer](https://github.com/Bodmer/TJpg_Decoder), wrapping
  **TJpgDec** by ChaN (R0.03, "free software ... education, research and
  commercial developments", no warranty). Vendored in `lib/` because the
  configuration differs from the upstream library default.
- **TFT_eSPI** by Bodmer — pulled via PlatformIO.
- **WiFiManager** by tzapu — MIT.
- **ArduinoJson** by Benoît Blanchon — MIT.
