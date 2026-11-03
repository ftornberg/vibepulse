# TID keeps the glass without a network — physical review — 2026-11-02

## Outcome

**VERIFIED ON `torget-216-02`.** On the 2.16 unit with USB MAC
`44:BD:8D:60:DC:F0`, running `v1.1.0-96-g480d5a5` (PR #26 merged with `dev`
at #27; OTA on the owner's request at 22:33, boot-health gate approved on the
USB console), the owner blocked the panel from the Wi-Fi at the router with
TID's clock in front and then switched to VibePulse. Timeline from the panel's
serial console and the owner's reports:

| Time | What happened | Evidence |
|---|---|---|
| ~22:51:20 | Router blocks the panel; the firmware starts cycling its saved networks (`WiFi tappat ... orsak 202/201/205, återansluter`, `provar "..." i stället`) | console |
| 22:51–22:54 | **TID's clock stays on the glass.** No NO NETWORK, no self-opening setup window: none of the lines the setup guard prints when it takes the glass (`setupfönstret ser N nät`, `DMA största block ...`, `accesspunkten`) appear for over two minutes, past both the 60 s and the 90 s marks | console; owner: "klockan stod kvar" |
| ~22:54:36 | Owner switches to VibePulse with a short KEY3 press | owner |
| ~22:55:35 | **NO NETWORK on VibePulse**, counted from the switch, not from the IP loss | owner: "nu kom no network" |
| 22:56:06–08 | **The setup window opens by itself** ~90 s after the switch: `DMA största block före öppning: 69632 byte`, `setupfönstret ser 3 nät`, `setupfönstret öppet i tio minuter (VibePulse-setup)` | console |
| 22:56:14 | `setupfönstret stängt` (a KEY3 press) | console |

So `works_offline` on TID and the guard's offline clock
(`tg_wifi_offline_since`, measured from the later of IP loss and a
network-needing app taking the glass) behave on hardware as the host tests
describe: TID is left alone, VibePulse keeps its honest NO NETWORK and setup
steps with their timing intact from the moment it is shown.

Memory during the window: largest internal block 69 632 B before opening,
55 296 B after the portal started (the guard's gate is in `wifi_setup.c`),
free internal heap 148–151 KB throughout.

## What this does not cover

- **A cold boot without a network** (the boot screen leaving at once for an
  offline app, `platform/boot_screen_policy.c`) was not exercised: the panel
  was already up when the Wi-Fi was blocked. It is host-tested
  (`test/test_boot_screen_policy.c`).
- The launcher as the foreground surface while offline was not tried.
- Whether the clock face showed RTC time or `––:––` was not asked
  separately; the owner reported the clock "stood still" (stayed), which is
  the behaviour under review.
- Other units have not been reviewed.

## Side note

The OTA that installed this image needed three attempts: the first two
uploads stalled (`strömmen bröts efter 70800 byte` after 38 s on the second),
the third went through in 223 s. Same pattern as the #27 install an hour
earlier (OBS-45); the link was poor all evening and the cause is still
unmeasured. The owner installed over the air on explicit request; nothing was
flashed over USB.
