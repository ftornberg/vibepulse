# TID: an orientation-driven clock, pomodoro and timer app

**Date:** 2026-09-25

**Status:** Designed, reviewed with the owner in this session. Not implemented.
Nothing in this document promotes any hardware capability to physically
verified, and nothing authorises a flash.

**Board scope:** Waveshare ESP32-S3 Touch-AMOLED-2.16 only. The 2.41 V2 rotation
is fixed landscape (`main/rotation.c`), so the app is not registered there.

**Scope:** One new app component, `components/app_time/`, registered in
`main/registry.c` and the simulator, plus one small read-only accessor in
`main/rotation.c`. The tokenserver, the relays and every wire contract are
unchanged. The app needs no network task.

## Goal

A non-agent app for the shelf panel. The side the panel stands on chooses what
it shows, so no menu is needed:

| Panel position | Mode |
|---|---|
| Button edge up | Clock only (current local time) |
| Right edge down | Pomodoro |
| Left edge down | Plain timer with presets 20, 40 and 50 minutes |
| Button edge down | No mode of its own: keep the last mode |

The app is an ordinary entry in the launcher (`torget_app_t`); the owner picks
it there or with KEY3. Orientation chooses the mode only inside the app. The
platform-wide auto-rotation is unchanged and keeps the picture upright.

## Success criteria

- Turning the panel between the three sides switches mode within the existing
  rotation hysteresis, and the button-edge-down and lying-flat cases never
  switch to a wrong mode.
- A running timer keeps its correct remaining time across orientation changes
  and across leaving and re-entering the app.
- No number is invented: the clock shows `––:––` (en dashes: the big number font has no ASCII hyphen) until the time is valid.
- Every state has exact native 480 x 480 simulator frames reviewed before any
  physical install.

## Non-goals (this spec)

- **Sound.** A separate second step, see below.
- Automatic app switching when the panel is turned (the owner chose the
  launcher route).
- Any change to VibePulse pages, Needs You, the relays or the auto-rotation
  behaviour.
- Cross-app completion notices. Without sound, a timer that finishes while
  another app is on the glass is seen when TID is next opened.
- Needs You while TID is showing. The Needs You takeover is created under
  VibePulse's own page tree (`tk_agent_monitor_create(root)` in
  `usage_screen.c`), so a NEEDS YOU alert is not visible while TID is on the
  glass. Making it a platform-level overlay is a separate decision; until then
  the owner should leave the panel on VibePulse when waiting for an agent.

## Architecture

- **`time_core` (pure C, no LVGL, no system clock).** Inputs are `now_us` and
  the rotation quadrant. It owns:
  - the mapping from quadrant to mode, with "keep last" for the button edge
    down;
  - two independent timers (pomodoro, plain timer), each *idle*, *running*,
    *paused* or *done*;
  - the pomodoro cycle 25 / 5 / 25 / 5 / 25 / 5 / 25 / 15 minutes, restarting
    after the long break.
  Timers store a deadline in monotonic time and compute `remaining = deadline -
  now`; nothing has to tick to stay correct. Host-tested through
  `./test/run.sh`.
- **`time_views`.** Three LVGL views (clock, pomodoro, timer) on the app's
  480 x 480 root: true black bottom, IBM Plex, sv-SE formatting, dashes when
  data is missing.
- **`app_time.c`.** Exports `torget_app_t` with `create`, `enter` and `leave`.
  While the app is visible an `lv_timer` reads the quadrant and ticks the view.
  Long press calls `torget_launcher_open()`, as the app contract requires.
- **Platform change.** A read-only `sg_rotation_quadrant()` in
  `main/rotation.[ch]` returning the current quarter-turn (0-3, or -1 without
  a running rotation), reached by apps through one new host function,
  `torget_orientation()` in `platform/torget.h` (an app component cannot
  include `main/`). The simulator implements it with a key. The rotation logic
  itself is not modified.
- **Registration.** `main/registry.c` and `sim/CMakeLists.txt`, behind a build
  flag (see decisions).

The clock reads local time using the existing `TG_TIMEZONE` and the RTC or SNTP
time source the platform already provides.

## Behaviour

- **Independent timers.** Pomodoro and plain timer each keep their own state.
  Turning the panel away from a running mode does not cancel it; returning shows
  the correct remaining time.
- **Touch.** A tap starts, pauses and resumes. In timer mode a tap on 20, 40 or
  50 selects and starts that preset. A small cancel control resets. Long press
  opens the launcher. Final layout is fixed in the design step with mockups.
- **Pomodoro phases.** Each phase ends with DONE and waits for a tap before the
  next starts. Auto-start is deliberately not used while the app is silent,
  because a phase change would go unnoticed.
- **Done state.** A clear full-screen marker on a true black background that
  stays until touched. It shows in any mode while the app is visible.
- **Brightness.** The app never calls `torget_keep_awake`. The platform dims as
  usual, so the clock becomes a quiet night light, and touch wakes the glass.

## Ring (owner request, 2026-09-25)

The digital time sits in the middle of the glass and a thin ring runs around
it in every mode (an `lv_arc`, 10 px wide, inset 16 px, drawn clockwise from
12 o'clock; no transform layer or canvas):

- **Clock:** the ring shows the seconds of the current minute. It is never
  empty while the time is valid (`(second + 1) / 60`, full at :59) and is
  hidden while the time is invalid.
- **Pomodoro and timer:** the ring shows the remaining fraction of the run,
  full at the start and shrinking to nothing, rounded up so a running timer
  never shows an empty ring. It is hidden while idle and under the DONE marker.

Everything else on the face stays inside the ring's inner edge (radius about
214 px), because the rounded bezel clips edge-near graphics. This adds no
network, no sound and no hardware claim.

## Step 2: sound (separate spec and plan)

Playing a signal at the end of a timer is a platform task, not an app change.
`torget.h` offers apps no audio path, and the repository registers no codec
backend until the physical speaker and display-DMA budget have passed device
testing (README, GitHub chime section; `components/app_tokens/`
`project_star_chime.c` is the existing failure-isolated sequencer pattern).
`spec/hardware-capabilities.yaml` lists `audio.speaker-output` as
`unit_verified: unknown` and `device-units.yaml` records `speaker: unknown`;
the owner reports a speaker is fitted, which a physical test must confirm and
record. Safe volume must be set. Sound is out of scope here.

## Testing and verification

- **Host tests first (TDD)** in `./test/run.sh`: quadrant to mode for all four
  positions including keep-last; deadline arithmetic, pause and resume; the full
  pomodoro cycle with DONE between phases; independence of the two timers;
  `––:––` while the time is unset.
- **Simulator:** a key rotates the quadrant. Exact 480 x 480 frames for clock,
  pomodoro (idle, running, paused, done), timer (selection, running, done) and
  time-not-set. The static frames are reviewed before any motion.
- **Firmware:** `idf.py build` only. No flash and no OTA unless the owner asks
  explicitly. Simulator approval never authorises a flash.
- **Physical, later and on request:** the three modes, button edge down, lying
  flat (rotation freezes), and which quadrant is "button edge up". A wrong value
  there shows as a constant wrong mode and is a single constant to change. Only
  then is anything recorded in `device-units.yaml`. The IMU is already
  physically tested for rotation; this work does not change that.
- **Documentation:** a README section with simulator frames, a CHANGELOG entry
  under `Unreleased`, and a short page under `docs/`.

## Decisions

1. **2.16 only.** Not registered on the 2.41 V2.
2. **Build flag.** `main/registry.c` states that a fresh clone builds exactly
   one app. TID is therefore gated by the CMake option `TORGET_WITH_TIME` (the
   Buddy convention), off by default; the owner enables it in their build.
3. **Button-edge quadrant** is a constant measured on the unit, not derived from
   source.

## Open items for the plan

- Exact touch layout and typography of the three views (design step).
- Whether the quadrant accessor needs a pure-function seam for the host test.
