# Power budget

Target was two months on 2300 mAh. The design lands at roughly **five months on 2000 mAh**, and the
headroom comes from somewhere slightly surprising, so it's worth writing down where.

## The finding

**Sleep current is not the thing to optimize. The panel refresh is.**

At an hourly refresh, pushing pixels costs about four times what sleeping costs. Every µA you claw
back out of deep sleep is worth roughly a day of runtime; every refresh you don't do is worth about
four. If you ever need more life out of this thing, lengthen `REFRESH_INTERVAL_SECONDS` before you
go hunting for leakage.

That said, the leakage still has to be dealt with — because the *default* version of this build,
with the stock SuperMini and an ungated panel, is dominated by leakage and dies in three weeks. The
three fixes in `docs/HARDWARE.md` are what move it from three weeks to five months.

## Numbers

Assumes hourly refresh, one sync a day, and a handful of shakes a week.

| Line item | Current | Duration | Per day |
| --- | ---: | ---: | ---: |
| Deep sleep | 60 µA | 24 h | 1.44 mAh |
| Panel refresh (MCU + panel, 30 s) | 45 mA | 24 × 30 s | 9.00 mAh |
| Daily sync (WiFi up, diff, down) | 120 mA | 10 s | 0.33 mAh |
| Shake sync, ~3/week | 120 mA | 15 s | 0.21 mAh |
| Battery divider (2 × 1 MΩ) | 1.65 µA | 24 h | 0.04 mAh |
| | | **total** | **11.0 mAh/day** |

2000 mAh × 0.85 usable = 1700 mAh ÷ 11.0 = **154 days**.

The 60 µA sleep figure is the one to distrust. The ESP32-S3 die alone is ~8 µA; the rest is the
SuperMini's LDO and USB-serial bridge, and it varies board to board. 60 µA is a realistic
measured-in-the-wild number for this class of board with the LED removed. If yours measures 150 µA,
you get 130 days instead of 154 — the conclusion doesn't change, which is the point of the first
section.

## What each fix is worth

| | Sleep current | Runtime |
| --- | ---: | ---: |
| Stock board, panel ungated | ~2400 µA | 23 days |
| Power LED cut | ~420 µA | 100 days |
| ...and panel gated on `GPIO6` | ~89 µA | 147 days |
| ...and LIS3DH fed at `3Vo` | ~60 µA | 154 days |

The LED is the whole ballgame. The other two are worth having and cost nothing, but if you only do
one thing, cut the LED.

## Rules the firmware holds to

Every one of these has a comment at its site in the source, tagged `POWER:`, so this list stays
auditable against the code.

**Deep sleep is the only resting state.** There is no `loop()`. `setup()` runs to completion and
calls `esp_deep_sleep_start()` on every path, including error paths. A crash that skips the sleep
call is a dead battery, so the sleep is in a destructor-guarded scope, not at the bottom of a
function that might `return` early.

**WiFi is torn down, not just disconnected.** `WiFi.disconnect(true, true)` then `WiFi.mode(WIFI_OFF)`
then `esp_wifi_stop()` and `esp_wifi_deinit()`. Calling only `disconnect()` leaves the radio
clock-gated but powered, which costs ~1 mA that survives into deep sleep and is invisible unless you
have a meter on the rail.

**The framebuffer is never held in RAM.** 120,000 bytes would fit in the S3's 512 KB, but streaming
it from LittleFS to SPI in 512-byte chunks means the CPU never has to allocate, and more
importantly it means the panel starts accepting data ~200 ms sooner. At 45 mA that's real.

**CS is held low across the whole data phase.** Waveshare's reference driver toggles chip-select
around every single byte, which for 120,000 bytes is 120,000 extra GPIO round-trips and roughly
doubles the transfer time. Bulk transfer with CS held is the same wire protocol and finishes in
about a third of the time. `EPD_BULK_TRANSFER` in `Config.h` reverts to per-byte if a panel turns
out to be fussy about it.

**Nothing floats.** Every unused GPIO gets an explicit pull configured before sleep. A floating
CMOS input oscillates and burns current in a way that is extremely difficult to find later.

**The ADC divider is not gated.** A MOSFET to switch the divider off would save 1.65 µA and cost a
gate resistor's leakage plus a part. At 0.04 mAh/day the divider is 0.4% of the budget. Left alone
deliberately.
