# TID meeting meter — physical review — 2026-11-02

## Outcome

**THE MEETING METER WAS SEEN AND ACCEPTED ON `torget-216-02`.** On the 2.16
unit with USB MAC `44:BD:8D:60:DC:F0`, the owner reviewed the static faces of
PR #27 (`claude/meeting-cost`) in two rounds:

1. `v1.1.0-94-gd501bcb` (OTA 20:33, built outside the repository with
   `-DTORGET_WITH_TIME=ON` and `TK_TID_SOUND 1`): MEETING came up with the
   steppers on the buttons-up face; the owner's verdict on the setup face,
   the steppers and a running meeting was "allt ser ut som det ska"
   (everything looks as it should). The owner's first minutes with the
   steppers found one behaviour wrong: a quick tap that missed − or + by a
   few pixels landed on the background, and the background meant "start",
   so a meeting began by mistake.
2. `v1.1.0-95-g27e970b` (OTA 21:15, same build path): in the setup face only
   the band over the hourly price starts a meeting; the rest of the face
   ignores taps while the steppers are up. The owner's verdict after quick
   taps around − / +, a tap on the big number, pause and RESET: "nu känns
   det bra" (now it feels right).

No change to layout, type or colour was asked for. The stepper hit areas
(− / + halves, 80 × 84 and 114 × 84 px) were usable with a finger.

## Device evidence (serial console, second round)

The USB console was attached and tailed during the second OTA
(`idf.py` env, plain pyserial reader, no reset):

```
21:15:28 torget: boot: torget v1.1.0-95-g27e970b (byggd Nov  2 2026 21:01:14, IDF v5.5.2), omstartsorsak mjukvaruomstart (3)
21:15:28 torget: omstartsliggare: boot #53 sedan liggaren initierades; efter PANIK 0, vakthund 0, BROWNOUT 0
21:15:28 boot-health: första boot på ota_0: hälsogrinden aktiv (8 s minimum, 15 s deadline)
21:15:36 boot-health: avbilden godkänd: alla lokala bevis på plats
```

`/api/ota/status` answered `running_version: v1.1.0-95-g27e970b`,
`running_partition: ota_0`. The first round's image was confirmed by the
owner reading MEETING on the glass; its boot-health verdict was not captured
(no console then), and the re-armed window was never observed open from the
host, which is consistent with the owner's KEY3 presses closing it.

## Side findings, not this PR's

- **OBS-45 measured from the device side for the first time.** The second
  round's first upload stalled: `ota-service: strömmen bröts efter 309088
  byte` after 98 s, with the panel's own pollers failing in the same window
  (`getaddrinfo() returns 202` for `ftornberg.local`, `Connection timed out
  before data was ready`, `Incomlete data received`) and several
  `esp_lv_adapter_lock(751): Failed to acquire LVGL lock` (OBS-44). The
  retry went through in 511 s (HTTP 202) where the day's first OTA took
  21 s. The panel's power source was unchanged between the fast and the
  slow upload (owner), so this is not the USB-power symptom in
  `docs/observability.md`; the cause is still unmeasured. The console log is
  in the session's scratch directory only.
- **`panic stop from the device` in the tokenserver log is not a crash.**
  It is the KEY3 medium-hold-and-release gesture with the maintenance window
  closed (`platform/button_arbitration.c`, `TG_BUTTON_PANIC`), which denies
  parked Needs You decisions. Seen at 20:35:09 with none pending.
- **A launcher hop on quick taps was reported once and then re-attributed by
  the owner** to the background tap starting the meeting. The touch input
  runs in interrupt mode (`esp_lv_adapter_input_touch.c`: no new interrupt
  means "last known state"), which could in principle hold a press and
  turn it into a long press; no touch read error appeared in 20 minutes of
  console during the owner's tapping, so nothing was changed there.

## What this does not cover

- The dynamic behaviour over a long meeting (the ring's hourly lap, the
  10 h cap) was not watched on the glass; it is host-tested.
- NVS persistence of the setup across a power cycle was not exercised on
  the panel.
- Other units have not been reviewed.

Nothing was flashed over USB; both images were OTA-installed on the owner's
explicit request ("kan du köra flashen nu?", "flasha").
