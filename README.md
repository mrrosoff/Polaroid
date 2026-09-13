# Polaroid

A battery-powered digital Polaroid. A 4" six-color e-ink panel that lives on a fridge, cycles
through photos, and syncs new ones when you shake it. Runs about a month per charge. Built as a
wedding gift.

## How it works

The server pre-renders every photo into a raw framebuffer matching the panel's resolution and
palette exactly, so the device never decodes, dithers or resizes. It wakes, streams a file from
flash to the panel, and sleeps.

```
 photo.jpg ──▶ [ backend ]                      [ device ]
                 crop 400×600 full-bleed          wake (timer or shake)
                 Polaroid film curve              read index from RTC memory
                 Floyd-Steinberg → 6 inks         stream .bin from LittleFS → SPI
                 pack 4bpp → 120,000 B            deep sleep
                 store .bin + preview.png
```

The device always pulls, and the server never reaches back. That is what lets it sleep behind a NAT
on someone else's WiFi for months.

Every wake renders, and some also sync. A shake, a cold boot, or the daily interval coming due will
each do it. The manifest is newest-first, so index 0 is the newest photo. A shake, a cold boot, and
a sync that deleted photos all jump to index 0, and every other wake advances by one and wraps.

Two screens are not photos. Below 5% the panel stops refreshing and shows **CHARGE ME**, clearing
once above 25%, and the gap stops a cell on the threshold swapping every hour. An empty library
shows **ADD PHOTOS**, so a device that has never synced doesn't look broken. The battery card wins
when both apply.

Failure means "keep showing what's already there". A sync that fails falls through to a normal
render, so the panel never goes blank because the network was unhappy.

## Build and flash

```bash
cd firmware
pio run -t upload    # the shipping firmware
pio test -e native   # 45 tests, no hardware needed
```

There is one firmware environment. `logf()` writes to USB serial when a host is attached and
returns immediately when one is not, so bench and battery runs are the same binary.

Deep sleep drops USB, so a sleeping board can only be reflashed by holding BOOT through a reset, or
by catching the few seconds it is awake.

Copy `firmware/include/Secrets.h.example` to `Secrets.h` (gitignored) for the device credential and
WiFi networks. The strongest listed network in range wins, so both homes can be listed.

## Hardware

A **Seeed XIAO ESP32S3**, chosen because it charges the cell over the same USB-C you flash through,
has no always-on power LED, and is designed for a 14 µA deep sleep. Its 8 MB costs 50 photos
against a 16 MB board's 123, which is the trade.

The panel is a **Waveshare 4" E Ink Spectra 6 (E6)** with its HAT+ driver board, `EPD_4in0e`.
Native 400 × 600 portrait, 4 bpp, two pixels per byte, which works out to a fixed 120,000 bytes
per frame. Six inks, no grays and no blends. A full refresh takes 15–35 s, measured around 21 s on
this build.

| | black | white | yellow | red | blue | green |
| --- | --- | --- | --- | --- | --- | --- |
| nibble | `0x0` | `0x1` | `0x2` | `0x3` | `0x5` | `0x6` |

Also inside are an Adafruit #2011 2000 mAh cell and an Adafruit #2809 LIS3DH breakout. Orient the
LIS3DH so its X axis lies in the plane of the fridge door, because that is the axis the `Config.h`
thresholds are tuned against. The case is 71.6 × 118.8 × 22.7 mm, its footprint set by the driver
board.

### Wiring

The XIAO breaks out exactly eleven GPIO and this design needs exactly eleven. Every wake source
must be on GPIO0–21 (the RTC domain) and the battery divider must be on ADC1 (the ADC that still
answers while WiFi is up). `D6`/`D7` are GPIO43/44, outside both, so they take the two signals
needing neither.

| Panel | XIAO | GPIO | | LIS3DH | XIAO | GPIO |
| --- | --- | --- | --- | --- | --- | --- |
| `SCK` | D6 | 43 | | `SCL` | D3 | 4 |
| `MOSI` | D7 | 44 | | `SDA` | D2 | 3 |
| `CS` | D10 | 9 | | `INT1` | D1 | 2 |
| `DC` | D9 | 8 | | `3Vo` | 3V3 | |
| `RST` | D8 | 7 | | `GND` | GND | |
| `BUSY` | D5 | 6 | | | | |
| `PWR` | D4 | 5 | | | | |

The cell goes to `B+`/`B-` on the XIAO's underside, sensed through a 2 × 1 MΩ divider into `D0`
(GPIO1, ADC1_CH0). Tests assert no pin is used twice, that `INT1` is RTC-capable, and that battery
sense is on ADC1, so a mistake here fails on your laptop.

Two things will cost you the battery if you skip them. **Gate the panel through `D4`**, because the
E6 driver board's regulator idles in the hundreds of µA even after the panel sleeps, roughly halving
runtime. **Power the LIS3DH at `3Vo`**, bypassing the breakout's LDO and its 29 µA quiescent draw.

## Power

Measured at about 2.2 mA, which is roughly a month per charge.

Nothing the firmware *does* moves that number. Twenty-four refreshes a day, the syncs and the
filesystem together change the discharge rate by less than the measurement noise, so lengthening
`REFRESH_INTERVAL_SECONDS` buys nothing. What the firmware *leaves behind* was most of the drain.
The panel board stays powered while we sleep and pulls its inputs up to its own VCC, so holding the
six data and control lines at 0 V sank current through those pull-ups continuously. Leaving them
high-impedance instead cut about 1.9 mA.

The ~2 mA that remains is the driver board's own quiescent draw on a rail that never turns off.
Getting past it means hardware, namely a high-side P-MOSFET on its VCC rather than a GPIO, since the
board pulls ~45 mA during a refresh.

**Measure it with the on-flash log, never over USB.** The ADC divider sits on the battery terminal,
so a board on a charger reads the charger. Every wake appends `boot,rtc_ms,mv,wake,host` to
`/vlog.csv`. Plug in and shake to dump it, or send `c` to clear. Fit a slope through the `host=0`
rows ordered by `rtc_ms`, and compare slopes only within the same voltage window, since a LiPo's mV
per mAh changes across the curve.

## Protocol

Base URL `https://api.maxrosoff.com/polaroid`. Device requests carry a bearer token of the form
`<deviceId>.<secret>`, both in `Secrets.h`. The API stores only the secret's SHA-256 alongside the
id in the `website-devices` table. It is a static secret rather than a JWT because the device has no
clock worth trusting an `exp` against.

`GET /photos` returns `{ id, hash, uploadedAt, previewUrl }`, newest first and uncapped. The device
keeps the newest `MAX_PHOTOS` in that order and diffs against its local manifest for fetch, delete
and keep sets. Only fetch costs bandwidth.

`POST /photo` with `{ "id": ... }` returns exactly 120,000 bytes. It **must** send
`Accept: application/octet-stream`, or API Gateway returns 160,000 base64 characters instead. It
honours the Lambda's `isBase64Encoded` only when Accept matches the API's `binaryMediaTypes`.
Success is the file being exactly 120,000 bytes once closed, so a truncated body, a 404 and a
dropped connection all land as a short file and retry next sync.

`POST /upload` and `POST /remove` sit behind the site's passkey auth, and the device never calls
them.

There is no database for photos. The list is a `ListObjectsV2` whose Key, ETag and LastModified are
exactly the id, hash and timestamp a table would hold.

## Where the code lives

This repo is the device. The service lives in
[Personal-Website](https://github.com/mrrosoff/Personal-Website), which holds the image pipeline
and four Lambda handlers under `api/endpoints/polaroid/`, plus the upload page at
`maxrosoff.com/polaroid`.
