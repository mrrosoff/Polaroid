# Polaroid

A battery-powered digital Polaroid. A 4" six-color e-ink panel that lives on a fridge, cycles
through photos, and syncs new ones when you shake it. Runs about five months per charge. Built as a
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

The device always pulls; the server never reaches back. That is what lets it sleep behind a NAT on
someone else's WiFi for months.

Every wake renders, and some also sync: a shake, a cold boot, or the daily interval coming due.
The manifest is newest-first, so index 0 is the newest photo. A shake, a cold boot, and a sync that
deleted photos all jump to index 0; every other wake advances by one and wraps.

Two screens are not photos. Below 5% the panel stops refreshing and shows **CHARGE ME**, clearing
once above 25% — the gap stops a cell on the threshold swapping every hour. An empty library shows
**ADD PHOTOS**, so a device that has never synced doesn't look broken. The battery card wins when
both apply.

Failure means "keep showing what's already there". A sync that fails falls through to a normal
render; the panel never goes blank because the network was unhappy.

## Build and flash

```bash
cd firmware
pio run -t upload    # the shipping firmware
pio test -e native   # 45 tests, no hardware needed
```

One firmware environment: `logf()` writes to USB serial when a host is attached and returns
immediately when one is not, so bench and battery runs are the same binary.

Deep sleep drops USB, so a sleeping board can only be reflashed by holding BOOT through a reset, or
by catching the few seconds it is awake.

Copy `firmware/include/Secrets.h.example` to `Secrets.h` (gitignored) for the device credential and
WiFi networks. The strongest listed network in range wins, so both homes can be listed.

## Hardware

A **Seeed XIAO ESP32S3**, chosen because it charges the cell over the same USB-C you flash through,
has no always-on power LED, and is designed for a 14 µA deep sleep. Its 8 MB costs 50 photos
against a 16 MB board's 123, which is the trade.

The panel is a **Waveshare 4" E Ink Spectra 6 (E6)** with its HAT+ driver board, `EPD_4in0e`.
Native 400 × 600 portrait, 4 bpp, two pixels per byte: 120,000 bytes per frame, fixed. Six inks,
no grays and no blends. A full refresh takes 15–35 s, measured around 21 s on this build.

| | black | white | yellow | red | blue | green |
| --- | --- | --- | --- | --- | --- | --- |
| nibble | `0x0` | `0x1` | `0x2` | `0x3` | `0x5` | `0x6` |

Also inside: an Adafruit #2011 2000 mAh cell and an Adafruit #2809 LIS3DH breakout. Orient the
LIS3DH so its X axis lies in the plane of the fridge door — that is the axis the `Config.h`
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

Two things will cost you the battery if you skip them. **Gate the panel through `D4`** — the E6
driver board's regulator idles in the hundreds of µA even after the panel sleeps, roughly halving
runtime. **Power the LIS3DH at `3Vo`**, bypassing the breakout's LDO and its 29 µA quiescent draw.

### Flash capacity

Framebuffers are exactly 120,000 bytes and do not compress. The no-OTA table in
`firmware/partitions.csv` leaves 6.4 MB of LittleFS, which fits 53 frames. The cap is 50, because
`downloadPhoto` stages a full temp file before renaming it over the old one — a device holding 53
could never replace a photo. `MAX_PHOTOS` lives in both `firmware/include/Config.h` and the
service's `api/common.ts`, and they must agree.

PSRAM is left uninitialized. Photos live in flash, the framebuffer streams 512 bytes at a time, so
there is nothing large to allocate and an initialized die would only add sleep current.

## Power

**Sleep current is not the thing to optimize — the panel refresh is.** At an hourly refresh,
pushing pixels costs about nine times what sleeping costs. Every µA out of deep sleep buys half a
day; every refresh not taken buys about seven. Lengthen `REFRESH_INTERVAL_SECONDS` before hunting
for leakage.

| Line item | Current | Duration | Per day |
| --- | ---: | ---: | ---: |
| Deep sleep (MCU, LIS3DH, gated panel) | 40 µA | 24 h | 0.96 mAh |
| Panel refresh | 45 mA | 24 × 30 s | 9.00 mAh |
| Daily sync | 120 mA | 10 s | 0.33 mAh |
| Shake sync, about 3 a week | 120 mA | 15 s | 0.21 mAh |
| Battery divider | 1.65 µA | 24 h | 0.04 mAh |
| | | total | 10.5 mAh/day |

1700 mAh usable gives about 162 days.

**The table assumes every panel pin is held low through deep sleep, which is a recent fix.**
Cutting the gate is not enough by itself. A GPIO stops being driven the moment the chip sleeps
unless explicitly held, so the six data and control lines float — and a floating pin at the driver
board's input forward-biases its ESD diodes and feeds the board's rail through that input,
powering the panel through the back door the gate was closed to prevent.

Measured, three runs of about 18 hours each from 4.22 V: never touching the panel cost **20 mV**;
one power-up and refresh, then nothing for the rest of the run, cost **140 mV**. The refresh itself
is worth 0.26 mAh, so seven-eighths of that was the board being fed all night through its own
inputs. This is why the first assembled build ran flat in days rather than months.

The 40 µA line is still unverified, and it assumes the RTC peripheral domain is off, which it is
not — `ext0` runs there, so powering it down silently disables shake-to-wake. Put a meter on the
rail before trusting it.

Firmware rules that protect the budget each carry a `POWER:` comment at their site in the source.

## Protocol

Base URL `https://api.maxrosoff.com/polaroid`. Device requests carry a bearer token of the form
`<deviceId>.<secret>`, both in `Secrets.h`. The API stores only the secret's SHA-256 alongside the
id in the `website-devices` table; the id makes the lookup a single keyed read rather than a scan.
A static secret rather than a JWT because the device has no clock worth trusting an `exp` against.

`GET /photos` returns `{ id, hash, uploadedAt, previewUrl }`, newest first and uncapped. The device
keeps the newest `MAX_PHOTOS` in that order and diffs against its local manifest for fetch, delete
and keep sets. Only fetch costs bandwidth.

`POST /photo` with `{ "id": ... }` returns exactly 120,000 bytes. It **must** send
`Accept: application/octet-stream`, or API Gateway returns 160,000 base64 characters instead — it
honours the Lambda's `isBase64Encoded` only when Accept matches the API's `binaryMediaTypes`.
Success is the file being exactly 120,000 bytes once closed, so a truncated body, a 404 and a
dropped connection all land as a short file and retry next sync.

```bash
curl -H "Authorization: Bearer $POLAROID_DEVICE_ID.$POLAROID_DEVICE_SECRET" \
     -H "Content-Type: application/json" \
     -H "Accept: application/octet-stream" \
     -d '{"id":"<an id from /photos>"}' \
     https://api.maxrosoff.com/polaroid/photo > frame.bin
```

`POST /upload` and `POST /remove` sit behind the site's passkey auth; the device never calls them.

There is no database for photos. The list is a `ListObjectsV2` whose Key, ETag and LastModified are
exactly the id, hash and timestamp a table would hold.

## Where the code lives

This repo is the device. The service lives in
[Personal-Website](https://github.com/mrrosoff/Personal-Website): the image pipeline and four
Lambda handlers under `api/endpoints/polaroid/`, and the upload page at `maxrosoff.com/polaroid`.
The enclosure is in `enclosure/`, where checked-in STLs and previews are outputs of `render.sh`,
not sources.
