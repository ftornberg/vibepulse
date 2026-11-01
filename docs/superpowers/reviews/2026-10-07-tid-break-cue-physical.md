# TID break cue — physical review — 2026-10-07

## Outcome

**THE BREAK CUE WAS HEARD AND ACCEPTED ON `torget-216-02`.** On the 2.16 unit
with USB MAC `44:BD:8D:60:DC:F0`, running `v1.1.0-44-gb30d689` (OTA from `dev`,
built with `TK_TID_SOUND 1`; the boot-health gate approved the image), the
owner ran the listening test for #24: a focus phase to its end, a tap to
acknowledge, a tap to start the break, and the break to its end. The owner's
verdict: "Ljudtestet var bra" (the sound test was good). No change was asked
for; `TG_AUDIO_VOLUME` stays 45 and the cue stays G5 then E5.

The concern before the test was that 784 Hz and 659 Hz sit low for the small
onboard speaker and might come out faint. The owner did not report that.

## What this does not cover

- **No console was captured during the listening.** The memory figures for the
  break cue (DMA block during play, stack left) were not read on this unit;
  the engine and its DMA gate are the ones measured on 2026-10-05, and the cue
  is rendered in the same small chunks.
- The verdict is the owner's one-line report, not a note-by-note comparison
  with the rising chime.
- Other units have not been heard.

Nothing was flashed by the agent over USB; the image was OTA-installed on the
owner's request. The OTA itself exposed a separate fault in the UPDATE READY
takeover (#29, `docs/lessons.md` 2026-10-07).
