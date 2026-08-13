# Polaroid

A battery-powered digital Polaroid. A 4" six-color e-ink panel that lives on a fridge, quietly
cycles through photos, and syncs new ones when you shake it — like waiting for a real Polaroid to
develop. Runs about five months per charge.

Built as a wedding gift.

## How it works

The server does all the heavy image work. The device stays dumb and instant.

Every uploaded photo is pre-rendered on the backend into a raw framebuffer that exactly matches the
panel's resolution and 6-color palette — Polaroid film emulation, then Floyd–Steinberg dithering
into the six inks, then packed 4bpp. The device never decodes a JPEG, never dithers, never resizes.
On wake it reads an index from RTC memory, streams a file from flash straight to the SPI panel, and
goes back to sleep.

```
 photo.jpg ──▶ [ backend ]                      [ device ]
                 crop 400×600 full-bleed          wake (timer or shake)
                 Polaroid film curve              read index from RTC memory
                 Floyd–Steinberg → 6 inks         stream .bin from LittleFS → SPI
                 pack 4bpp → 120,000 B            deep sleep
                 store .bin + preview.png
```

## Where the code lives

This repo is the **device**. The **service** lives in
[Personal-Website](https://github.com/mrrosoff/Personal-Website), on the `polaroid` branch, because
that's where the site and its AWS stack already are.

| Path | What |
| --- | --- |
| `firmware/` | PlatformIO / Arduino-ESP32 firmware, C++20 |
| `docs/HARDWARE.md` | Bill of materials, what to order, wiring, physical fit |
| `docs/POWER.md` | Power budget and where it actually goes |
| `docs/PROTOCOL.md` | Device ⇄ server contract |

In Personal-Website:

| Path | What |
| --- | --- |
| `api/polaroid/` | image pipeline — palette, film emulation, dither, packing, frame |
| `api/endpoints/polaroid/` | six Lambda handlers behind `api.maxrosoff.com/polaroid` |
| `src/components/Polaroid.tsx` | the couple's upload page, at `maxrosoff.com/polaroid` |
| `test/polaroid*.test.ts` | vitest |

The two halves are developed independently — the firmware's tests run on your laptop with no
hardware attached.

## Quick start

Pull a rendered framebuffer down to flash by hand:

```bash
curl -H "Authorization: Bearer $POLAROID_DEVICE_TOKEN" \
     -H "Content-Type: application/json" \
     -d '{"id":"<an id from /manifest>"}' \
     https://api.maxrosoff.com/polaroid/photo > firmware/data/p/test.bin
```

Build and flash the firmware:

```bash
cd firmware
pio run -t upload        # default env is polaroid-xiao
pio run -t uploadfs      # pushes data/ (framebuffers + manifest) to LittleFS
```

Run the tests:

```bash
cd firmware               && pio test -e native -e native-xiao   # 72, no hardware needed
cd ../../Personal-Website && npm test                            # 70
```

## Modes

| Mode | Trigger | What happens |
| --- | --- | --- |
| `NORMAL` | hourly timer | advance to next local framebuffer, push to panel, sleep. No network. |
| `SYNC` | **shake**, or once a day | WiFi up, diff against `/manifest`, download only what changed, show the newest photo, WiFi down, sleep |
| `FRIDGE` | low-threshold motion (fridge door) | advance one photo. Off by default — `POLAROID_FRIDGE_MODE` |
| `PROVISION` | no saved credentials | captive-portal AP so the couple can pick their WiFi from a phone |

Shake and fridge-jolt both raise the same `INT1` line. They're told apart in the LIS3DH's own
hardware: a shake is several direction reversals per second and fires the **click** detector; a
fridge door is one directional swing and fires the lower **activity** threshold. On wake the
firmware reads `CLICK_SRC` and `INT1_SRC` to decide which happened.

## Panel

Waveshare 4" E Ink Spectra 6 (E6), `EPD_4in0e`. Native **400 × 600 portrait**, four bits per pixel,
two pixels per byte — **120,000 bytes per frame**, fixed.

Six inks, and nothing in between — no grays, no blends:

| | black | white | yellow | red | blue | green |
| --- | --- | --- | --- | --- | --- | --- |
| nibble | `0x0` | `0x1` | `0x2` | `0x3` | `0x5` | `0x6` |

A full refresh takes 15–35 seconds and visibly flashes through color planes. That's the technology,
not a bug. It's also the single largest line item in the power budget — see `docs/POWER.md`.
