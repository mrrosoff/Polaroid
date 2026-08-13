# Power budget

Target was two months on 2300 mAh. The design lands at roughly **five and a half months on
2000 mAh**, and the headroom comes from somewhere slightly surprising, so it's worth writing down
where.

## The finding

**Sleep current is not the thing to optimize. The panel refresh is.**

At an hourly refresh, pushing pixels costs about nine times what sleeping costs. Every µA clawed
out of deep sleep is worth roughly half a day of runtime; every refresh not taken is worth about
seven. If you ever need more life out of this thing, lengthen `REFRESH_INTERVAL_SECONDS` before you
go hunting for leakage.

That was less true on the generic SuperMini, where an always-on power LED and an ungated panel
dominated everything and the device died in three weeks. Moving to the XIAO deletes the LED problem
outright, and gating the panel deletes the other one — which is exactly why the refresh is now the
only thing left worth arguing about.

## Numbers

XIAO ESP32S3, Adafruit #2011 (2000 mAh), hourly refresh, one sync a day, a few shakes a week.

| Line item | Current | Duration | Per day |
| --- | ---: | ---: | ---: |
| Deep sleep (MCU + LIS3DH + gated panel) | 40 µA | 24 h | 0.96 mAh |
| Panel refresh (MCU + panel, 30 s) | 45 mA | 24 × 30 s | 9.00 mAh |
| Daily sync (WiFi up, diff, down) | 120 mA | 10 s | 0.33 mAh |
| Shake sync, ~3/week | 120 mA | 15 s | 0.21 mAh |
| Battery divider (2 × 1 MΩ) | 1.65 µA | 24 h | 0.04 mAh |
| | | **total** | **10.5 mAh/day** |

2000 mAh × 0.85 usable = 1700 mAh ÷ 10.5 = **162 days**, about five and a half months.

The 40 µA is the figure to distrust. Seeed specifies 14 µA for the bare XIAO in deep sleep; the
LIS3DH in low-power mode adds ~2 µA, the divider 1.65 µA, and the charge controller and 3.3 V
regulator add the rest. 40 µA is a conservative working number. If yours measures 90 µA you get
144 days instead of 162 — the conclusion doesn't move, which is the point of the first section.

## What each fix is worth

| | Sleep current | Runtime |
| --- | ---: | ---: |
| Panel ungated, LIS3DH through its regulator | ~150 µA | 130 days |
| Panel gated on `D4` | ~69 µA | 155 days |
| ...and LIS3DH fed at `3Vo` | ~40 µA | 162 days |

Neither is dramatic on this board, and both cost nothing, so do both. On the SuperMini the same
table starts at 2400 µA and 23 days, because of the power LED — that's the difference the board
choice makes.

## Rules the firmware holds to

Every one of these has a comment at its site in the source tagged `POWER:`, so this document stays
auditable against the code.

**Deep sleep is the only resting state.** There is no `loop()`. `setup()` runs to completion and
ends in `sleepUntilNextEvent()`, which does not return, on every path including error paths.

**Power-owning things are RAII.** `Panel` and `Net` both tear their hardware down in a destructor
rather than in a function you have to remember to call. Every early return out of a sync or a render
is otherwise a path where forgetting costs the battery, and there are a lot of early returns —
failed connect, missing file, wedged panel, timeout.

**WiFi is torn down, not just disconnected.** `WiFi.disconnect(true, true)`, then `WIFI_OFF`, then
`esp_wifi_stop()` and `esp_wifi_deinit()`. Calling only `disconnect()` leaves the PHY powered —
about 1 mA that follows you into deep sleep and is invisible without a meter on the rail.

**WiFi and the panel never overlap.** `runSync()` scopes the `Net` object so the radio is fully down
before the 30 s refresh starts. Otherwise the two largest draws in the design stack on top of each
other, and the peak is what the regulator and the cell's protection circuit have to survive.

**The framebuffer is never held in RAM.** 120,000 bytes would fit, but streaming it from LittleFS to
SPI in 512-byte chunks means no allocation and the panel starts accepting data ~200 ms sooner. At
45 mA that's real.

**CS is held low across the whole data phase.** Waveshare's reference driver toggles chip-select
around every single byte, which for 120,000 bytes is 120,000 extra GPIO round-trips and roughly
triples transfer time. Bulk transfer with CS held is the same wire protocol.
`EPD_BULK_TRANSFER` in `Config.h` reverts to per-byte if a panel turns out to be fussy.

**A wedged panel cannot hold us awake.** `EPD_BUSY_TIMEOUT_MS` is 40 s, past the far end of the
datasheet's refresh range. On timeout the firmware gives up and sleeps; e-ink keeps whatever was
already on screen.

**A failed sync backs off.** `syncInterval()` goes 1 h, 2 h, 4 h … up to the normal daily cadence,
counted from the last *attempt* rather than the last success. Retrying hourly through a router
outage would cost 24 connect timeouts a day — 15 s at 120 mA each, about 12 mAh, more than the
entire rest of the budget. It would have halved runtime with no visible symptom. After three
consecutive failures the panel gets a small offline icon, composited onto a refresh that was
happening anyway.

**Sync has a hard wall on radio time.** `SYNC_TIMEOUT_MS` is 45 s. Whatever downloaded is committed
and the rest waits for tomorrow — better than holding the radio up until a bad connection drains
the cell.

**Below 5% battery the panel stops refreshing entirely.** E-ink holds its last image with no power,
so the couple is left looking at a photo rather than at the half-drawn frame you get when the rail
sags twenty seconds into a refresh.

**Nothing floats.** Every unused GPIO gets an explicit pull before sleep. A floating CMOS input
oscillates around its threshold and burns current in a way that is very hard to attribute later.
On the XIAO this list is empty — all eleven pins are used.

**The ADC divider is not gated.** A MOSFET would save 1.65 µA and cost a gate resistor's leakage
plus a part. At 0.04 mAh/day the divider is 0.4% of the budget. Left alone deliberately.
