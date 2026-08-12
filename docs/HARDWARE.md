# Hardware

## Bill of materials

### Have

| Part | Notes | Link |
| --- | --- | --- |
| Waveshare 4" E Ink Spectra 6 (E6), 600×400, w/ HAT+ driver board | The panel. You'll use the ribbon + driver board, not the Pi HAT headers. | [B0DHTNHRRY](https://www.amazon.com/dp/B0DHTNHRRY) |
| ESP32-S3 SuperMini ×10 | 4 MB flash. **Prototype on these** — bring up the panel, calibrate shake thresholds, measure sleep current. Build `-e polaroid`. | [B0GS1X97DZ](https://www.amazon.com/dp/B0GS1X97DZ) |
| ELEGOO double-sided protoboard, 32 pcs | Cut one down to carry the MCU + LIS3DH. | [B072Z7Y19F](https://www.amazon.com/dp/B072Z7Y19F) |
| LOVIMAG neodymium discs, 32 × 3 mm, adhesive | Fridge mount. **One**, dead centre, recessed flush into the back. | [B072KDBJWC](https://www.amazon.com/dp/B072KDBJWC) |
| BOENFU 6" flush cutters | For trimming headers off. | [B07C5PM8L4](https://www.amazon.com/dp/B07C5PM8L4) |

### Order

**One Adafruit order covers the battery and the accelerometer:**

| Part | Why this one | Link |
| --- | --- | --- |
| **Lithium Ion Battery 3.7 V 2000 mAh** (#2011) | 60 × 36 × **7 mm**, JST-PH, protection built in. The 7 mm is the point — it is the single biggest term in the finished thickness. | [adafruit.com/product/2011](https://www.adafruit.com/product/2011) |
| **LIS3DH triple-axis accelerometer breakout** (#2809) | The library, the click detector, and `INT1` are all first-class. 25 × 19 mm. | [adafruit.com/product/2809](https://www.adafruit.com/product/2809) |

**And direct from Seeed, 2-day:**

| Part | Why | Link |
| --- | --- | --- |
| **XIAO ESP32S3** | The board this is built around. 8 MB flash (53 photos), onboard LiPo charging, 21 × 17.5 mm, and a deep-sleep floor Seeed actually specifies. Build `-e polaroid-xiao`. | [seeedstudio.com](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html) |

Not the 2500 mAh (#328), even though it's barely more expensive. It's 50 × 60 mm and 7.3 mm thick —
500 mAh more (about two extra weeks on a five-month budget) in exchange for 40% more area in a case
that is already sized by the panel, and it leaves no room beside it for the MCU and accelerometer.

You'll also want 30 AWG silicone wire and a JST-PH pigtail. You do **not** need a TP4056: the XIAO
charges the cell over its own USB-C.

## Why the XIAO

It costs 53 photos against the 123 a 16 MB SuperMini would give, and it's worth it:

**Onboard LiPo charging.** `B+` / `B-` pads on the underside, charged over the same USB-C you flash
through. That deletes a part from a footprint that has no room for spare parts, and it means the
couple charges the frame with a phone cable twice a year instead of you handing them a charger board.

**It has no always-on power LED.** The generic SuperMini does, wired straight across the 3.3 V rail
at 1–3 mA — which is fifty times the entire rest of the sleep budget and has to be physically cut
off with the flush cutters. The XIAO simply doesn't have the problem.

**Seeed publishes a deep-sleep number** (14 µA) and designed for it. Generic SuperMini sleep current
is whatever that batch's regulator happens to do.

53 photos is not really a constraint. At an hourly refresh that's a rotation that takes two and a
half days to repeat, and the album is fully resident so the radio comes up once a day out of habit
rather than out of need.

One thing to know: the XIAO's charge current defaults to **100 mA**, so a full 2000 mAh charge takes
about 20 hours. Irrelevant for a device charged twice a year, surprising if you don't expect it.

## Physical fit

Vendor numbers, since my first pass guessed and guessed wrong:

| | |
| --- | --- |
| Driver board (HAT+) | **101.0 × 68.0 mm** — this sets the footprint |
| Raw panel glass | 99.0 × 66.0 × **0.85 mm** |
| Active area | 84.6 × 56.40 mm |

The finished case is **73.6 × 122.6 × 21.5 mm** — see `enclosure/`.

**The stack is: glass, board, then everything else.** The driver board covers essentially the whole
footprint, so the battery, MCU and accelerometer sit *behind* it rather than beside it. That is
still "nothing stacks" in the sense that mattered — battery, MCU and LIS3DH are all in one layer,
none of them on top of each other — but the earlier diagram showing them alongside the panel was
based on a bad guess at the panel size and is gone.

```
  front ─────────────────────────────────────────── back
   bezel 2.2 │ glass 0.85 │ board 6.0 │ battery 7 + swell │ floor 3.6
                                        MCU 4, LIS3DH 3
```

Behind the board, in one layer on the tray floor:

```
        ~73.6 mm
  ┌──────────────────┐
  │ ┌──────┐         │
  │ │ batt │         │   battery 60 × 36 × 7, on its side
  │ │ 2000 │ ┌─────┐ │   LIS3DH  25 × 19
  │ │ mAh  │ │LIS3D│ │        ~122.6 mm
  │ └──────┘ └─────┘ │
  │      ( magnet )  │   32mm disc, dead centre, recessed flush
  │     ┌──────┐     │
  │     │ XIAO │     │   USB-C out the bottom wall
  │     └──────┘     │
  └──────────────────┘
```

Battery footprint is 2160 mm² against the case's 9023 mm² — **24%**, well under the half you asked
for. Thickness is set by the sum of glass + board + battery, not by any one of them, which is why
`board_t` is the number worth measuring before you print anything.

Orient the LIS3DH so its **X axis lies in the plane of the fridge door** — that's the axis a shake
swings along and the axis the door swings along, and it's what the thresholds in `Config.h` are
tuned against.

## Wiring

The XIAO breaks out exactly eleven GPIO and this design needs exactly eleven, so there is no slack.
Two constraints drove the assignment: every wake source must be on GPIO0–21 (the S3's RTC domain),
and the battery divider must be on ADC1, the ADC that still works while WiFi is up. `D6`/`D7` are
GPIO43/44 — outside the RTC domain, no ADC — so they get the two signals that need neither.

### Panel — SPI

| Panel | XIAO pin | GPIO |
| --- | --- | --- |
| `SCK` | D6 | 43 |
| `MOSI` / `DIN` | D7 | 44 |
| `CS` | D10 | 9 |
| `DC` | D9 | 8 |
| `RST` | D8 | 7 |
| `BUSY` | D5 | 6 |
| `PWR` | D4 | 5 |
| `VCC` | 3V3 | |
| `GND` | GND | |

### Accelerometer — I²C

| LIS3DH | XIAO pin | GPIO |
| --- | --- | --- |
| `SCL` | D3 | 4 |
| `SDA` | D2 | 3 |
| `INT1` | D1 | 2 |
| `3Vo` | 3V3 — **feed 3.3 V here, not into `Vin`** | |
| `GND` | GND | |

### Battery

Cell to the `B+` / `B-` pads on the XIAO's underside. Sense through a 2 × 1 MΩ divider off `B+`
into `D0` (GPIO1, ADC1_CH0). One megohm per leg costs 1.65 µA continuously, which is the right
trade against the leakage and part count of a MOSFET switch.

The SuperMini pinout (`-e polaroid`) is different and lives in `firmware/include/Config.h` under the
`#else` branch. Both are supported; the firmware picks by build environment.

## Pin budget: exactly enough, with nothing spare

The XIAO breaks out **11 GPIO**. This design needs **11**. That works, and it works with zero
margin — worth knowing before you solder, because there is no room for a status LED, a reset
button, or a spare line to probe with.

| | Pins | |
| --- | ---: | --- |
| Panel SPI | 5 | `SCK`, `MOSI`, `CS`, `DC`, `RST` |
| Panel `BUSY` | 1 | must be read; the panel holds it low for 15–35 s |
| Panel `PWR` gate | 1 | see below |
| I²C | 2 | `SCL`, `SDA` |
| `INT1` | 1 | must be RTC-capable — this is the wake source |
| Battery sense | 1 | must be ADC1 — ADC2 stops answering while WiFi is up |
| **Total** | **11** | of 11 |

Nothing here is padding. There's no MISO because the panel is write-only. There's no second chip
select because there's one SPI device. The accelerometer is on I²C rather than SPI precisely because
SPI would have cost another two pins.

Two tests in `pio test -e native-xiao` assert that no pin is used twice, that `INT1` is inside the
RTC domain, and that battery sense is on ADC1 — so a mistake in this table fails on your laptop
rather than on a headerless board glued behind a panel.

### If you need a pin back

In order of what it costs you:

**Drop the panel `PWR` gate (`D4`).** Wire the panel's `VCC` to 3.3 V and set `PIN_EPD_PWR` to `-1`.
Sleep current goes from ~40 µA to ~69 µA — about **7 days off 162**. This is the cheapest pin
available and the one to take first.

**Drop battery sense (`D0`).** You lose the low-battery corner icon. The device still works; it just
dies without warning, which for a gift is worse than it sounds — the couple would have no idea it
was coming.

**Don't drop `BUSY`.** You'd have to replace it with a fixed 35 s delay, which both wastes power on
every single refresh (the dominant line item in the budget) and removes the timeout that stops a
wedged panel from holding the device awake until the battery is flat.

## Two things that will cost you the battery if you skip them

**Gate the panel through `D4`.** The E6 driver board's regulator idles in the hundreds of µA even
after the panel is told to sleep. Drive the gate low and the board disappears from the budget. The
firmware assumes this pin exists (`PIN_EPD_PWR`); wire the panel's `VCC` straight to 3.3 V instead
and expect roughly half the runtime.

**Power the LIS3DH at `3Vo`, bypassing its regulator.** Adafruit's breakout has a 3.3 V LDO in front
of the sensor for people feeding it 5 V. Its quiescent draw is ~29 µA — an order of magnitude more
than the sensor itself pulls in low-power mode (2 µA). Feeding 3.3 V directly to `3Vo` skips it
entirely. This is a supported use of that pin, not a hack.

(On the SuperMini there is a third: cut the always-on power LED. The XIAO doesn't have one.)

## Flash capacity

Each framebuffer is exactly 120,000 bytes and they don't compress — dithered noise is
incompressible by construction. With the no-OTA partition tables in `firmware/`:

| Board | LittleFS | Photos | Env |
| --- | --- | --- | --- |
| **XIAO ESP32S3, 8 MB** | 6.4 MB | **53** | `polaroid-xiao` ← default |
| SuperMini, 4 MB | 2.1 MB | 16 | `polaroid` |
| SuperMini N16R8, 16 MB | 14.8 MB | 123 | `polaroid-16mb` |

### PSRAM is not storage

Most of these boards have PSRAM — the XIAO has 8 MB, and the SuperMini is often an S3FH4R2 with
2 MB. It does nothing for photo count. Photos live in flash, which survives power loss; PSRAM is
volatile working memory that is empty every time the device wakes.

It's not useful to this firmware in any other way either. The framebuffer streams from LittleFS to
SPI 512 bytes at a time and is never held whole, so there is nothing large to allocate. PSRAM is
left uninitialized (`BOARD_HAS_PSRAM=0`), which is also the right call for power — an initialized
PSRAM die adds current in deep sleep and returns nothing here.
