# Hardware

## Bill of materials

### Already ordered

| Part | Notes | Link |
| --- | --- | --- |
| Waveshare 4" E Ink Spectra 6 (E6), 600×400, w/ HAT+ driver board | The panel. You'll use the raw ribbon + driver board, not the Pi HAT headers. | [B0DHTNHRRY](https://www.amazon.com/dp/B0DHTNHRRY) |
| ESP32-S3 SuperMini (10-pack) | 240 MHz, 512 KB SRAM, 4 MB flash, no PSRAM. 22.5 × 18 mm. | [B0GS1X97DZ](https://www.amazon.com/dp/B0GS1X97DZ) |
| ELEGOO double-sided protoboard, 32 pcs | Cut one down to carry the S3 + LIS3DH. | [B072Z7Y19F](https://www.amazon.com/dp/B072Z7Y19F) |
| LOVIMAG neodymium discs, 12 pcs, 32 × 3 mm, adhesive | Fridge mount. Three across the back is plenty for ~90 g. | [B072KDBJWC](https://www.amazon.com/dp/B072KDBJWC) |
| BOENFU 6" flush cutters | For trimming the SuperMini headers off. | [B07C5PM8L4](https://www.amazon.com/dp/B07C5PM8L4) |

### Still to order

| Part | Why this one | Link |
| --- | --- | --- |
| **Qimoo 103450 3.7 V 2000 mAh LiPo**, protection board, JST 1.25 | 50 × 34 × 10 mm. See the fit analysis below — this is the size that keeps everything in one plane. | [B0FT3CZ6N1](https://www.amazon.com/dp/B0FT3CZ6N1) |
| **Adafruit LIS3DH triple-axis accelerometer breakout** (#2809) | The library, the click detector, and the `INT1` pin are all first-class. 25 × 19 mm. | [adafruit.com/product/2809](https://www.adafruit.com/product/2809) |

Two notes on the battery pick, since it's the constrained part:

The 103450 cell is **1700 mm²** of footprint against the panel's ~5500 mm² — 31%, comfortably
under the half you asked for. A larger cell buys you nothing: the budget in `docs/POWER.md` already
lands near five months, and anything bigger stops fitting in one plane.

Get the **JST 1.25** variant, not PH 2.0. The SuperMini has no battery connector at all, so you're
soldering to pads either way, but 1.25 is the smaller strain-relief and it matters at 10 mm total
stack height.

You will also need, if you don't have them: 30 AWG silicone wire, a JST 1.25 pigtail, and a small
LiPo charger board (TP4056 with protection) unless you're happy pulling the cell to charge it.

## Physical fit

The point of the exercise: **nothing stacks.** The battery, the MCU, and the accelerometer all sit
side by side behind the panel, so the finished object is only as thick as the panel plus the
battery.

The 4" panel's active area is 84.5 × 56.4 mm (4" diagonal at 3:2). Call the outline ~90 × 61 mm —
**measure your actual panel before cutting protoboard**, Waveshare's bezel varies between batches.

```
        ~61 mm
  ┌───────────────────┐
  │  ┌─────────────┐  │   ← battery, 50 × 34 mm, 10 mm thick
  │  │   2000 mAh  │  │
  │  │   103450    │  │
  │  └─────────────┘  │
  │ ┌──────┐ ┌──────┐ │   ← S3 SuperMini 22.5 × 18   ~90 mm
  │ │ S3   │ │LIS3DH│ │     LIS3DH breakout 25 × 19
  │ └──────┘ └──────┘ │
  │   [magnet] [mag]  │
  └───────────────────┘
```

Used: 1700 + 405 + 475 = 2580 mm² of ~5490 mm². Leaves half the plane for wiring, the ribbon
connector's turning radius, and the magnets.

Orient the LIS3DH so its **X axis lies in the plane of the fridge door** — that's the axis a shake
swings along and the axis the door swings along, and it's the one the thresholds in `Config.h` are
tuned against.

## Wiring

Pins are chosen so every wake source is on an RTC-capable GPIO (`GPIO0–21` on the S3) and the
battery divider lands on ADC1 (`GPIO1–10`), which is the ADC that still works while WiFi is up.

### Panel — SPI

| Panel | S3 GPIO | Note |
| --- | --- | --- |
| `SCK` | 12 | |
| `MOSI` / `DIN` | 11 | |
| `CS` | 10 | |
| `DC` | 9 | |
| `RST` | 8 | |
| `BUSY` | 7 | input, panel drives it low while refreshing |
| `PWR` | 6 | **gate the panel's rail through this.** See below. |
| `VCC` | 3V3 | |
| `GND` | GND | |

### Accelerometer — I²C

| LIS3DH | S3 GPIO |
| --- | --- |
| `SCL` | 4 |
| `SDA` | 5 |
| `INT1` | 3 |
| `3Vo` | 3V3 — **feed 3.3 V in here, not into `Vin`** |
| `GND` | GND |

### Battery sense

`GPIO2` (ADC1_CH1), through a 2 × 1 MΩ divider off `BAT+`. One megohm per leg costs 1.65 µA
continuously, which is the right trade against the leakage of a MOSFET gate — see `docs/POWER.md`.

## Three things that will cost you the battery life if you skip them

**Cut the SuperMini's power LED.** It is wired straight across the 3.3 V rail and draws 1–3 mA
forever. That alone is more than the entire rest of the design's sleep budget, by about fifty times.
Find it next to the USB-C jack and remove it with the flush cutters, or lift one pad.

**Gate the panel through `GPIO6`.** The E6 driver board's regulator idles in the hundreds of µA even
after the panel is told to sleep. Drive the gate low and the whole board disappears from the budget.
The firmware assumes this pin exists (`PIN_EPD_PWR` in `Config.h`); if you wire the panel's `VCC`
straight to 3.3 V instead, expect roughly half the runtime.

**Power the LIS3DH at `3Vo`, bypassing its regulator.** Adafruit's breakout has a 3.3 V LDO in front
of the sensor for people feeding it 5 V. Its quiescent draw is ~29 µA — an order of magnitude more
than the sensor itself pulls in low-power mode (2 µA). Feeding 3.3 V directly to the `3Vo` pin skips
the regulator entirely. This is a supported use of that pin, not a hack.

## Flash capacity

Each framebuffer is exactly 120,000 bytes and they don't compress — dithered noise is
incompressible by construction. With the no-OTA partition tables in `firmware/`:

| Board | LittleFS | Photos | Env |
| --- | --- | --- | --- |
| 4 MB SuperMini (what you have) | 2.1 MB | **16** | `polaroid` |
| 8 MB variant | 6.4 MB | 53 | `polaroid-8mb` |
| **16 MB N16R8 variant** | 14.8 MB | **123** | `polaroid-16mb` |

### PSRAM is not storage

The SuperMini you ordered is probably an **ESP32-S3FH4R2** — 4 MB flash *and* 2 MB PSRAM. That PSRAM
does nothing for photo count. Photos live in flash, which survives power loss; PSRAM is volatile
working memory that is empty every time the device wakes. 2 MB of PSRAM is still 16 photos.

It's also not useful to this firmware in any other way. The framebuffer streams from LittleFS to SPI
512 bytes at a time and is never held whole, so there is nothing large to allocate. PSRAM is
therefore left uninitialized (`BOARD_HAS_PSRAM=0`), which is also the right call for power — an
initialized PSRAM die adds current in deep sleep and returns nothing here.

Check what you actually have:

```bash
esptool.py --port /dev/cu.usbmodem* flash_id
```

`ESP32-S3 (QFN56) (revision v0.2)` plus `Detected flash size: 4MB` is the R2. What matters is the
flash line, not the PSRAM line.

### Getting to 123 photos

**Start building today on the 4 MB boards you already have.** Nothing about stages 1–7 of the build
order cares about capacity — you're bringing up a panel, calibrating shake thresholds against a real
fridge, and measuring sleep current. Sixteen photos is plenty to do all of that, and the board is
already on your desk.

Then, in parallel, order **[DIYmalls ESP32-S3 N16R8 Mini,
2-pack](https://www.amazon.com/dp/B0HC2ZYKF2)** — same SuperMini form factor, same footprint, same
pinout, 16 MB flash, and it ships from Amazon rather than from a slow boat. When it arrives you
change `-e polaroid` to `-e polaroid-16mb` and reflash. No wiring changes, no code changes.

The Seeed XIAO ESP32S3 (`-e polaroid-xiao`) is supported too and is a genuinely nicer board —
onboard LiPo charging, 21 × 17.5 mm, a deep-sleep floor Seeed actually specifies. But it's 8 MB
flash for 53 photos, and if it's quoting 3–6 weeks it is not worth blocking the build on. The
profile is there if you ever want it.

Sixteen photos is a thin rotation. At an hourly refresh the couple sees the same photo twice a day,
and every photo they add past sixteen evicts another, which means the device has to sync more often
to keep the rotation interesting — you end up spending radio time to work around missing flash.
123 photos is more than they will plausibly upload, so the album is fully resident and the radio
comes up once a day out of habit rather than out of need. **Storage is the cheapest way to buy back
WiFi time.**

**Don't return the 10-pack.** Keep it for bring-up. You are going to solder a panel ribbon, a LiPo,
and an accelerometer onto a headerless board by hand, and the probability of killing one is not
small. Ten cheap boards to practice and prototype on, plus two good ones for the actual gift, is the
right shape — the 4 MB boards are perfectly fine for stages 1–4 of the build order, where you're
displaying a hardcoded framebuffer and calibrating shake thresholds and don't care about capacity.

Build with `-e polaroid-16mb` instead of `-e polaroid`.
