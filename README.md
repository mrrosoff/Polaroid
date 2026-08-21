# Polaroid

A battery-powered digital Polaroid. A 4" six-color e-ink panel that lives on a fridge, cycles
through photos, and syncs new ones when you shake it, like waiting for a real Polaroid to develop.
Runs about five months per charge.

Built as a wedding gift.

## How it works

The server does all the heavy image work, so the device can stay dumb and instant.

Every uploaded photo is pre-rendered on the backend into a raw framebuffer that exactly matches the
panel's resolution and 6-color palette: Polaroid film emulation, then Floyd–Steinberg dithering
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

## Quick start

Pull a rendered framebuffer by hand. `Accept` is not optional: without it API Gateway hands back
base64 text instead of the 120,000 bytes.

```bash
curl -H "Authorization: Bearer $POLAROID_DEVICE_TOKEN" \
     -H "Content-Type: application/json" \
     -H "Accept: application/octet-stream" \
     -d '{"id":"<an id from /photos>"}' \
     https://api.maxrosoff.com/polaroid/photo > frame.bin
```

Build and flash the firmware:

```bash
cd firmware
pio run -t upload        # the shipping firmware
pio run -e bringup -t upload   # same, plus serial logging, and never sleeps
```

Run the tests:

```bash
cd firmware && pio test -e native   # 46, no hardware needed
```

The image pipeline in Personal-Website has no tests yet: `npm test` finds no files.

## Modes

| Mode | Trigger | What happens |
| --- | --- | --- |
| `NORMAL` | hourly timer | advance to next local framebuffer, push to panel, sleep. No network. |
| `SYNC` | **shake**, cold boot, or once a day | WiFi up, diff against `/photos`, download only what changed, show the newest photo, WiFi down, sleep |
| `PROVISION` | no networks in `Secrets.h` and none saved | captive-portal AP so the couple can pick their WiFi from a phone |

Shake detection is the LIS3DH's own activity threshold, high-pass filtered so gravity does not hold
it tripped at whatever angle the frame hangs. Anything that clears the threshold syncs; the
interrupt is latched, and reading `INT1_SRC` on wake is what releases it.

Networks are normally listed in `firmware/include/Secrets.h`, which is gitignored, and the strongest
one in range wins. The portal is what happens when that list is empty.

## Panel

Waveshare 4" E Ink Spectra 6 (E6), `EPD_4in0e`. Native **400 × 600 portrait**, four bits per pixel,
two pixels per byte, so **120,000 bytes per frame**, fixed.

Six inks, and nothing in between. No grays, no blends:

| | black | white | yellow | red | blue | green |
| --- | --- | --- | --- | --- | --- | --- |
| nibble | `0x0` | `0x1` | `0x2` | `0x3` | `0x5` | `0x6` |

A full refresh takes 15–35 seconds and visibly flashes through color planes. It is also the single
largest line item in the power budget, see `docs/POWER.md`.
