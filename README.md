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

**Measured: about 2.2 mA, roughly a month per charge.** The frame logs its own battery hourly,
on battery, and the discharge is read by fitting a slope through those samples. 176 clean
points over 175 h give 0.725 ± 0.013 mV/h. The design budget below predicts 162 days; it is
kept because the gap between it and reality is the interesting part.

| Line item | Current | Duration | Per day |
| --- | ---: | ---: | ---: |
| Deep sleep (MCU, LIS3DH, gated panel) | 40 µA | 24 h | 0.96 mAh |
| Panel refresh | 45 mA | 24 × 30 s | 9.00 mAh |
| Daily sync | 120 mA | 10 s | 0.33 mAh |
| Shake sync, about 3 a week | 120 mA | 15 s | 0.21 mAh |
| Battery divider | 1.65 µA | 24 h | 0.04 mAh |
| | | design total | 10.5 mAh/day |
| | | **measured** | **~52 mAh/day** |

**Nothing the firmware *does* is measurable.** A diagnostic build that boots, reads the battery
and sleeps — no radio, no panel rail, no refresh — discharges at the same rate as the full
firmware, 1.648 ± 0.410 against 1.698 ± 0.043 mV/h. Twenty-four refreshes a day, the syncs and
the filesystem together move the rate by less than the noise. Lengthening
`REFRESH_INTERVAL_SECONDS` or skipping refreshes buys nothing.

**What the firmware *leaves behind* is measurable, and it was most of the drain.** The panel
board's VCC is wired to the 3V3 rail — `PIN_EPD_PWR` drives a switch on the board, not its
supply — so the board is powered while we sleep and pulls its inputs up to its own VCC. Holding
the six data and control lines at 0 V sank current through those pull-ups continuously. Over
the same 4127–4210 mV window:

| panel pins in deep sleep | slope | |
| --- | ---: | --- |
| driven low and held | 1.698 ± 0.043 mV/h | ~5.1 mA |
| high-impedance | 1.080 ± 0.038 mV/h | ~3.2 mA |

A 36% reduction, 10.8 sigma, about 1.9 mA — six lines through roughly 9 kΩ. Compare slopes only
over the same voltage window: a LiPo's mV per mAh changes across the curve, and the same
current reads as a shallower slope once the cell reaches its plateau.

About 2 mA still remains against a ~100 µA ideal. That is the board's own quiescent draw on a
rail that never turns off, and reaching it means hardware — a high-side P-MOSFET on its VCC
rather than a GPIO, since the board pulls ~45 mA during a refresh.

**Measure it with the on-flash log, never over USB.** The ADC divider sits on the battery
terminal, so a terminal on a charger reads the charger: three samples eleven seconds apart once
read 4229, 4147 and 4143 mV, and the low one was the only one taken with the cable out — an
86 mV spread against 4.8 mV of ADC noise. Every wake appends `boot,rtc_ms,mv,wake,host` to
`/vlog.csv`; plug in and shake to dump it, send `c` to clear.

Fit a slope through the `host=0` rows ordered by `rtc_ms`. Do not difference two endpoints — a
single reading carries ~4.8 mV of noise, while a day of hourly points resolves the drain to
about ±0.15 mA. Order by `rtc_ms`, not `bootCount`: a reflash resets the boot counter while the
RTC clock keeps running, so grouping by boot splices unrelated stretches together.

The 40 µA sleep line was never verified, and it assumes the RTC peripheral domain is off, which
it is not — `ext0` runs there, so powering it down silently disables shake-to-wake.

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
