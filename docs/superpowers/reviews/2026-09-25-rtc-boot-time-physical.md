# RTC boot time and battery badge — physical review — 2026-09-25

## Outcome

**PHYSICAL STEP 3 OF THE POWER DESIGN PASSED. On the 2.16 unit with USB MAC
`44:BD:8D:60:DC:F0`, running `v1.1.0-26-ga3c84a3` (the `dev` squash of parts
A, B and #8), a PMU power-key cycle with the cell fitted booted with
`omstartsorsak strömpåslag`, logged `tid från RTC: 2026-09-25 08:34 UTC` at
3.2 s uptime, seven seconds before WiFi was up, and the night schedule saw a
valid clock one second later. THE AXP2101 DECODE IS CONFIRMED ON THE UNIT
(three boots, charging at 88–95 %, 4.09–4.14 V). THE PCF85063 DOES NOT KEEP
TIME THROUGH A POWER LOSS WITH THE CELL DISCONNECTED: two such boots logged
`RTC opålitlig (OS=1, …)`, which the trust rule handled as designed. STEPS 1
(percent over an hour), 4 (night dimming with a moved window) AND 5 (part C
shutdown) ARE NOT EXERCISED. The registry's `unit_verified` is the owner's to
set; this note is the evidence it would cite.**

The owner was at the glass; the agent read the USB console. Nothing was
flashed by the agent: the image was OTA-installed by the owner from `dev`
before the session (the banner names it, no `-dirty`).

## Method

A passive listener on `/dev/cu.usbmodem101` (pyserial, 115200, DTR/RTS left at
their defaults, read-only) appended every console line with a wall-clock
timestamp. Four boots were captured. The first listener lowered DTR while RTS
stayed high, which is the USB-Serial-JTAG reset pattern; that produced boot 1
(`USB-reset`) and was corrected before the others. `idf.py monitor` was not
used because it resets the target on attach.

The unit is the second 2.16 board (not `torget-home-01`, whose registry row
still says `battery: not_fitted` and `v1.0.0-67`). This PR registers it in
`spec/device-units.yaml` as `torget-216-02` with a provisional friendly
name, because a physical-test source must cite a known unit; the spec and
this note also identify it by USB MAC.

## Boot 1 — 09:57, soft reset (the listener's own USB reset)

```
rst:0x15 (USB_UART_CHIP_RESET),boot:0x2b (SPI_FAST_FLASH_BOOT)
I (1121) torget: boot: torget v1.1.0-26-ga3c84a3 (byggd Sep 25 2026 09:22:05, IDF v5.5.2), omstartsorsak USB-reset (11)
I (1145) torget: tidszon: CET-1CEST,M3.5.0,M10.5.0/3
I (1145) torget: klockan behållen över omstarten: 2026-09-25 07:57 UTC (källa RTC + NTP)
I (1148) torget: omstartsliggare: boot #8 sedan liggaren initierades; efter PANIK 0, vakthund 0, BROWNOUT 0
I (3198) torget: natt: dag (09:57, schema 2300–0700, klocka giltig)
I (3207) axp2101: råbyten: status 0x38 0x32 vbat 0x0f 0xf9 pct 88
I (3213) torget: batteri: laddar
I (3226) pcf85063: PCF85063 hittad (Control_1 0x00)
I (3227) torget: systemklockan är redan satt, RTC:n lämnas orörd
I (9822) torget: WiFi uppe ("#Telia-547890")
I (18091) torget: tid omsynkad från SNTP
I (18091) torget: tid synkad
I (18428) torget: RTC uppdaterad från SNTP
```

What it shows:

- The soft-restart path: the clock and its provenance survived, the RTC was
  left alone, the schedule had a valid clock before any network. Local
  09:57 against UTC 07:57 is the configured Stockholm zone.
- `källa RTC + NTP` means an earlier boot on this firmware had already
  applied the RTC; boot 4 below reproduces that with the line captured.
- AXP2101 STATUS1 `0x38`: VBUS good (bit 5), battery present (bit 3).
  STATUS2 `0x32`: direction 01 = charging (bits 6:5), status 010 (bits 2:0,
  not 4 = done). VBAT `0x0f 0xf9` = 4089 mV. Gauge 88 %. The policy said
  `laddar`. All four fields agree with a cell on a charger.
- One cosmetic wart: `tid omsynkad från SNTP` precedes `tid synkad`, because
  the restored provenance makes the first sync of this boot look like a
  resync. Two lines for one event; a follow-up, not a defect.

## Boot 2 — 10:05, power-on with the cell disconnected

```
I (1123) torget: boot: torget v1.1.0-26-ga3c84a3 (…), omstartsorsak strömpåslag (1)
I (1148) torget: omstartsliggare: boot #10 …; efter PANIK 0, vakthund 0, BROWNOUT 0
I (3191) torget: natt: dag (--:--, schema 2300–0700, klocka saknas)
I (3200) axp2101: råbyten: status 0x20 0x15 vbat 0x00 0x00 pct 0
I (3205) torget: batteri: okänd
I (3208) pcf85063: PCF85063 hittad (Control_1 0x00)
W (3213) torget: RTC opålitlig (OS=1, UTC-märkt=1, år 2026), tiden väntar på SNTP
I (16132) torget: WiFi uppe ("#Telia-547890")
I (20304) torget: tid synkad
I (20687) torget: natt: dag (10:05, schema 2300–0700, klocka giltig)
I (24088) torget: RTC uppdaterad från SNTP
```

What it shows:

- STATUS1 `0x20`: VBUS only, no battery. The gauge byte 0 with VBAT 0 was
  never a percentage: `present` is false, the badge is unknown, no alarm.
- The RTC had lost its oscillator (`OS=1`) although the UTC mark and the
  year survived: the chip's supply dipped below what the oscillator needs
  but held the registers. The trust rule rejected the reading and SNTP set
  the clock 17 s later. **Correct time through a full power loss with no
  cell is not something this hardware delivers**, which is what
  `spec/hardware-capabilities.yaml` has said since 2026-08-10.
- The night line went `--:--, klocka saknas` → `10:05, klocka giltig` on
  the sync: the validity-transition log from the #7 review works.

## Boot 3 — 10:12, power-on with the cell just reconnected

```
I (1126) torget: boot: … omstartsorsak strömpåslag (1)
I (3203) axp2101: råbyten: status 0x38 0x32 vbat 0x10 0x01 pct 89
I (3209) torget: batteri: laddar
W (3222) torget: RTC opålitlig (OS=1, UTC-märkt=1, år 2026), tiden väntar på SNTP
I (13294) torget: tid synkad
I (18422) torget: RTC uppdaterad från SNTP
```

The cell was reconnected during the power-off before this boot, so the RTC
had again been without supply; `OS=1` is expected and the write-back at
18 s gave it a fresh UTC-marked time. VBAT 4097 mV, 89 %, charging.

Between boots 3 and 4 the owner pulled USB with the cell in: **the panel
kept running on the cell** (owner-confirmed; the console is gone while USB
is out, so there is no log of it). The badge state during that window was
not read from the glass — that is step 1's "USB out" half, still open.

## Boot 4 — 10:34, PMU power-key cycle with the cell fitted (step 3)

```
I (1123) torget: boot: torget v1.1.0-26-ga3c84a3 (byggd Sep 25 2026 09:22:05, IDF v5.5.2), omstartsorsak strömpåslag (1)
I (1146) torget: tidszon: CET-1CEST,M3.5.0,M10.5.0/3
I (1147) torget: omstartsliggare: boot #13 sedan liggaren initierades; efter PANIK 0, vakthund 0, BROWNOUT 0
I (3161) torget: natt: dag (--:--, schema 2300–0700, klocka saknas)
I (3170) axp2101: råbyten: status 0x38 0x32 vbat 0x10 0x2c pct 95
I (3175) torget: batteri: laddar
I (3189) pcf85063: PCF85063 hittad (Control_1 0x00)
I (3189) torget: tid från RTC: 2026-09-25 08:34 UTC
I (4164) torget: natt: dag (10:34, schema 2300–0700, klocka giltig)
I (10487) torget: WiFi uppe ("#Telia-547890")
I (10730) torget: tid synkad
I (13818) torget: RTC uppdaterad från SNTP
```

What it shows, and it is the whole chain the design asked for:

1. `strömpåslag`: a true power-on, not a reset that would have kept the
   system clock (no `klockan behållen` line).
2. No `RTC opålitlig`: oscillator clean, UTC mark present, year 2026 — the
   cell kept the PCF85063 alive through the PMU's off period.
3. `tid från RTC: 2026-09-25 08:34 UTC` at 3.2 s, before WiFi (10.5 s) and
   SNTP (10.7 s).
4. The schedule evaluated with a valid clock at 4.2 s, local 10:34.
5. SNTP agreed (no clock jump is visible; the write-back three seconds
   later is the routine one).

The ledger stayed at zero panics, watchdogs and brownouts across all
thirteen boots, so the owner's manual restarts were clean power cycles.

## What this changes in the registries

- `power.axp2101` and `rtc.pcf85063atl`: `firmware_enabled` becomes `yes`
  (the firmware reads both since #6/#7), with this note as the physical-test
  source. `unit_verified` is left for the owner to set with a date.
- The RTC's constraint "correct time through complete power loss must not
  be promised" stays: boots 2 and 3 are the evidence for it. What is now
  shown is the narrower claim "the cell backs the RTC through a PMU power
  cycle".
- The unit gets its row in `spec/device-units.yaml` (`torget-216-02`,
  battery fitted, `v1.1.0-26-ga3c84a3` installed); the owner renames it.

## Not exercised

- Step 1's hour of percent tracking and the "USB out" badge state.
- Step 4, night dimming with a moved window (`TG_NIGHT_START_HHMM` in
  `secrets.h`, rebuild, OTA, watch `natt: dimmar` and the glass).
- Step 5, the part C shutdown — not compiled in.
- The V2 board: nothing here transfers to it.

## Side findings for the backlog

- `esp_core_dump_flash: Incorrect size of core dump image: 12594944` on every
  boot: not in `docs/observability.md`. Most likely a never-written coredump
  partition read as garbage; harmless, wants an OBS entry.
- `wifi-creds: kunde inte öppna tgwifi: ESP_ERR_NVS_NOT_FOUND` on every
  boot: no remembered networks yet; expected on this unit.
- `agent-net: agentstatus avvisad: transportfel` right after boot: the host
  had not answered yet; recovered on the next poll.
