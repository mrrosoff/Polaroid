# Polaroid

A battery-powered digital Polaroid. A 4" six-color e-ink panel that lives on a fridge, cycles
through photos, and syncs new ones when you shake it, like waiting for a real Polaroid to develop.
Runs about five months per charge. Built as a wedding gift.

## How it works

The server does all the heavy image work so the device can stay dumb and instant. Every uploaded
photo is pre-rendered into a raw framebuffer matching the panel's resolution and palette exactly:
Polaroid film emulation, then Floyd-Steinberg dithering into the six inks, then packed 4bpp. The
device never decodes a JPEG, never dithers, never resizes. On wake it reads an index from RTC
memory, streams a file from flash straight to the SPI panel, and goes back to sleep.

```
 photo.jpg ──▶ [ backend ]                      [ device ]
                 crop 400×600 full-bleed          wake (timer or shake)
                 Polaroid film curve              read index from RTC memory
                 Floyd-Steinberg → 6 inks         stream .bin from LittleFS → SPI
                 pack 4bpp → 120,000 B            deep sleep
                 store .bin + preview.png
```

The device pulls and the server never reaches back, which is what lets the thing sleep behind a NAT
on someone else's WiFi for months without anyone thinking about it.

## What a wake does

Every wake either syncs or it does not, and then it renders. A shake syncs, because that is the
gesture. A cold boot syncs, because it has nothing to show. Otherwise the hourly timer advances to
the next photo and pushes it to the panel with no network at all, except once a day when the sync
interval comes due. Motion that is not a shake is treated as spurious and goes straight back to
sleep without spending a refresh.

Shake detection is the LIS3DH's activity threshold, high-pass filtered so gravity does not hold it
tripped at whatever angle the frame hangs. The interrupt is latched, and reading `INT1_SRC` on wake
is what releases it.

Networks live in `firmware/include/Secrets.h`, which is gitignored, and the strongest one in range
wins, so both homes can be listed. There is no captive portal.

Failure always means "keep showing what's already there". A sync that cannot reach the server, or
times out, falls through to a normal render. There is no state where the panel goes blank because
the network was unhappy.

Photos deleted on the server disappear from the frame on the next sync, since anything in the local
manifest that the server no longer lists gets unlinked before new downloads start. A sweep after
each sync also removes framebuffers the manifest does not mention, which is what a crash between
writing a photo and saving the manifest leaves behind.

## Build and flash

```bash
cd firmware
pio run -t upload              # the shipping firmware
pio run -e bringup -t upload   # same, plus serial logging, and never sleeps
pio test -e native             # 46 tests, no hardware needed
```

Two environments exist for bench work. `bringup` is the one to use while developing, because deep
sleep drops USB and a sleeping board can only be reflashed by holding BOOT through a reset.
`sleep-test` logs but does sleep, which is the only way to watch the wake path.

The image pipeline in Personal-Website has no tests yet.

## Hardware

Built around a **Seeed XIAO ESP32S3**. It costs 50 photos against the 123 a 16 MB board would give,
and it is worth it. The XIAO charges the cell over the same USB-C you flash through, which deletes
a part from a footprint with no room for spare parts. It has no always-on power LED, where the
generic SuperMini wires one across the 3.3 V rail at 1 to 3 mA, fifty times the entire rest of the
sleep budget. Seeed also publishes a deep-sleep figure of 14 µA and designed for it.

The panel is a **Waveshare 4" E Ink Spectra 6 (E6)** with its HAT+ driver board, `EPD_4in0e`.
Native 400 × 600 portrait, four bits per pixel, two pixels per byte, so 120,000 bytes per frame,
fixed. Six inks and nothing in between, no grays and no blends:

| | black | white | yellow | red | blue | green |
| --- | --- | --- | --- | --- | --- | --- |
| nibble | `0x0` | `0x1` | `0x2` | `0x3` | `0x5` | `0x6` |

A full refresh takes 15 to 35 seconds and visibly flashes through color planes. Measured on this
build it lands around 21 seconds.

Also in the case: an Adafruit #2011 2000 mAh cell (60 × 36 × 7 mm, and the 7 mm is the point, since
thickness is the sum of glass, board and battery) and an Adafruit #2809 LIS3DH breakout. The
finished case is 71.6 × 118.8 × 22.7 mm; the driver board's 101 × 68 mm sets the footprint, so
battery, MCU and accelerometer all sit behind it in a single layer rather than beside it.

Orient the LIS3DH so its X axis lies in the plane of the fridge door. That is the axis a shake
swings along, and what the thresholds in `Config.h` are tuned against.

### Wiring

The XIAO breaks out exactly eleven GPIO and this design needs exactly eleven, so there is no slack
for a status LED, a reset button, or a spare line to probe. Two constraints drove the assignment:
every wake source must be on GPIO0 to 21, the S3's RTC domain, and the battery divider must be on
ADC1, the ADC that still answers while WiFi is up. `D6` and `D7` are GPIO43/44, outside the RTC
domain and with no ADC, so they take the two signals needing neither.

| Panel | XIAO | GPIO | | LIS3DH | XIAO | GPIO |
| --- | --- | --- | --- | --- | --- | --- |
| `SCK` | D6 | 43 | | `SCL` | D3 | 4 |
| `MOSI` | D7 | 44 | | `SDA` | D2 | 3 |
| `CS` | D10 | 9 | | `INT1` | D1 | 2 |
| `DC` | D9 | 8 | | `3Vo` | 3V3 | |
| `RST` | D8 | 7 | | `GND` | GND | |
| `BUSY` | D5 | 6 | | | | |
| `PWR` | D4 | 5 | | | | |

The cell goes to the `B+` and `B-` pads on the XIAO's underside. Sense it through a 2 × 1 MΩ
divider off `B+` into `D0` (GPIO1, ADC1_CH0). One megohm per leg costs 1.65 µA continuously, which
is the right trade against the leakage and part count of a MOSFET switch. Tests assert that no pin
is used twice, that `INT1` is RTC-capable, and that battery sense is on ADC1, so a mistake in this
table fails on your laptop rather than on a board glued behind a panel.

Two things will cost you the battery if you skip them. **Gate the panel through `D4`**, because the
E6 driver board's regulator idles in the hundreds of µA even after the panel is told to sleep;
wiring `VCC` straight to 3.3 V instead costs roughly half the runtime. **Power the LIS3DH at
`3Vo`**, bypassing the breakout's 3.3 V LDO, whose 29 µA quiescent draw is an order of magnitude
more than the sensor itself pulls in low-power mode.

### Flash capacity

Framebuffers are exactly 120,000 bytes and do not compress, since dithered noise is incompressible
by construction. The no-OTA table in `firmware/partitions.csv` leaves 6.4 MB of LittleFS, which
fits 53 frames. The cap is 50: `downloadPhoto` stages a full temp file before renaming it over the
old one, so a device holding 53 could never replace a photo again. `MAX_PHOTOS` lives in both
`firmware/include/Config.h` and the service's `api/common.ts` and they must agree.

PSRAM is not storage and is left uninitialized. Photos live in flash, which survives power loss.
The framebuffer streams from LittleFS to SPI 512 bytes at a time and is never held whole, so there
is nothing large to allocate, and an initialized PSRAM die adds deep-sleep current for no return.

## Power

The finding worth keeping: **sleep current is not the thing to optimize, the panel refresh is.** At
an hourly refresh, pushing pixels costs about nine times what sleeping costs. Every µA clawed out
of deep sleep buys roughly half a day; every refresh not taken buys about seven. If you need more
life out of this, lengthen `REFRESH_INTERVAL_SECONDS` before hunting for leakage.

| Line item | Current | Duration | Per day |
| --- | ---: | ---: | ---: |
| Deep sleep (MCU, LIS3DH, gated panel) | 40 µA | 24 h | 0.96 mAh |
| Panel refresh | 45 mA | 24 × 30 s | 9.00 mAh |
| Daily sync | 120 mA | 10 s | 0.33 mAh |
| Shake sync, about 3 a week | 120 mA | 15 s | 0.21 mAh |
| Battery divider | 1.65 µA | 24 h | 0.04 mAh |
| | | total | 10.5 mAh/day |

2000 mAh at 85% usable is 1700 mAh, so about 162 days. The 40 µA is the figure to distrust and has
not been measured on this build. It also assumes the RTC peripheral domain is powered down, and it
is not: `ext0` runs on that domain, so powering it off silently disables shake-to-wake. Measure the
sleep current before trusting the five-month number.

The firmware holds to a few rules, each with a `POWER:` comment at its site in the source so this
section stays auditable. Deep sleep is the only resting state, and `setup()` ends in
`sleepUntilNextEvent()` on every path including error paths. `Panel` and `Net` tear their hardware
down in destructors rather than in a function someone has to remember to call, because every early
return is otherwise a path where forgetting costs the battery. WiFi is deinited rather than merely
disconnected, since `disconnect()` alone leaves the PHY drawing about 1 mA into deep sleep. WiFi
and the panel never overlap, so the two largest draws never stack. A failed sync backs off 1 h,
2 h, 4 h up to the daily cadence, because retrying hourly through a router outage would cost more
than the entire rest of the budget. Below 5% the panel stops refreshing entirely and shows a card,
since e-ink holds its last image with no power at all.

## Protocol

Base URL `https://api.maxrosoff.com/polaroid`. Every device request carries a static bearer token,
stored in SSM at `/website/polaroid/device-secret` and baked into `firmware/include/Secrets.h`
(gitignored, copy `Secrets.h.example`). A static secret rather than a JWT because the device has no
clock worth trusting an `exp` against.

`GET /photos` returns the list, newest first and uncapped, as `{ id, hash, uploadedAt, previewUrl }`.
The device takes the newest `MAX_PHOTOS`, reverses them into display order, and diffs against its
local manifest to get fetch, delete and keep sets. Only fetch costs bandwidth.

`POST /photo` with `{ "id": ... }` returns exactly 120,000 bytes as `application/octet-stream`. The
request must send `Accept: application/octet-stream`, or API Gateway hands back 160,000 base64
characters instead of the bytes, since it only honours the Lambda's `isBase64Encoded` when the
Accept header matches the API's `binaryMediaTypes`. Success is decided by the file being exactly
120,000 bytes after it is closed, so a truncated body, a 404 or a dropped connection all land as a
short file and get retried on the next sync.

```bash
curl -H "Authorization: Bearer $POLAROID_DEVICE_TOKEN" \
     -H "Content-Type: application/json" \
     -H "Accept: application/octet-stream" \
     -d '{"id":"<an id from /photos>"}' \
     https://api.maxrosoff.com/polaroid/photo > frame.bin
```

`POST /upload` and `POST /remove` sit behind the site's passkey auth and the device never calls
them. Uploads accept JPEG, PNG or HEIC, run the pipeline, and return `{ id, previewUrl }`, where
`previewUrl` is a presigned S3 URL rather than a route so an `<img src>` can be a plain GET.

There is no database. The photo list is a `ListObjectsV2` over the bucket, whose Key, ETag and
LastModified are exactly the id, hash and timestamp a table would hold. A table would only add a
second copy of the truth that can disagree with the first, and every upload would become two writes
that can half-fail, leaving a manifest entry pointing at a framebuffer that is not there.

## Where the code lives

This repo is the device. The service lives in
[Personal-Website](https://github.com/mrrosoff/Personal-Website): the image pipeline and four
Lambda handlers under `api/endpoints/polaroid/`, and the upload page at `maxrosoff.com/polaroid`.
The enclosure is in `enclosure/`, where the checked-in STLs and previews are outputs of
`render.sh`, not sources.
