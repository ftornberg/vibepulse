# TID: clock, pomodoro and meeting meter

TID is an opt-in launcher app for the original 2.16 panel. It has nothing to do
with agents: it turns the panel into a small desk clock, a pomodoro timer and a
meeting meter that shows what a meeting costs, and **the side the panel stands
on chooses which one**. There
is no menu.

> **Status.** Running on the owner's 2.16 panel since 2026-09-26 (OTA), with the
> side mapping below measured there. It is silent (see [Limits](#limits)).
> Nothing here promotes a hardware capability in `spec/`.

## What it does

| Panel position | Face |
|---|---|
| Buttons to the right | **Clock:** the current local time as `HH:MM`, seconds as a ring around it |
| Buttons to the left | **Pomodoro:** 25 min focus, 5 min break, four rounds, then a 15 min long break |
| Buttons up | **Meeting:** set people and kr per person and hour, then watch the cost |
| Buttons down | No face of its own: the app keeps whatever it showed last |

The clock sits with the buttons to the right because that is how the panel
rests on the desk, with the charging cable leading away naturally.

The digital time sits in the middle of the glass and a thin ring runs around it:

- **Clock:** the ring counts the seconds and always moves clockwise: in odd
  minutes the filled part grows from 12 o'clock, in even minutes the empty
  part does, so the ring never jumps from full to empty. It is hidden until the
  panel has a valid time (the digits then read `––:––`, never `00:00`).
- **Pomodoro:** the ring is the time left, full at the start and shrinking to
  nothing. It is hidden while the pomodoro is idle.
- **Meeting:** the ring makes one lap per meeting hour, growing clockwise from
  12 o'clock and starting over each hour. It is hidden before the meeting
  starts.

### Touch

- **Tap** starts, pauses and resumes.
- **RESET** (shown while a pomodoro or a meeting runs or is paused) cancels it. Its touch area
  is larger than the word: most of the bottom of the face.
- **Long press** anywhere opens the launcher, as in every app.
- When a timer ends, a full circle and **DONE** take over the glass in *any*
  face until you tap. In pomodoro the next phase then waits idle for another
  tap; nothing starts by itself while the app is silent.

The pomodoro and the meeting are independent: turning the panel away from a
running one does not stop it, and returning shows the correct time.

### Meeting meter

Replaced the 20 / 40 / 50 minute timer on 2026-11-02 (the owner's choice).

- **Before the meeting** the face shows two steppers: **PEOPLE** (1–99) and
  **KR/H EACH** (100–5 000 kr per person and hour, in steps of 100). The big
  number is the meeting's price per hour (`6 × 900 = 5 400`), so every tap on
  − or + shows its effect. Each stepper splits into two touch areas, − on the
  left half and + on the right, at least 80 × 80 px each. Defaults are 6 people
  at 900 kr; the last setting is saved on the panel (NVS namespace `tg_time`,
  only when a meeting starts) and comes back after a restart.
- **Tap on the big number** (the band with the hourly price and the `TAP TO
  START` line) starts the meeting. The rest of the face ignores taps while the
  steppers are up: on the glass a quick tap that missed − or + by a few
  pixels used to land on the background and start a meeting by mistake
  (2026-11-02). The big number is now the cost so
  far in whole kronor, rounded down, with the elapsed time and the head count
  under it (`KR · 27:40 · 6 PEOPLE`). The steppers are locked while a meeting
  runs or is paused: the cost always uses what the meeting started with.
- **Tap** pauses (the number dims, the time stops) and resumes; **RESET** ends
  the meeting and keeps the setting for the next one.
- **Ten hours is the cap.** The meeting then stands paused. This keeps the
  largest possible sum (99 × 5 000 kr/h × 10 h = 4 950 000) to seven digits,
  which fit inside the ring in the 84 px digits; six digits and more switch
  from 118 px to 84 px.
- **It never chimes** and has no DONE screen: a meeting has no planned end.
- Status: **seen and accepted on `torget-216-02`** (2026-11-02,
  `v1.1.0-95-g27e970b`, OTA): the setup face, the steppers with a finger, a
  running meeting, pause and RESET
  ([physical review](superpowers/reviews/2026-11-02-tid-meeting-meter-physical.md)).
  The hourly ring lap, the 10 h cap and the saved setup across a power
  cycle are host- and simulator-tested only (`--time-app-captures`, frames
  `time-meeting-*`).

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

# Firmware, build only. The build directory must live OUTSIDE the repository:
. ~/esp/esp-idf/export.sh
idf.py -B /tmp/torget-time-build -DTORGET_WITH_TIME=ON build
```

**Why outside the repository.** The tokenserver announces the newest
`<repo>/build*/torget.bin` to the panel, which then shows an **UPDATE READY**
takeover for that version ([`docs/ota.md`](ota.md)). A branch or `-dirty` build
in `build/`, `build-time/`, `build-241/` or any other repo-root `build*/`
directory therefore pops an update screen on the desk panel (this happened on
2026-09-25 with a build directory named `build-time`). Building outside the repository root keeps
the announcement pointed at the firmware the panel actually runs.

Installing on a panel is a separate step that you ask for explicitly; see
[`docs/ota.md`](ota.md) and `CLAUDE.md`. The OTA sender refuses a `-dirty`
build, so commit before any install.

### Measuring which side is which

`components/app_time/time_core.h` maps the rotation the panel measures
(`torget_orientation()`, quarter turns from boot) to a face:
`TG_TIME_ROT_CLOCK`, `TG_TIME_ROT_POMODORO` and `TG_TIME_ROT_MEETING`. On the
owner's 2.16 panel (2026-09-26) buttons up reports 0, buttons left 1 and
buttons right 3, so buttons down is 2 by elimination. Another unit should
report the same (the calibration in `main/rotation.c` is fixed), but if a face
appears on the wrong side, stand the panel on each side, note which face
appears, and edit the three `#define` values.

## Sound

When a focus phase ends, TID plays three rising notes (C6, E6,
G6, about 0.6 s), also while another app is on the glass. When a pomodoro
break ends, short or long, it plays two falling, lower notes instead (G5, E5,
about 0.6 s), so the ear can tell "rest now" from "back to work". The
meeting meter never sounds. It stays silent
during the night schedule (the same 23:00 to 07:00 as night dimming, with a
valid clock) and while the maintenance window is open. Silence follows night
dimming itself: with **NIGHT DIM** switched off in SETTINGS → LABS, the chime
also plays at night (the owner asked for "silent while night dimming").

- **The speaker symbol** at the bottom of the clock turns sound on and off;
  the choice survives a restart. Turning it on plays the chime once. (Until
  2026-11-02 it also sat on the timer's preset page; the meeting meter has
  its steppers there.)
- **It is compiled in only with `#define TK_TID_SOUND 1`** in `secrets.h`
  (off in `secrets.h.example`). Without it the panel carries no audio code.
- **Memory first.** The engine borrows the I2S peripheral and the ES8311 codec
  per chime and returns them. Before each chime it checks that the largest
  internal DMA block still covers the display flush (11 520 B) plus the
  audio buffers and an 8 KB margin; otherwise it stays silent and logs
  `ljud nekat: DMA-block ...`. Three failed starts in a row turn sound off
  until the next boot. Every refusal is logged with its reason.

Status: **heard on `torget-216-02`** (2026-10-05, `v1.1.0-40-gaef36a2`): all
three notes clear at `TG_AUDIO_VOLUME` 45, ten chimes with the largest DMA
block never below 31 744 B and no freeze
([physical review](superpowers/reviews/2026-10-05-tid-sound-physical.md)).
Every chime starts 160 ms after the tap: the amplifier needs that long to
wake. Other units have not been heard. The break's falling cue was **heard on the same unit**
(2026-10-07, `v1.1.0-44-gb30d689`): the owner ran a focus phase and then a
break to their ends and judged the cue good at the unchanged volume
([physical review](superpowers/reviews/2026-10-07-tid-break-cue-physical.md)).

## Limits

- **No cue when a phase starts, and no repeat.** Starting a phase is your own
  tap, so it is silent; repeating a chime until a tap is a later step.
- **Needs You takes the glass from TID.** When an agent needs you, the panel
  switches to VibePulse so the question is seen, and returns to TID after the
  answer, unless you switched app yourself meanwhile. A running pomodoro or
  meeting keeps counting underneath. Seen on the owner's 2.16 panel on 2026-09-26
  (`v1.1.0-32-g96d6947`): NEEDS YOU came up over the clock face, for a
  manual-mode request where the panel offered only to dismiss it, and TID came
  back after it was dismissed.
- **When Claude waits for you, TID lends the glass for 45 s.** Claude Code
  reports "waiting" when it has finished its answer, asks a question or needs
  permission. The panel then shows VibePulse's pulsing NEEDS YOU card for up to
  45 s (a DONE card: 10 s) and hands the glass back to TID. While Claude still
  waits, a small orange Claude icon sits above the clock; it goes away when
  Claude works again or the card is dismissed in VibePulse. Seen on the owner's
  2.16 panel on 2026-09-30 (`v1.1.0-37-g7d28aa8`): the pulse came up over the
  clock after Claude's turn ended, the clock came back, and the icon sat above
  the digits, sized well for a notification.
- **Buttons down has no TID face of its own** (it keeps the last face). That
  pose used to render every app as smeared streaks; the cause was a platform
  rotation gap, not TID, and is fixed in source (OBS-42 in
  `docs/observability-backlog.md`), verified clean on the owner's panel on
  2026-09-30.
- **Without a working IMU the face stays where it was** (the clock after boot);
  the auto-rotation reports no orientation and TID keeps the last face.
- **Wall-clock time** comes from the RTC or SNTP like the night dimming does. The
  clock face uses local time from `TG_TIMEZONE`.
- **Without a network TID keeps the glass.** TID declares `works_offline`
  in the app contract. While it (or the launcher) is shown, the boot screen
  steps aside at once, and neither NO NETWORK nor the self-opening Wi-Fi setup
  window takes the glass. Before 2026-11-02 they took turns covering the clock
  for as long as the network was missing. The panel still boots into
  VibePulse, so one KEY3 press reaches TID. If the setup window has already
  opened (after 90 s on VibePulse with no network), the first press closes it
  and the second one moves to TID. The clock reads the RTC time when the RTC
  holds a trusted time, which it does once any earlier boot has synced over
  SNTP; a never-set or untrusted RTC shows `––:––` until the network returns.
  Timers need no wall clock at all. Host-tested; not yet verified on the
  physical panel.

## Design notes

The pure logic (`time_core`, `time_present`) has no LVGL and no system clock, and
is tested on the host with `./test/run.sh`. The pomodoro stores a deadline in
the device's monotonic clock and computes the time left from it; the meeting
stores when it (adjusted for pauses) began and computes the elapsed time and
cost from that. Neither needs to tick to stay correct across a long absence. The LVGL layer only renders a view model and forwards
touches; taps use `LV_EVENT_SHORT_CLICKED` so a long press that opens the
launcher never also toggles a timer.

The design and plan live in
[`superpowers/specs/2026-09-25-time-app-design.md`](superpowers/specs/2026-09-25-time-app-design.md)
and
[`superpowers/plans/2026-09-25-time-app.md`](superpowers/plans/2026-09-25-time-app.md).
