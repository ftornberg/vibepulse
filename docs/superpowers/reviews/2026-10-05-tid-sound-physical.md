# TID sound — physical review — 2026-10-05

## Outcome

**THE SPEAKER AND THE AUDIO ENGINE PASSED THE PHYSICAL GATE ON `torget-216-02`.**
On the 2.16 unit with USB MAC `44:BD:8D:60:DC:F0`, running
`v1.1.0-40-gaef36a2` (OTA from `dev`, built with `TK_TID_SOUND 1`), the
owner heard the three-note chime from the speaker symbol in TID, judged the
volume (`TG_AUDIO_VOLUME` 45) right, and heard all three notes clearly after
the 160 ms lead-in of #22. Ten chimes played while the owner moved between
TID and VibePulse; none was refused, nothing froze, and no panic or watchdog
appeared. The largest internal DMA block never went below 31 744 B, against
the 11 520 B the display flush needs and the engine's 21 248 B gate.

The owner was at the panel; the agent read the USB console. Nothing was
flashed by the agent over USB; the image was OTA-installed on the owner's
request.

## Method

A listener opened `/dev/cu.usbmodem101` once (pyserial, 115 200 baud, DTR and
RTS set low before opening, read only) and wrote the console to a file for ten
minutes. **Opening the port reset the panel once anyway**
(`rst:0x15 (USB_UART_CHIP_RESET)`), as `docs/lessons.md` warned for the 2.41
V2; the port then stayed open with no further reset. The owner played the
chime by turning sound off and on with the speaker symbol.

## Evidence (console, wall-clock offsets in ms)

```
I (1001) app_init: App version:      v1.1.0-40-gaef36a2
I (31014) audio: spelade DONE: DMA-block före 34816, under 31744 B, stack kvar 940 B
I (39198) audio: spelade DONE: DMA-block före 34816, under 31744 B, stack kvar 940 B
I (42054) audio: spelade DONE: DMA-block före 34816, under 31744 B, stack kvar 940 B
I (44331) audio: spelade DONE: DMA-block före 34816, under 31744 B, stack kvar 936 B
I (57386) audio: spelade DONE: DMA-block före 34816, under 31744 B, stack kvar 940 B
I (62433) audio: spelade DONE: DMA-block före 34816, under 31744 B, stack kvar 940 B
I (77625) audio: spelade DONE: DMA-block före 34816, under 31744 B, stack kvar 940 B
I (86095) audio: spelade DONE: DMA-block före 34816, under 31744 B, stack kvar 936 B
W (86111) audio: möjlig ljudläcka: DMA-block 34816 -> 31744 B
I (221968) audio: spelade DONE: DMA-block före 34816, under 31744 B, stack kvar 940 B
I (229349) audio: spelade DONE: DMA-block före 34816, under 31744 B, stack kvar 940 B
```

## Findings

- **Audible and clean.** The first build (`v1.1.0-39`) played the first note
  faintly: `es8311_enable()` drives the amplifier pin and unmutes in the same
  instant, so the 180 ms C6 played while the amplifier woke. #22 leads every
  chime with 160 ms of silence; on `v1.1.0-40` all three notes were equally
  clear.
- **The engine's real DMA cost is 3 072 B**, not the 1 536 B the spec
  estimated (block 34 816 B before, 31 744 B during). The gate still holds with
  margin; the spec's figure is corrected in the backlog entry below.
- **Stack headroom is about 940 B of the 4 096 B chime task.** Enough, but not
  generous.
- **The leak warning was a coincidence.** The same dip (internal free about
  10 KB lower, largest block 34 816 → 31 744 B) recurs every one to two minutes
  with no chime at all (186, 276, 368 s) and recovers by itself. The engine's
  own before/after held for nine of ten chimes. Logged as OBS-43.
- The internal heap's lowest-ever mark fell from 111 455 B to 73 395 B between
  23.6 s and 35.8 s, a window that included the first chime and the boot's
  network start; it did not fall further over the next nine chimes.

## Registry

This note is the physical-test source `torget-physical-2026-10-05-tid-sound`.
It sets `audio.speaker-output` `unit_verified: "yes"` for `torget-216-02` and
`speaker: fitted` on that unit. It says nothing about `torget-home-01`, whose
speaker remains unknown.
