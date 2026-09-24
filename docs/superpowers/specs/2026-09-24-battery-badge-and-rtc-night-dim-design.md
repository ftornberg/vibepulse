# Battery badge, RTC clock and scheduled night dimming

**Date:** 2026-09-24

**Status:** Designed, reviewed with the owner in this session. Not implemented.
Nothing in this document promotes any hardware capability to physically
verified; that requires the owner at the glass and a dated registry edit.

**Board scope:** Waveshare ESP32-S3 Touch-AMOLED-2.16 only. The 2.41 V2 has a
different PMU wiring and is out of scope until its registry says otherwise.

**Scope:** A new `components/torget_power/` platform package (AXP2101 and
PCF85063 register drivers, a battery policy and a night policy), a
platform-level battery badge on the LVGL top layer, two ABOUT rows, one LABS
switch, RTC-backed boot time, and a compile-time-gated low-battery shutdown.
The tokenserver, the relays and every wire contract are unchanged; battery
telemetry never leaves the device.

## Problem

The 2.16 board carries an AXP2101 power-management IC with a charging path and
a two-pin lithium connector, and a PCF85063ATL real-time clock with battery
backup. The firmware uses neither: the BSP never configures the PMU (which is
why the panel lights without it), and `main.c` assumes the RTC has no backup
and waits for SNTP before anything network-bound can start.

On 2026-09-24 the owner fitted a 3.7 V cell to the unit with USB MAC
`44:BD:8D:60:DC:F0`. The panel now survives a pulled cable and runs fine on
the cell, but the glass says nothing about it, the cell charges on AXP2101
factory defaults nobody has read, a deep discharge ends in a hard power loss
mid-write, and every boot without network shows the wrong time until NTP
answers.

The owner's decisions, in order of the brainstorm:

1. **Role: UPS.** The panel lives on a charger; the cell bridges outages and
   moves between rooms. Not portable use.
2. **Clock: correct time at boot without network, plus scheduled night
   dimming.** No scheduled power-off, no RTC alarm wake.
3. **Badge: always visible**, bottom-right, with a percentage.
4. **Low battery: dim, then shut down cleanly** when the charger has been gone
   long enough, and come back by itself when it returns.
5. **Schedule: fixed in firmware** with a LABS on/off switch; times live in
   `secrets.h` with defaults in code.

## Hardware status (from `spec/hardware-capabilities.yaml`)

| Capability | Silicon | Board wired | BSP | Firmware today | Unit verified |
| --- | --- | --- | --- | --- | --- |
| `power.axp2101` | yes | yes, I2C 0x34 | no | no | unknown |
| `power.battery-connector` | yes | yes | no | no | unknown |
| `rtc.pcf85063atl` | yes | yes, I2C 0x51, INT on GPIO13 | no | no | unknown |

No PMU interrupt line is documented on this board; the PMU is polled. The
registry's constraints carry into this design unchanged: charge settings are
never written, polarity and cell are the owner's responsibility, and shutdown
behaviour must be proven on the unit before it is enabled.

## Architecture

```text
 components/torget_power/                platform/                 main/main.c
 ┌───────────────────────────┐            ┌────────────────────┐   ┌──────────────────┐
 │ axp2101.c   (I2C 0x34)    │──sample──► │                    │   │ power_task (5 s) │
 │ pcf85063.c  (I2C 0x51)    │            │ battery_badge.c    │◄──│  reads PMU       │
 │ battery_policy.c (pure)   │──state───► │  LVGL top layer,   │   │  runs policies   │
 │ night_policy.c   (pure)   │──dim?────► │  bottom-right      │   │  sets bright cap │
 └───────────────────────────┘            └────────────────────┘   │ boot: RTC → time │
        ▲ host-tested                         ▲ built in sim       │ SNTP ok → RTC    │
        └──── sim/ feeds fake samples (key B, key N) ──────────────┘ (host layer only)
```

### `components/torget_power/`

- **`axp2101.c/.h`** — reads: battery present, VBUS present, charging state
  (charging / done / not charging), fuel-gauge percentage (0–100), battery
  voltage in mV. One I2C device on the BSP bus handle from
  `bsp_i2c_get_handle()`, exactly as `main/rotation.c` does for the IMU. No
  register writes, with two exceptions: enabling the battery-voltage ADC
  channel at init (a measurement setting, register `0x30` bit 0), and,
  behind `CONFIG_TG_POWER_SHUTDOWN`, the PMU power-off command used by the
  shutdown sequence. Charge current, charge termination voltage and thermal
  limits are never touched.
- **`pcf85063.c/.h`** — reads and writes the seven time/date registers and
  reads the oscillator-stop flag (`OS`). No alarm, no timer, no interrupt
  handling in this version.
- **`battery_policy.c/.h`** — pure, host-tested. Input: a sample
  (`present`, `vbus`, `charging`, `percent`, `mv`, `valid`) and monotonic
  time. Output: badge state, brightness cap, and a `shutdown` decision. All
  thresholds and hysteresis live here.
- **`night_policy.c/.h`** — pure, host-tested. Input: local time
  (hour, minute), `time_valid`, the LABS switch, and the schedule. Output:
  whether night dimming applies right now.

Both policies are plain C with no ESP-IDF includes, so `sim/` and `test/`
compile them unchanged.

### `platform/battery_badge.c/.h`

An LVGL widget on `lv_layer_top()` anchored bottom-right at the footer
baseline with the same right margin as the "TO RESET" label. It knows only a
badge state and an optional percentage. It hides itself while a takeover
owns the glass (UPDATE READY, Needs You, WiFi setup, OTA transfer) and while
SETTINGS shows a sub-view that draws over the footer. It redraws only on a
state change.

### `main/main.c`

- **`power_task`** (low priority, 5 s period): read AXP2101, feed
  `battery_policy`, publish the badge state to the LVGL task through the
  existing tick path, and fold the brightness cap into the brightness target.
- **Boot:** before WiFi, read the RTC. If `OS` is clear and the year is 2026
  or later, call `settimeofday` and log `tid från RTC`. Otherwise log
  `RTC opålitlig` once and proceed exactly as today.
- **After each successful SNTP sync:** write the system time to the RTC once
  and log `RTC uppdaterad`. The comment claiming the RTC has no backup is
  replaced.
- **Brightness target** becomes the minimum of three sources: the existing
  inactivity rule (`NIGHT_AFTER_US` / `WAKE_HOLD_US`), `night_policy`, and
  `battery_policy`'s cap. No source can raise the target above another.

### `sim/`

Builds `battery_policy`, `night_policy` and `battery_badge`. Key `B` steps a
fake sample through: unknown → charging 71 % → full → on battery 64 % → low
18 % → critical 4 %. Key `N` toggles the night schedule as active. The static
QA matrix gains four frames (badge charging, badge critical, ABOUT with the
new rows, LABS with NIGHT DIM). The hardware drivers are not built on the
host.

## Battery policy

| State | Condition | Badge | Brightness cap | Log (on transition only) |
| --- | --- | --- | --- | --- |
| `UNKNOWN` | no battery present, or 3 consecutive read failures | outline with a dash, no percent | none | `batteri: okänd` |
| `CHARGING` | VBUS present, PMU reports charging | green fill, bolt | none | `batteri: laddar` |
| `FULL` | VBUS present, PMU reports charge done | green fill, bolt | none | `batteri: full` |
| `ON_BATTERY` | VBUS absent, percent > 20 | white fill by percent | none | `batteri: på batteri N %` |
| `LOW` | VBUS absent, percent ≤ 20 | yellow fill | `BRIGHT_NIGHT` (20) | `batteri: låg N %` |
| `CRITICAL` | VBUS absent, percent ≤ 5 for 30 s without interruption | red fill, pulsing | `BRIGHT_NIGHT` | `batteri: kritisk` |

- **Hysteresis:** leaving `LOW` upward requires percent ≥ 23; leaving
  `CRITICAL` upward requires percent ≥ 8. VBUS returning exits any state
  immediately.
- **Percent** is the PMU fuel gauge rounded to an integer. No estimation from
  voltage, no interpolation. If the gauge reads invalid, the badge shows no
  number.
- **`shutdown`** is true only in `CRITICAL`, only after the 30 s dwell, and
  only when `CONFIG_TG_POWER_SHUTDOWN` is enabled. Without the option the
  state machine still reaches `CRITICAL` and logs it; nothing else happens.

### Shutdown sequence (gated)

1. Persist LABS choices and the boot ledger to NVS.
2. Show `BATTERY EMPTY` centred for 2 s.
3. Brightness 0, panel off.
4. AXP2101 power-off command.

Power returning on VBUS is expected to restart the PMU rails and boot the
board. This is factory PMU behaviour and is **unverified on this unit**; it
is the last physical step below and the reason for the option.

## Night policy

- Default window **23:00–07:00 local time**, level `BRIGHT_NIGHT`.
- Three `#define`s in `secrets.h` with defaults in the code:
  `TG_NIGHT_START_HHMM` (2300), `TG_NIGHT_END_HHMM` (0700),
  `TG_NIGHT_ENABLED_DEFAULT` (1). An unchanged `secrets.h` gives the default.
- LABS row **NIGHT DIM** toggles it; the choice persists through the existing
  `labs_store` NVS path with the other LABS switches. Default on for new
  installs, as the owner chose.
- Windows that cross midnight are handled (start > end means "wraps").
- Without a valid clock (neither RTC nor NTP), the schedule does not apply
  and only the inactivity rule runs, exactly as today.
- A touch still raises brightness to `BRIGHT_DAY` for `WAKE_HOLD_US` (30 s)
  and it settles back while the window applies, matching today's wake
  behaviour.

## Glass

- **Badge:** 26 × 13 px outline with a nub, fill by percent, bolt left of the
  outline when VBUS is present, percent left of the bolt in the small
  uppercase label font used for `TO RESET`. Colours per the table above.
- **ABOUT** gains two label/value rows in the existing style:
  - `POWER`: `USB · CHARGING 71 %`, `USB · FULL`, `BATTERY 64 % · 4.02 V`,
    `NO BATTERY`
  - `CLOCK`: `RTC + NTP`, `RTC ONLY`, `NTP ONLY`, `NOT SET`
  Four rows plus BACK fit by reducing `SETTINGS_ABOUT_LINE_GAP` from 62 to
  50 px; the simulator frame is the review artifact before any flash.
- **LABS** gains `NIGHT DIM` in the same list as the other switches.

Out of scope, deliberately: a settings page for the schedule, battery
history, and any battery field in `/api/*` or the relays.

## Error handling and safety

- **Shared I2C bus.** The rotation task already drives the IMU on the same
  bus. ESP-IDF's `i2c_master` serialises transactions per bus, so the power
  task registers its two devices on the BSP handle and takes no lock of its
  own. Every transfer uses a 50 ms timeout.
- **Read failures** are counted; three in a row give `UNKNOWN`. One success
  resets the count. Logs are transitions, never one line per attempt, per
  `docs/observability.md`.
- **No accidental shutdown.** Requires VBUS absent, percent ≤ 5 for 30 s
  continuously, and the compile-time option. A single sample can never shut
  the panel down; VBUS returning during the dwell cancels it.
- **Charge profile untouched.** No writes to charge current, termination
  voltage or thermal registers. The registry names this as a constraint.
- **The RTC can never move the clock backwards.** RTC time is applied only at
  boot while the system clock is unset. After SNTP the network always wins.
  An RTC with `OS` set or a year before 2026 is ignored.
- **The simulator does not lie.** It runs the real policies on fake samples,
  so a simulator frame shows the same decision the firmware would make.

## Testing

- **Host tests** (`test/`) for `battery_policy` and `night_policy`: every row
  of the state table, hysteresis both ways, the 30 s dwell, VBUS returning
  mid-dwell, the read-failure counter, a window crossing midnight, an
  invalid clock, and `OS` set on the RTC path where it is modelled. Pure
  functions, no mocks.
- **Exact-raster checks** for the four new static QA frames so a footer
  shift is caught by `./test/run.sh`.
- **Physical verification, in this order, owner at the glass:**
  1. Badge state on USB in / USB out; percent tracks the PMU over an hour.
  2. ABOUT rows.
  3. Boot with WiFi disabled: time is correct from the RTC.
  4. Night dimming with a temporarily moved window boundary.
  5. Last, only after 1–4 pass: enable `CONFIG_TG_POWER_SHUTDOWN`, discharge
     to 5 % under supervision, confirm the clean shutdown, then confirm the
     panel boots when the charger returns. USB-C remains the rescue path.
- **Registry:** the owner, not the agent, sets `unit_verified` with a date for
  `power.axp2101`, `power.battery-connector` and `rtc.pcf85063atl` after the
  steps above, per `CLAUDE.md`.

## Delivery order

1. Drivers, policies, host tests, simulator keys and frames (no glass change
   visible on the unit yet).
2. Badge, ABOUT rows, LABS row; simulator review; OTA to the unit.
3. RTC boot time and write-back.
4. Night schedule.
5. Shutdown, gated, after physical steps 1–4.

Each step is one PR against `dev`. Every flash and every OTA push follows
`docs/ota.md`; nothing here authorises one.

## Open questions

- Whether the PMU's fuel gauge reports a usable percentage for this
  particular cell without a configured capacity. If it does not, the badge
  shows only the icon and voltage in ABOUT until a verified gauge
  configuration exists; no estimated percentage is ever shown.
- Whether AXP2101 factory defaults let VBUS restart the rails after a
  software power-off. Decided by physical step 5, not by reading a datasheet.
