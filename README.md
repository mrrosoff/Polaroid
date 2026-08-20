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
| `PROVISION` | no saved credentials | captive-portal AP so the couple can pick their WiFi from a phone |

A shake is picked out in the LIS3DH's own hardware: several direction reversals per second fire the
**click** detector, where a single knock or door swing only trips the lower **activity** threshold.
On wake the firmware reads `CLICK_SRC` and `INT1_SRC` to tell the two apart, and treats anything
that isn't a shake as a spurious wake.

## Panel

Waveshare 4" E Ink Spectra 6 (E6), `EPD_4in0e`. Native **400 × 600 portrait**, four bits per pixel,
two pixels per byte, so **120,000 bytes per frame**, fixed.

Six inks, and nothing in between. No grays, no blends:

| | black | white | yellow | red | blue | green |
| --- | --- | --- | --- | --- | --- | --- |
| nibble | `0x0` | `0x1` | `0x2` | `0x3` | `0x5` | `0x6` |

A full refresh takes 15–35 seconds and visibly flashes through color planes. It is also the single
largest line item in the power budget, see `docs/POWER.md`.
