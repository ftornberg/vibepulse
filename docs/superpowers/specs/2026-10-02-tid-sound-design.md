# TID sound: a three-note chime when a timer ends

**Date:** 2026-10-02

**Status:** Designed with the owner in this session. Not implemented. Nothing
here promotes `audio.speaker-output` or the unit's `speaker` field; that
happens only after the physical gate below, with the owner at the panel.

**Board scope:** Waveshare ESP32-S3 Touch-AMOLED-2.16 only. The 2.41 V2's audio
path is unexamined and gets no audio code.

**Scope:** A new platform component, `components/torget_audio/`, with one
non-blocking host API; a speaker toggle and a deadline watch in TID; the
three-note chime. The tokenserver, the relays, LABS and every wire contract
are unchanged.

## Goal

The owner's target is "a TID that talks": a chime when a timer ends, and
gentler cues when a focus phase starts or ends. This spec delivers the first
step only:

- **One chime when a timer or a pomodoro phase ends**, played once.
- **Silent during the night schedule** (23:00 to 07:00 by default, the same
  schedule as night dimming; it applies only with a valid clock). It follows
  night dimming itself, including the LABS NIGHT DIM switch (ruled 2026-10-04
  from the owner's "silent while night dimming").
- **A speaker symbol in TID** turns sound on and off; the choice survives a
  restart. Turning sound on plays the chime once, which is both the
  confirmation and the physical test.
- **A build flag, `TK_TID_SOUND`**, decides whether the audio engine is built
  at all. It protects the display's DMA budget on units where sound has not
  passed the physical gate.

## Success criteria

- A timer that ends while another app is on the glass still chimes.
- The chime never plays during the night schedule, never twice for one ending,
  and never delays or blocks the UI.
- After 10 chimes while scrolling VibePulse pages, the largest internal DMA
  block is unchanged and the display never freezes.
- Every refusal (night, busy, OTA, memory, init failure) is logged once with
  its reason; none is silent.

## Non-goals (this spec)

- Different cues for phase start and end (option 3, the follow-up).
- Repeating the chime until a tap.
- A LABS switch (the owner chose the in-TID symbol; LABS can come later).
- Moving the GitHub star chime onto the engine (possible afterwards; its
  existing backend contract in `project_star_chime.h` already fits).
- Volume control on the glass.

## Why the engine owns the hardware

The display flush needs one contiguous internal DMA block of at least 11 520 B
(`480 x 12 x 2`). The panel froze twice when something else ate that block
(`docs/lessons.md`, 2026-08-14 and 2026-08-16); I2S buffers come from the same
memory. So sound **borrows** the hardware per chime and returns it.

The BSP's `bsp_audio_init()` does not fit that model: it opens I2S in duplex
(speaker and microphone, double the DMA buffers), keeps its handles in private
statics, and has no deinit, so a closed channel leaves stale pointers that make
the next init think audio is already running. The engine therefore creates its
own TX-only channel and ES8311 codec, as `main.c` already starts the display
without the BSP's help.

## Architecture

- **`components/torget_audio/audio_policy.[ch]` (pure C, host-tested).**
  - `tg_audio_allowed(...)`: compiled-in, night schedule, OTA busy, already
    playing; returns a reason code.
  - `tg_audio_dma_ok(largest_block)`: largest internal DMA block must be at
    least flush (11 520 B) + audio DMA (`TG_AUDIO_DMA_BYTES`) + 8 KB margin.
  - A tone renderer: `tg_audio_render(cue, frame_offset, buf, frames)` fills a
    chunk of 16-bit mono PCM at 16 kHz on demand. The DONE cue is C6, E6, G6
    (1047, 1319, 1568 Hz), about 0.6 s in total, each note with a short attack
    and release so every note starts and ends at zero amplitude.
- **`components/torget_audio/audio.c` (target only).** One FreeRTOS task and a
  one-slot queue. Per chime:
  1. Measure `heap_caps_get_largest_free_block(MALLOC_CAP_DMA |
     MALLOC_CAP_INTERNAL)`; refuse and log if `tg_audio_dma_ok` fails.
  2. Create a TX-only I2S channel (16 kHz, 16-bit mono, 3 descriptors x 256
     frames, about 1.5 KB DMA), the ES8311 codec via `esp_codec_dev` on the
     shared I2C bus (`bsp_i2c_get_handle()`), power amplifier on GPIO46.
  3. Write PCM in 256-frame chunks rendered on the fly, then 20 ms of silence.
  4. Tear down in reverse order; measure again and warn if more than 256 B is
     missing.
  - Volume is a constant, `TG_AUDIO_VOLUME`, starting low; the physical gate
    sets it.
  - Any init error tears down what exists, logs and drops that chime. Three
    failures in a row disable sound until the next boot, logged once.
  - The task never touches LVGL. The I2C bus is shared with touch, IMU, PMU and
    RTC through ESP-IDF's bus lock.
- **Host API in `platform/torget.h`:** `bool torget_audio_play(tg_audio_cue
  cue)`. Non-blocking; false means refused. The simulator implements it by
  logging `audio: DONE`.
- **Night and OTA state** come from the platform (`main.c` already tracks the
  night decision and the OTA window), so every future app inherits the rule.
- **TID:**
  - A **deadline watch**: a 1 s timer that always runs, also while TID is
    hidden. It ticks both timers and, on a RUNNING to DONE transition, calls
    `torget_audio_play(DONE)` if sound is on. Rendering still happens only
    while TID is visible.
  - A **speaker symbol** at the bottom of the clock and timer faces, inside the
    ring; crossed out when off. Tap toggles; the choice is stored in NVS under
    TID's own namespace (target) and in memory (simulator). Turning it on plays
    the chime once. The symbol is a small A8 image generated by a script, like
    the Wi-Fi status assets.
  - Every pomodoro phase end plays the same DONE cue in this step.

## Testing and verification

- **Host tests (TDD) in `./test/run.sh`:**
  - `audio_policy`: every refusal; the DMA gate one byte under and over.
  - Renderer: note lengths, frequencies (zero crossings), start and end at zero
    amplitude, peak never above the level.
  - TID: the watch chimes exactly once per DONE transition, also while hidden;
    never with sound off; the toggle's pure state.
- **Simulator:** a test runs a timer to DONE with VibePulse in front and finds
  `audio: DONE` in the log; 480 x 480 frames of the speaker symbol on and off
  on the clock and timer faces, reviewed by the owner before any flash.
- **Firmware:** builds with and without `TK_TID_SOUND`, outside the repository
  root; the link map proves the engine is absent without the flag.
- **Physical gate (on the owner's request, OTA):**
  1. Turn sound on in TID and listen. Nothing heard means speaker or amplifier
     path, investigated from there.
  2. Volume: adjust `TG_AUDIO_VOLUME` from the owner's verdict, one OTA per
     change.
  3. Memory: with USB attached, read `idf.py monitor` (read only, no flash)
     while the owner plays 10 chimes and scrolls VibePulse. The DMA margin
     holds and the display never freezes.
  4. Only then: `speaker: verified` with date and unit in
     `spec/device-units.yaml`, and `unit_verified` for `audio.speaker-output`.
- **Documentation:** `docs/time-app.md`, the README's TID section,
  `CHANGELOG.md`; simulator-verified and glass-verified stated separately.

## Decisions

1. Engine borrows per chime (option A); never kept open between chimes.
2. Own TX-only channel and codec, not `bsp_audio_init()`.
3. Control: `TK_TID_SOUND` build flag plus the in-TID speaker symbol.
4. Sound: three rising notes, C6 to E6 to G6, about 0.6 s.
5. Silent during the night schedule; refused during OTA.
6. 2.16 only.

## Open items for the plan

- Default of `TK_TID_SOUND` (proposed: on in the owner's build via
  `secrets.h`, off in `secrets.h.example`).
- Exact `TG_AUDIO_VOLUME` start value and the DMA margin, confirmed by the
  physical gate.
- Whether the speaker symbol also appears on the pomodoro face (proposed: no,
  the dots sit there).
