# v1.1.0-49 on torget-216-02 — first glass for the merge card — 2026-10-08

## Outcome

**THE UPDATE WENT IN, THE NOTICE FIX WORKS ON THE GLASS, AND THE MERGE CARD
APPEARED.** On the 2.16 unit with USB MAC `44:BD:8D:60:DC:F0` the owner
answered UPDATE READY with LATER on `v1.1.0-45-ga3bcbbf`: the takeover left the
glass (#29). The owner then opened the window from SETTINGS; the image
`v1.1.0-49-g8a83c3a` arrived in 17.4 s, the panel restarted into it and the
boot-health gate approved it. With two pull requests waiting on the host, the
owner saw the READY TO MERGE card (#28) after the restart.

The owner was at the panel; the agent read the USB console. Nothing was
flashed over USB; the image was OTA-installed on the owner's request with the
corrected `tools/ota-flash.sh` (#31), its first real delivery.

## Method

One listener on `/dev/cu.usbmodem101` (pyserial, 115 200 baud, DTR and RTS low
before opening, read only), opened once for the whole session. Opening it
reset the panel once (`rst:0x15`), as recorded before.

## Evidence (console, offsets in ms since each boot)

```
I (1129) torget: boot: torget v1.1.0-45-ga3bcbbf ... omstartsorsak USB-reset (11)
I (52348) ota-service: uppdatering annonserad (v1.1.0-49-g8a83c3a) — notisen tar glaset
I (65852) ota-service: notisen avfärdad med ett tryck — åter om en timme
I (604380) ota-service: uppladdning: 2106832 byte mot ota_1
I (621778) ota-service: avbild ota_1 vald för nästa boot (version v1.1.0-49-g8a83c3a)
I (1143) torget: boot: torget v1.1.0-49-g8a83c3a ... omstartsorsak mjukvaruomstart (3)
I (9219) boot-health: avbilden godkänd
```

## Findings

- **The dismissed takeover leaves the glass.** Seen by the owner and logged.
- **The merge card reaches the glass** and the poller's first fetch, about 33 s
  after boot, did not crash the panel: no panic, watchdog or stack-overflow
  line in the first 264 s.
- **Memory on `v1.1.0-49`, window closed, 23 heap probes:** internal free
  98 935 to 108 855 B, largest DMA block 31 744 B in every probe, against the
  11 520 B flush and the 21 248 B audio gate. On `v1.1.0-45` before the
  update: 102 335 to 125 831 B free, DMA block 31 744 to 49 152 B. The
  all-time low for internal free on `v1.1.0-49` was 58 759 B, reached between
  the probes at 24 s and 34 s, where the first merge-queue fetch falls; the
  probe samples every 10 s, so the DMA block at that instant was not seen.
- **UI-lock timeouts continue:** two `Failed to acquire LVGL lock` in 264 s on
  `v1.1.0-49`, three in ten minutes on `v1.1.0-45` (OBS-44, still open).
- **The upload was fast for no known reason** (OBS-45 has the figures).

## What this does not cover

- No static AMOLED review of the card: colours, alignment and the 45 s pulse
  were not judged, only that the card showed.
- The merge-queue task's stack high-water mark: the firmware does not log it.
- Dismissing the card, a second pull request arriving, a source going down,
  and the card behind NEEDS YOU were not exercised on the glass.
- A capture of four minutes says nothing about hours.
