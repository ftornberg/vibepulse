# TID: clock, pomodoro and timer

TID is an opt-in launcher app for the original 2.16 panel. It has nothing to do
with agents: it turns the panel into a small desk clock, a pomodoro timer and a
plain countdown, and **the side the panel stands on chooses which one**. There
is no menu.

> **Status.** Designed, built and reviewed in the simulator only. It has **not
> been flashed** to any panel, the rotation values below are provisional until
> they are measured on the unit, and it is silent (see [Limits](#limits)).
> Nothing here promotes a hardware capability in `spec/`.

## What it does

| Panel position | Face |
|---|---|
| Button edge up | **Clock:** the current local time as `HH:MM`, seconds as a ring around it |
| Right edge down | **Pomodoro:** 25 min focus, 5 min break, four rounds, then a 15 min long break |
| Left edge down | **Timer:** pick 20, 40 or 50 minutes |
| Button edge down | No face of its own: the app keeps whatever it showed last |

The digital time sits in the middle of the glass and a thin ring runs around it:

- **Clock:** the ring is the seconds of the current minute. It is never empty
  while the time is valid, and it is hidden until the panel has a valid time
  (the digits then read `––:––`, never `00:00`).
- **Pomodoro and timer:** the ring is the time left, full at the start and
  shrinking to nothing. It is hidden while a timer is idle.

### Touch

- **Tap** starts, pauses and resumes. In the timer face a tap on 20, 40 or 50
  starts that length.
- **RESET** (shown while a timer runs or is paused) cancels it.
- **Long press** anywhere opens the launcher, as in every app.
- When a timer ends, a full circle and **DONE** take over the glass in *any*
  face until you tap. In pomodoro the next phase then waits idle for another
  tap; nothing starts by itself while the app is silent.

The two timers are independent: turning the panel away from a running one does
not cancel it, and returning shows the correct time left.

## Build it

TID is off by default so that a fresh clone still builds exactly one app, and it
is 2.16 only (the 2.41 V2 has a fixed landscape rotation, so there is nothing to
read there).

```bash
# Simulator (its own build directory: CMake caches the option)
cmake -S sim -B sim/build-time -G Ninja -DTORGET_WITH_TIME=ON
ninja -C sim/build-time
./sim/build-time/torget-sim        # R turns the panel a quarter turn

# Exact native frames of every state
TORGET_CAPTURE_DIR=/tmp/time-frames ./sim/build-time/torget-sim --time-app-captures

# Firmware, build only (never into build/, which the OTA sender trusts)
. ~/esp/esp-idf/export.sh
idf.py -B build-time -DTORGET_WITH_TIME=ON build
```

Installing on a panel is a separate step that you ask for explicitly; see
[`docs/ota.md`](ota.md) and `CLAUDE.md`. The OTA sender refuses a `-dirty`
build, so commit before any install.

### Measuring which side is which

`components/app_time/time_core.h` maps the rotation the panel measures
(`torget_orientation()`, quarter turns from boot) to a face:
`TG_TIME_ROT_CLOCK`, `TG_TIME_ROT_POMODORO` and `TG_TIME_ROT_TIMER`. The
defaults are a guess. After a first install, stand the panel on each side, note
which face appears, and edit the three `#define` values. A
wrong value shows as a constant wrong face and is one number to change.

## Limits

- **Silent.** There is no sound yet. A timer that finishes while another app is
  on the glass is seen when TID is next opened. A beep needs a platform audio
  path (a codec backend, a physical speaker test and a display-DMA budget) and is
  a separate piece of work.
- **Needs You is invisible while TID is showing.** The Needs You takeover is
  built inside the VibePulse app's own page tree, so an alert from an agent does
  not appear while TID is on the glass. Leave the panel on VibePulse while you
  wait for an agent. Making it a platform-level overlay is a separate decision.
- **Without a working IMU the face stays where it was** (the clock after boot);
  the auto-rotation reports no orientation and TID keeps the last face.
- **Wall-clock time** comes from the RTC or SNTP like the night dimming does. The
  clock face uses local time from `TG_TIMEZONE`.

## Design notes

The pure logic (`time_core`, `time_present`) has no LVGL and no system clock, and
is tested on the host with `./test/run.sh`. Timers store a deadline in the
device's monotonic clock and compute the time left from it, so they stay correct
across a long absence. The LVGL layer only renders a view model and forwards
touches; taps use `LV_EVENT_SHORT_CLICKED` so a long press that opens the
launcher never also toggles a timer.

The design and plan live in
[`superpowers/specs/2026-09-25-time-app-design.md`](superpowers/specs/2026-09-25-time-app-design.md)
and
[`superpowers/plans/2026-09-25-time-app.md`](superpowers/plans/2026-09-25-time-app.md).
