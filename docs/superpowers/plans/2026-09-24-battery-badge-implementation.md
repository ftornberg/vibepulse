# Battery Badge Implementation Plan (part A of the power design)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put the fitted battery on the glass: an AXP2101 reader, a pure battery policy, a top-layer badge bottom-right with a percentage, two ABOUT rows and a NIGHT DIM LABS switch, all host-tested and simulator-reviewed before one OTA push.

**Architecture:** A new `components/torget_power/` package holds the AXP2101 register driver (target only) and two pure policies, `battery_policy` and `night_policy`, that compile on the host. `platform/battery_badge.c` draws the badge on `lv_layer_top()` from a badge state alone. `main/main.c` gains a 5 s `power_task` that samples the PMU, runs the policy under the UI lock, publishes the badge state and folds the policy's brightness cap into the existing target. The simulator builds the policies and the badge and steps fake samples with key `B`.

**Tech Stack:** ESP-IDF 5.5 (`i2c_master`, FreeRTOS), LVGL 9.5, C11 host tests compiled by `test/run.sh`, the SDL simulator in `sim/`, Python landmark tests in `test/test_vibepulse_visual_landmarks.py`.

**Spec:** `docs/superpowers/specs/2026-09-24-battery-badge-and-rtc-night-dim-design.md`

**Out of this plan (parts B and C):** RTC boot time and write-back, the night schedule's wiring in `main.c`, and the gated shutdown. `night_policy` is written and tested here because it is pure and the LABS switch lands here; `main.c` does not call it yet.

## Global Constraints

- Board scope is the Waveshare 2.16 only (`TG_DISPLAY_WIDTH 480`). Nothing here touches the 2.41 V2 board files.
- No AXP2101 register write except enabling the battery-voltage ADC channel (register `0x30`, bit 0). Charge current, termination voltage and thermal registers are never written.
- Percent shown is the PMU fuel gauge rounded to an integer. Never estimate from voltage. Missing data renders as no number, never `0`.
- Logs are transitions, one line per state change, Swedish like the rest of the firmware log (`batteri: laddar`).
- Glass text is English uppercase like the rest of the glass (`NIGHT DIM`, `POWER`, `CLOCK`).
- UI behaviour lives in `platform/` or the pure policy, never in `main/main.c` or `sim/main.c` (AGENTS.md "Värdlagren är tunna").
- Pure policies have no ESP-IDF includes and are added to `test/run.sh` like `notice_policy`.
- Every visual change is reviewed in the simulator first; nothing in this plan authorises a flash or an OTA push.
- Commit messages: imperative sentence, ending with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.

---

## File map

| File | Responsibility |
| --- | --- |
| `components/torget_power/battery_policy.h/.c` | Pure state machine: sample + time in, badge state / brightness cap / shutdown request out |
| `components/torget_power/night_policy.h/.c` | Pure: local time + switch + schedule in, "night applies" out |
| `components/torget_power/axp2101.h/.c` | Target-only I2C reader for the PMU |
| `components/torget_power/CMakeLists.txt` | Registers the package, `axp2101.c` only on the target |
| `platform/battery_badge.h/.c` | LVGL badge on the top layer; knows only badge state and percent |
| `platform/settings_menu.h/.c` | ABOUT gains POWER and CLOCK rows and a setter for them |
| `components/app_tokens/labs_features.h/.c` | Sixth LABS feature `TK_LABS_NIGHT_DIM`, record version 2 migration |
| `components/app_tokens/app_tokens_config.h` | `TK_NIGHT_ENABLED_DEFAULT` |
| `secrets.h.example` | Documents `TG_NIGHT_START_HHMM`, `TG_NIGHT_END_HHMM`, `TK_NIGHT_ENABLED_DEFAULT` |
| `main/main.c` | `power_task`, badge publish, brightness cap, ABOUT values |
| `main/CMakeLists.txt` | Depends on `torget_power` |
| `sim/main.c`, `sim/CMakeLists.txt` | Key `B`, four static QA frames, builds the new sources |
| `test/test_battery_policy.c`, `test/test_night_policy.c`, `test/run.sh` | Host tests |
| `test/test_labs_features.c` | Sixth feature and migration |
| `test/test_vibepulse_visual_landmarks.py` | Four new frames with pixel landmarks |
| `README.md` | Key `B` in the simulator key list; badge section with a simulator frame |
| `CHANGELOG.md` | `Unreleased` entry |

---

### Task 1: `battery_policy` — states, hysteresis, dwell

**Files:**
- Create: `components/torget_power/battery_policy.h`
- Create: `components/torget_power/battery_policy.c`
- Create: `test/test_battery_policy.c`
- Modify: `test/run.sh` (after the `boot_health_policy` block, around line 285)

**Interfaces:**
- Produces:

```c
typedef enum {
  TG_BATT_UNKNOWN, TG_BATT_CHARGING, TG_BATT_FULL,
  TG_BATT_ON_BATTERY, TG_BATT_LOW, TG_BATT_CRITICAL,
} tg_batt_state;

typedef struct {
  bool valid;      /* the read succeeded */
  bool present;    /* battery connected */
  bool vbus;       /* USB power present */
  bool charging;   /* PMU says current flows into the cell */
  bool charge_done;
  int percent;     /* 0..100, or -1 when the gauge is invalid */
  int mv;          /* battery voltage, or -1 */
} tg_batt_sample;

typedef struct {
  tg_batt_state state;
  int failures;            /* consecutive invalid samples */
  int64_t critical_since_us; /* 0 when not in the ≤5 % dwell */
  bool shutdown_armed;     /* CONFIG_TG_POWER_SHUTDOWN, set by the host */
} tg_batt_policy;

typedef struct {
  tg_batt_state state;
  int percent;     /* -1 when no number should be drawn */
  int bright_cap;  /* 100 = no cap, else the cap in percent */
  bool shutdown;   /* true exactly once when the sequence must start */
  bool changed;    /* state or percent differs from the previous call */
} tg_batt_verdict;

#define TG_BATT_LOW_PCT 20
#define TG_BATT_LOW_EXIT_PCT 23
#define TG_BATT_CRITICAL_PCT 5
#define TG_BATT_CRITICAL_EXIT_PCT 8
#define TG_BATT_CRITICAL_DWELL_US (30LL * 1000000LL)
#define TG_BATT_FAILURES_TO_UNKNOWN 3
#define TG_BATT_NIGHT_CAP 20

tg_batt_verdict tg_batt_update(tg_batt_policy *p, const tg_batt_sample *s, int64_t now_us);
const char *tg_batt_state_name(tg_batt_state s); /* "okänd", "laddar", "full", "på batteri", "låg", "kritisk" */
```

- [ ] **Step 1: Write the failing tests**

`test/test_battery_policy.c`:

```c
#include <stdio.h>

#include "../components/torget_power/battery_policy.h"

static int failures;

static void check(const char *what, int condition) {
  if (!condition) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

static tg_batt_sample sample(bool vbus, bool charging, bool done, int pct) {
  tg_batt_sample s = {0};
  s.valid = true; s.present = true; s.vbus = vbus;
  s.charging = charging; s.charge_done = done; s.percent = pct; s.mv = 3900;
  return s;
}

#define SEC(n) ((int64_t)(n) * 1000000LL)

static void test_charging_full_and_on_battery(void) {
  tg_batt_policy p = {0};
  tg_batt_verdict v = tg_batt_update(&p, &(tg_batt_sample){0}, SEC(1));
  check("invalid first sample is unknown", v.state == TG_BATT_UNKNOWN);
  tg_batt_sample s = sample(true, true, false, 71);
  v = tg_batt_update(&p, &s, SEC(2));
  check("charging", v.state == TG_BATT_CHARGING && v.percent == 71);
  check("charging has no cap", v.bright_cap == 100);
  check("charging changed", v.changed);
  v = tg_batt_update(&p, &s, SEC(3));
  check("same sample unchanged", !v.changed);
  s = sample(true, false, true, 100);
  v = tg_batt_update(&p, &s, SEC(4));
  check("full", v.state == TG_BATT_FULL);
  s = sample(false, false, false, 64);
  v = tg_batt_update(&p, &s, SEC(5));
  check("on battery", v.state == TG_BATT_ON_BATTERY && v.percent == 64);
}

static void test_low_with_hysteresis(void) {
  tg_batt_policy p = {0};
  tg_batt_sample s = sample(false, false, false, 21);
  tg_batt_update(&p, &s, SEC(1));
  s.percent = 20;
  tg_batt_verdict v = tg_batt_update(&p, &s, SEC(2));
  check("20 is low", v.state == TG_BATT_LOW);
  check("low caps brightness", v.bright_cap == TG_BATT_NIGHT_CAP);
  s.percent = 22;
  v = tg_batt_update(&p, &s, SEC(3));
  check("22 stays low", v.state == TG_BATT_LOW);
  s.percent = 23;
  v = tg_batt_update(&p, &s, SEC(4));
  check("23 leaves low", v.state == TG_BATT_ON_BATTERY);
}

static void test_critical_needs_dwell_and_vbus_cancels(void) {
  tg_batt_policy p = {0};
  p.shutdown_armed = true;
  tg_batt_sample s = sample(false, false, false, 5);
  tg_batt_verdict v = tg_batt_update(&p, &s, SEC(0));
  check("5 % at once is low, not critical", v.state == TG_BATT_LOW);
  v = tg_batt_update(&p, &s, SEC(29));
  check("29 s is still low", v.state == TG_BATT_LOW && !v.shutdown);
  v = tg_batt_update(&p, &s, SEC(30));
  check("30 s is critical", v.state == TG_BATT_CRITICAL);
  check("armed critical requests shutdown", v.shutdown);
  v = tg_batt_update(&p, &s, SEC(35));
  check("shutdown is requested once", !v.shutdown);
  s.vbus = true; s.charging = true;
  v = tg_batt_update(&p, &s, SEC(36));
  check("vbus leaves critical immediately", v.state == TG_BATT_CHARGING);
}

static void test_critical_dwell_resets_when_interrupted(void) {
  tg_batt_policy p = {0};
  tg_batt_sample s = sample(false, false, false, 4);
  tg_batt_update(&p, &s, SEC(0));
  tg_batt_update(&p, &s, SEC(20));
  s.percent = 9;
  tg_batt_update(&p, &s, SEC(21));
  s.percent = 4;
  tg_batt_verdict v = tg_batt_update(&p, &s, SEC(45));
  check("dwell restarted after the interruption", v.state == TG_BATT_LOW);
  v = tg_batt_update(&p, &s, SEC(51));
  check("critical 30 s after the restart", v.state == TG_BATT_CRITICAL);
}

static void test_unarmed_critical_never_shuts_down(void) {
  tg_batt_policy p = {0};
  tg_batt_sample s = sample(false, false, false, 3);
  tg_batt_update(&p, &s, SEC(0));
  tg_batt_verdict v = tg_batt_update(&p, &s, SEC(40));
  check("critical without arming", v.state == TG_BATT_CRITICAL && !v.shutdown);
}

static void test_critical_exit_hysteresis(void) {
  tg_batt_policy p = {0};
  tg_batt_sample s = sample(false, false, false, 4);
  tg_batt_update(&p, &s, SEC(0));
  tg_batt_update(&p, &s, SEC(31));
  s.percent = 7;
  tg_batt_verdict v = tg_batt_update(&p, &s, SEC(32));
  check("7 stays critical", v.state == TG_BATT_CRITICAL);
  s.percent = 8;
  v = tg_batt_update(&p, &s, SEC(33));
  check("8 leaves critical to low", v.state == TG_BATT_LOW);
}

static void test_failures_and_recovery(void) {
  tg_batt_policy p = {0};
  tg_batt_sample good = sample(true, true, false, 50);
  tg_batt_sample bad = {0};
  tg_batt_update(&p, &good, SEC(1));
  tg_batt_verdict v = tg_batt_update(&p, &bad, SEC(2));
  check("one failure keeps the state", v.state == TG_BATT_CHARGING);
  tg_batt_update(&p, &bad, SEC(3));
  v = tg_batt_update(&p, &bad, SEC(4));
  check("three failures give unknown", v.state == TG_BATT_UNKNOWN);
  check("unknown draws no number", v.percent == -1);
  v = tg_batt_update(&p, &good, SEC(5));
  check("one success recovers", v.state == TG_BATT_CHARGING);
}

static void test_no_battery_is_unknown(void) {
  tg_batt_policy p = {0};
  tg_batt_sample s = sample(true, false, false, 0);
  s.present = false;
  tg_batt_verdict v = tg_batt_update(&p, &s, SEC(1));
  check("absent battery is unknown", v.state == TG_BATT_UNKNOWN && v.percent == -1);
}

static void test_invalid_gauge_hides_number(void) {
  tg_batt_policy p = {0};
  tg_batt_sample s = sample(true, true, false, -1);
  tg_batt_verdict v = tg_batt_update(&p, &s, SEC(1));
  check("charging with no gauge", v.state == TG_BATT_CHARGING && v.percent == -1);
}

int main(void) {
  test_charging_full_and_on_battery();
  test_low_with_hysteresis();
  test_critical_needs_dwell_and_vbus_cancels();
  test_critical_dwell_resets_when_interrupted();
  test_unarmed_critical_never_shuts_down();
  test_critical_exit_hysteresis();
  test_failures_and_recovery();
  test_no_battery_is_unknown();
  test_invalid_gauge_hides_number();
  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("battery policy: ok\n");
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd test && cc -std=c11 -Wall -Wextra -Werror -O1 ../components/torget_power/battery_policy.c test_battery_policy.c -o /tmp/torget-battery-policy-test`
Expected: compile error, `battery_policy.c: No such file or directory`.

- [ ] **Step 3: Write the header**

`components/torget_power/battery_policy.h`:

```c
#ifndef TORGET_POWER_BATTERY_POLICY_H
#define TORGET_POWER_BATTERY_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Batteripolicyn: ren, pollad var femte sekund med PMU:ns mätning.
 * Ingen I2C, ingen LVGL, ingen tid utom den som skickas in — så varje rad
 * i tillståndstabellen (spec 2026-09-24) låses i test/test_battery_policy.c.
 *
 *  UNKNOWN     inget batteri, eller tre ogiltiga mätningar i rad
 *  CHARGING    USB in, ström in i cellen
 *  FULL        USB in, laddning klar
 *  ON_BATTERY  USB borta, > 20 %
 *  LOW         USB borta, <= 20 %  (lämnas uppåt först vid >= 23 %)
 *  CRITICAL    USB borta, <= 5 % i 30 s utan avbrott (lämnas vid >= 8 %)
 *
 * Procenten är PMU:ns bränslemätare avrundad, aldrig något vi räknar fram;
 * -1 betyder "rita ingen siffra". shutdown sätts EN gång, bara i CRITICAL
 * efter hela fördröjningen och bara när värden armerat den.
 */

typedef enum {
  TG_BATT_UNKNOWN,
  TG_BATT_CHARGING,
  TG_BATT_FULL,
  TG_BATT_ON_BATTERY,
  TG_BATT_LOW,
  TG_BATT_CRITICAL,
} tg_batt_state;

typedef struct {
  bool valid;
  bool present;
  bool vbus;
  bool charging;
  bool charge_done;
  int percent; /* 0..100 eller -1 */
  int mv;      /* eller -1 */
} tg_batt_sample;

typedef struct {
  tg_batt_state state;
  int failures;
  int64_t critical_since_us;
  bool shutdown_armed;
  bool shutdown_sent;
  int last_percent;
} tg_batt_policy;

typedef struct {
  tg_batt_state state;
  int percent;
  int bright_cap;
  bool shutdown;
  bool changed;
} tg_batt_verdict;

#define TG_BATT_LOW_PCT 20
#define TG_BATT_LOW_EXIT_PCT 23
#define TG_BATT_CRITICAL_PCT 5
#define TG_BATT_CRITICAL_EXIT_PCT 8
#define TG_BATT_CRITICAL_DWELL_US (30LL * 1000000LL)
#define TG_BATT_FAILURES_TO_UNKNOWN 3
#define TG_BATT_NIGHT_CAP 20

tg_batt_verdict tg_batt_update(tg_batt_policy *p, const tg_batt_sample *s,
                               int64_t now_us);
const char *tg_batt_state_name(tg_batt_state s);

#endif
```

- [ ] **Step 4: Write the implementation**

`components/torget_power/battery_policy.c`:

```c
#include "battery_policy.h"

static tg_batt_state next_state(const tg_batt_policy *p, const tg_batt_sample *s,
                                int64_t now_us) {
  if (!s->present) return TG_BATT_UNKNOWN;
  if (s->vbus) return s->charge_done ? TG_BATT_FULL : TG_BATT_CHARGING;
  int pct = s->percent;
  if (pct < 0) return TG_BATT_ON_BATTERY; /* ingen mätare: aldrig larm på gissning */
  switch (p->state) {
    case TG_BATT_CRITICAL:
      if (pct >= TG_BATT_CRITICAL_EXIT_PCT)
        return pct >= TG_BATT_LOW_EXIT_PCT ? TG_BATT_ON_BATTERY : TG_BATT_LOW;
      return TG_BATT_CRITICAL;
    case TG_BATT_LOW:
      if (pct >= TG_BATT_LOW_EXIT_PCT) return TG_BATT_ON_BATTERY;
      if (pct <= TG_BATT_CRITICAL_PCT && p->critical_since_us &&
          now_us - p->critical_since_us >= TG_BATT_CRITICAL_DWELL_US)
        return TG_BATT_CRITICAL;
      return TG_BATT_LOW;
    default:
      return pct <= TG_BATT_LOW_PCT ? TG_BATT_LOW : TG_BATT_ON_BATTERY;
  }
}

tg_batt_verdict tg_batt_update(tg_batt_policy *p, const tg_batt_sample *s,
                               int64_t now_us) {
  tg_batt_verdict v = {0};
  tg_batt_state before = p->state;
  int before_pct = p->last_percent;

  if (!s->valid) {
    if (++p->failures >= TG_BATT_FAILURES_TO_UNKNOWN) {
      p->state = TG_BATT_UNKNOWN;
      p->critical_since_us = 0;
    }
  } else {
    p->failures = 0;
    /* Fördröjningsklockan: går bara medan USB är borta och pct <= 5. */
    if (!s->vbus && s->present && s->percent >= 0 &&
        s->percent <= TG_BATT_CRITICAL_PCT) {
      if (!p->critical_since_us) p->critical_since_us = now_us ? now_us : 1;
    } else {
      p->critical_since_us = 0;
    }
    p->state = next_state(p, s, now_us);
    if (p->state != TG_BATT_CRITICAL && p->state != TG_BATT_LOW)
      p->shutdown_sent = false;
  }

  v.state = p->state;
  v.percent = (p->state == TG_BATT_UNKNOWN || !s->valid) ? p->last_percent
                                                          : s->percent;
  if (p->state == TG_BATT_UNKNOWN) v.percent = -1;
  if (s->valid && p->state != TG_BATT_UNKNOWN) p->last_percent = s->percent;
  if (p->state == TG_BATT_UNKNOWN) p->last_percent = -1;

  v.bright_cap = (p->state == TG_BATT_LOW || p->state == TG_BATT_CRITICAL)
                     ? TG_BATT_NIGHT_CAP : 100;
  if (p->state == TG_BATT_CRITICAL && p->shutdown_armed && !p->shutdown_sent) {
    p->shutdown_sent = true;
    v.shutdown = true;
  }
  v.changed = (p->state != before) || (v.percent != before_pct);
  return v;
}

const char *tg_batt_state_name(tg_batt_state s) {
  switch (s) {
    case TG_BATT_CHARGING: return "laddar";
    case TG_BATT_FULL: return "full";
    case TG_BATT_ON_BATTERY: return "på batteri";
    case TG_BATT_LOW: return "låg";
    case TG_BATT_CRITICAL: return "kritisk";
    default: return "okänd";
  }
}
```

Note for the implementer: `p->last_percent` starts at 0 in a zero-initialised policy; `test_no_battery_is_unknown` and `test_failures_and_recovery` expect `-1` for `UNKNOWN`, which the explicit assignment covers. If `test_charging_full_and_on_battery`'s "same sample unchanged" fails, the cause is `changed` comparing against a stale `before_pct`; keep `before_pct` read before any update.

- [ ] **Step 5: Add the test to `test/run.sh`**

Insert after the `boot_health_policy` block (the `cc ... test_boot_health_policy.c` lines and the run line that follows them):

```sh
cc -std=c11 -Wall -Wextra -Werror -O1 \
  ../components/torget_power/battery_policy.c \
  test_battery_policy.c \
  -o /tmp/torget-battery-policy-test
/tmp/torget-battery-policy-test
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cd test && cc -std=c11 -Wall -Wextra -Werror -O1 ../components/torget_power/battery_policy.c test_battery_policy.c -o /tmp/torget-battery-policy-test && /tmp/torget-battery-policy-test`
Expected: `battery policy: ok`

- [ ] **Step 7: Commit**

```bash
git add components/torget_power/battery_policy.h components/torget_power/battery_policy.c test/test_battery_policy.c test/run.sh
git commit -m "Add the pure battery policy with hysteresis and a critical dwell

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: `night_policy` — schedule window, midnight wrap, invalid clock

**Files:**
- Create: `components/torget_power/night_policy.h`
- Create: `components/torget_power/night_policy.c`
- Create: `test/test_night_policy.c`
- Modify: `test/run.sh` (after the battery policy block from Task 1)

**Interfaces:**
- Produces:

```c
typedef struct { int start_hhmm; int end_hhmm; } tg_night_schedule; /* 2300, 0700 */
bool tg_night_applies(const tg_night_schedule *s, bool enabled, bool time_valid, int hour, int minute);
```

- [ ] **Step 1: Write the failing tests**

`test/test_night_policy.c`:

```c
#include <stdio.h>

#include "../components/torget_power/night_policy.h"

static int failures;

static void check(const char *what, int condition) {
  if (!condition) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

int main(void) {
  tg_night_schedule wrap = { 2300, 700 };
  check("23:00 starts night", tg_night_applies(&wrap, true, true, 23, 0));
  check("02:30 is night", tg_night_applies(&wrap, true, true, 2, 30));
  check("06:59 is night", tg_night_applies(&wrap, true, true, 6, 59));
  check("07:00 ends night", !tg_night_applies(&wrap, true, true, 7, 0));
  check("12:00 is day", !tg_night_applies(&wrap, true, true, 12, 0));
  check("22:59 is day", !tg_night_applies(&wrap, true, true, 22, 59));
  check("disabled never applies", !tg_night_applies(&wrap, false, true, 2, 0));
  check("invalid clock never applies", !tg_night_applies(&wrap, true, false, 2, 0));

  tg_night_schedule same_day = { 1300, 1500 };
  check("13:00 starts", tg_night_applies(&same_day, true, true, 13, 0));
  check("14:59 applies", tg_night_applies(&same_day, true, true, 14, 59));
  check("15:00 ends", !tg_night_applies(&same_day, true, true, 15, 0));
  check("02:00 outside a same-day window", !tg_night_applies(&same_day, true, true, 2, 0));

  tg_night_schedule empty = { 800, 800 };
  check("equal start and end never applies", !tg_night_applies(&empty, true, true, 8, 0));

  check("garbage hour never applies", !tg_night_applies(&wrap, true, true, 25, 0));

  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("night policy: ok\n");
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd test && cc -std=c11 -Wall -Wextra -Werror -O1 ../components/torget_power/night_policy.c test_night_policy.c -o /tmp/torget-night-policy-test`
Expected: compile error, `night_policy.c: No such file or directory`.

- [ ] **Step 3: Write header and implementation**

`components/torget_power/night_policy.h`:

```c
#ifndef TORGET_POWER_NIGHT_POLICY_H
#define TORGET_POWER_NIGHT_POLICY_H

#include <stdbool.h>

/*
 * Nattschemat: ren funktion. Lokal tid in, "natt gäller nu" ut. Fönstret
 * anges som HHMM (2300, 700); start > slut betyder över midnatt. Utan
 * giltig klocka gäller aldrig schemat — bara inaktivitetsregeln i main.c.
 */

typedef struct {
  int start_hhmm;
  int end_hhmm;
} tg_night_schedule;

bool tg_night_applies(const tg_night_schedule *s, bool enabled,
                      bool time_valid, int hour, int minute);

#endif
```

`components/torget_power/night_policy.c`:

```c
#include "night_policy.h"

bool tg_night_applies(const tg_night_schedule *s, bool enabled,
                      bool time_valid, int hour, int minute) {
  if (!s || !enabled || !time_valid) return false;
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
  int now = hour * 100 + minute;
  int start = s->start_hhmm, end = s->end_hhmm;
  if (start == end) return false;
  if (start < end) return now >= start && now < end;
  return now >= start || now < end; /* över midnatt */
}
```

- [ ] **Step 4: Add the test to `test/run.sh`** (after the battery block):

```sh
cc -std=c11 -Wall -Wextra -Werror -O1 \
  ../components/torget_power/night_policy.c \
  test_night_policy.c \
  -o /tmp/torget-night-policy-test
/tmp/torget-night-policy-test
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd test && cc -std=c11 -Wall -Wextra -Werror -O1 ../components/torget_power/night_policy.c test_night_policy.c -o /tmp/torget-night-policy-test && /tmp/torget-night-policy-test`
Expected: `night policy: ok`

- [ ] **Step 6: Commit**

```bash
git add components/torget_power/night_policy.h components/torget_power/night_policy.c test/test_night_policy.c test/run.sh
git commit -m "Add the pure night schedule policy

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: LABS feature `NIGHT DIM` with record migration

**Files:**
- Modify: `components/app_tokens/labs_features.h` (enum, `TK_LABS_ALL`, `TK_LABS_RECORD_VERSION`)
- Modify: `components/app_tokens/labs_features.c` (`defaults()`, `tk_labs_init()`, `tk_labs_name()`)
- Modify: `components/app_tokens/app_tokens_config.h` (add `TK_NIGHT_ENABLED_DEFAULT`)
- Modify: `secrets.h.example` (document the three night macros)
- Modify: `platform/settings_menu.c:174-179` and `:253-266` (third slot in the GitHub view)
- Modify: `test/test_labs_features.c`

**Interfaces:**
- Produces: `TK_LABS_NIGHT_DIM` (value 5), `TK_LABS_ALL 63u`, `TK_LABS_RECORD_VERSION 0x200u`, `TK_LABS_RECORD_VERSION_V1 0x100u`, `TK_NIGHT_ENABLED_DEFAULT` (0/1).
- Consumed later by `main.c` (part B) through `tk_labs_active(TK_LABS_NIGHT_DIM)`.

- [ ] **Step 1: Write the failing tests**

Append to `test/test_labs_features.c` inside `main()` before the final `return`. Read the file first to keep its existing assertions; the additions are:

```c
  /* NIGHT DIM: sixth feature, defaults from TK_NIGHT_ENABLED_DEFAULT. */
  result = TK_LABS_STORE_EMPTY;
  saved = 0;
  tk_labs_init();
  assert(tk_labs_active(TK_LABS_NIGHT_DIM) == !!TK_NIGHT_ENABLED_DEFAULT);
  assert((saved & ~TK_LABS_ALL) == TK_LABS_RECORD_VERSION);

  /* A v1 record (five features) migrates: old bits kept, night from default. */
  result = TK_LABS_STORE_FOUND;
  saved = TK_LABS_RECORD_VERSION_V1 | 9u; /* BURN RATE + GITHUB PAGE */
  tk_labs_init();
  assert(tk_labs_active(TK_LABS_BURN_RATE));
  assert(tk_labs_active(TK_LABS_GITHUB));
  assert(!tk_labs_active(TK_LABS_TRACKER));
  assert(tk_labs_active(TK_LABS_NIGHT_DIM) == !!TK_NIGHT_ENABLED_DEFAULT);
  assert(!tk_labs_storage_error());
  assert(tk_labs_toggle(TK_LABS_NIGHT_DIM));
  assert((saved & ~TK_LABS_ALL) == TK_LABS_RECORD_VERSION);

  /* An unknown future version stays read-only. */
  result = TK_LABS_STORE_FOUND;
  saved = 0x400u | 1u;
  tk_labs_init();
  assert(tk_labs_storage_error());
  assert(!tk_labs_toggle(TK_LABS_NIGHT_DIM));

  assert(tk_labs_name(TK_LABS_NIGHT_DIM)[0] == 'N');
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd test && cc -std=c11 -Wall -Wextra -Werror -O1 ../components/app_tokens/labs_features.c test_labs_features.c -o /tmp/torget-labs-test`
Expected: compile error, `TK_LABS_NIGHT_DIM undeclared`.

- [ ] **Step 3: Extend the header**

In `components/app_tokens/labs_features.h` replace the enum and the two defines:

```c
typedef enum {
  TK_LABS_BURN_RATE, TK_LABS_TRACKER, TK_LABS_VALUE,
  TK_LABS_GITHUB, TK_LABS_STAR_POPUP, TK_LABS_NIGHT_DIM, TK_LABS_COUNT
} tk_labs_feature;
#define TK_LABS_ALL 63u
/* v1 records carried five features; v2 adds NIGHT DIM. A v1 record is
 * migrated on read (night from its compile-time default) and rewritten as v2
 * on the next toggle. */
#define TK_LABS_RECORD_VERSION_V1 0x100u
#define TK_LABS_RECORD_VERSION 0x200u
```

- [ ] **Step 4: Extend the config header**

Append to `components/app_tokens/app_tokens_config.h` before the final `#endif`:

```c
/* Scheduled night dimming (design 2026-09-24). The LABS row NIGHT DIM
 * toggles it; these are the compiled defaults an unchanged secrets.h gets. */
#ifndef TK_NIGHT_ENABLED_DEFAULT
#define TK_NIGHT_ENABLED_DEFAULT 1
#endif
#ifndef TG_NIGHT_START_HHMM
#define TG_NIGHT_START_HHMM 2300
#endif
#ifndef TG_NIGHT_END_HHMM
#define TG_NIGHT_END_HHMM 700
#endif
```

- [ ] **Step 5: Implement defaults, migration and name**

In `components/app_tokens/labs_features.c`:

```c
static uint8_t defaults(void) {
  return (TK_LABS_ANALYTICS_DEFAULT ? 7u : 0u) |
         (TK_GITHUB_SCREEN_ENABLED ? 8u : 0u) |
         (TK_GITHUB_NOTIFICATIONS_ENABLED ? 16u : 0u) |
         (TK_NIGHT_ENABLED_DEFAULT ? 32u : 0u);
}

void tk_labs_init(void) {
  uint32_t record = 0;
  tk_labs_store_result result = tk_labs_store_read(&record);
  uint32_t version = record & ~TK_LABS_ALL;
  bool v1 = result == TK_LABS_STORE_FOUND && version == TK_LABS_RECORD_VERSION_V1;
  read_only = result == TK_LABS_STORE_ERROR ||
      (result == TK_LABS_STORE_FOUND && !v1 && version != TK_LABS_RECORD_VERSION);
  active = selected = defaults();
  storage_error = read_only;
  if (result == TK_LABS_STORE_FOUND && !read_only) {
    active = selected = (uint8_t)(record & TK_LABS_ALL);
    if (v1) {
      /* Five stored bits are the user's; NIGHT DIM was not a choice yet. */
      active = selected = (uint8_t)((record & 31u) | (defaults() & 32u));
    }
  }
  if (result == TK_LABS_STORE_EMPTY)
    storage_error = !tk_labs_store_write(TK_LABS_RECORD_VERSION | selected);
}
```

and the names array:

```c
  static const char *const names[] = {
    "BURN RATE", "MAX TRACKER", "API VALUE", "GITHUB PAGE", "STAR POPUP",
    "NIGHT DIM"
  };
```

`tk_labs_toggle` already writes `TK_LABS_RECORD_VERSION | next`, which rewrites a migrated v1 record as v2.

- [ ] **Step 6: Give the GitHub LABS view its third slot**

In `platform/settings_menu.c`, `torget_settings_click_slot`:

```c
  } else if (ui.view == VIEW_LABS_GITHUB) {
    if (slot < 3) ui.labs.toggle((int)slot + 3);
    else ui.view = VIEW_MENU;
  }
```

and in `render()`:

```c
      if ((ui.view == VIEW_LABS_ANALYTICS && i < 3) ||
          (ui.view == VIEW_LABS_GITHUB && i < 3)) {
        ...
      } else {
        lv_label_set_text(ui.row_labels[i],
            ui.view == VIEW_LABS_ANALYTICS ? "MORE" : "SETTINGS");
      }
```

The GitHub view loses its BACK row (slot 2 becomes NIGHT DIM); slot 3 still returns to SETTINGS, and KEY3 still closes. Update the comment above `render()` if it describes the old row set.

- [ ] **Step 7: Document the macros in `secrets.h.example`**

Append before `#endif`:

```c
/* Scheduled night dimming: the panel dims to its night level between these
 * local times (HHMM). SETTINGS > LABS > NIGHT DIM turns it on or off without
 * a rebuild; these only set the defaults a fresh panel starts with. */
/* #define TG_NIGHT_START_HHMM 2300 */
/* #define TG_NIGHT_END_HHMM 700 */
/* #define TK_NIGHT_ENABLED_DEFAULT 1 */
```

- [ ] **Step 8: Run the LABS tests and the simulator LABS QA**

Run: `cd test && cc -std=c11 -Wall -Wextra -Werror -O1 ../components/app_tokens/labs_features.c test_labs_features.c -o /tmp/torget-labs-test && /tmp/torget-labs-test`
Expected: exit 0, no assertion output.

Run: `cmake -S sim -B sim/build -G Ninja && ninja -C sim/build && ./sim/build/torget-sim --vibepulse-labs-qa`
Expected: exit 0. `run_vibepulse_labs_qa` in `sim/main.c` walks every LABS view; if it asserts on the visited count, read its loop and include the new feature in the walk.

- [ ] **Step 9: Commit**

```bash
git add components/app_tokens/labs_features.h components/app_tokens/labs_features.c components/app_tokens/app_tokens_config.h secrets.h.example platform/settings_menu.c test/test_labs_features.c
git commit -m "Add the NIGHT DIM LABS switch with a v1 record migration

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: `battery_badge` on the top layer

**Files:**
- Create: `platform/battery_badge.h`
- Create: `platform/battery_badge.c`
- Modify: `sim/CMakeLists.txt` (add `../platform/battery_badge.c`, `../components/torget_power/battery_policy.c`, `../components/torget_power/night_policy.c` to `add_executable(torget-sim ...)`)
- Modify: `sim/main.c` (create the badge after `torget_settings_create()`; key `B`)

**Interfaces:**
- Consumes: `tg_batt_state`, `tg_batt_verdict` from Task 1.
- Produces:

```c
void torget_battery_badge_create(void);              /* once, under the UI lock, after settings */
void torget_battery_badge_set(tg_batt_state state, int percent); /* under the UI lock; dedupes */
void torget_battery_badge_set_covered(bool covered); /* hide while a takeover owns the glass */
```

- [ ] **Step 1: Write the header**

`platform/battery_badge.h`:

```c
#ifndef TORGET_BATTERY_BADGE_H
#define TORGET_BATTERY_BADGE_H

#include <stdbool.h>

#include "../components/torget_power/battery_policy.h"

/*
 * Batteriikonen: nere till höger i sidfoten, på topplagret så den syns på
 * alla appsidor och i SETTINGS. Vet bara om ett tillstånd och en procent —
 * ingen I2C, ingen policy. Döljs medan ett övertagande äger glaset.
 * Alla funktioner kallas under torget_ui_lock().
 */
void torget_battery_badge_create(void);
void torget_battery_badge_set(tg_batt_state state, int percent);
void torget_battery_badge_set_covered(bool covered);

#endif
```

- [ ] **Step 2: Write the widget**

`platform/battery_badge.c`:

```c
#include "battery_badge.h"

#include <stdio.h>

#include "display_geometry.h"
#include "lvgl.h"

extern const lv_font_t plex_ui_12;

/* Sidfotens högra kant: samma marginal som "TO RESET" (22 px) och samma
 * baslinje som sidprickarna (PAGER_Y 456 på 2.16). */
#define BADGE_RIGHT_MARGIN 22
#define BADGE_Y (456 - (TG_VIEWPORT_INSET_Y ? 4 : 0))
#define BODY_W 26
#define BODY_H 13
#define NUB_W 3
#define NUB_H 7
#define BOLT_W 8
#define GAP 4
#define PCT_W 34

#define COL_OUTLINE lv_color_hex(0xBBBBBB)
#define COL_OK      lv_color_hex(0xFFFFFF)
#define COL_CHARGE  lv_color_hex(0x8FBF6A)
#define COL_LOW     lv_color_hex(0xFFD45A)
#define COL_CRIT    lv_color_hex(0xE0533A)
#define COL_MUTED   lv_color_hex(0x8A8F98)

static struct {
  lv_obj_t *root, *body, *fill, *nub, *bolt, *dash, *pct;
  lv_anim_t pulse;
  tg_batt_state state;
  int percent;
  bool covered;
  bool created;
} ui;

static void pulse_cb(void *obj, int32_t v) {
  lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, c, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

void torget_battery_badge_create(void) {
  if (ui.created) return;
  ui.root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ui.root);
  int w = PCT_W + GAP + BOLT_W + GAP + BODY_W + NUB_W;
  lv_obj_set_size(ui.root, w, 16);
  lv_obj_set_pos(ui.root, TG_VIEWPORT_X + TG_DISPLAY_WIDTH - BADGE_RIGHT_MARGIN - w,
                 TG_VIEWPORT_Y + BADGE_Y - 2);
  lv_obj_clear_flag(ui.root, LV_OBJ_FLAG_CLICKABLE);

  ui.pct = lv_label_create(ui.root);
  lv_obj_set_style_text_font(ui.pct, &plex_ui_12, 0);
  lv_obj_set_style_text_color(ui.pct, COL_OUTLINE, 0);
  lv_obj_set_style_text_letter_space(ui.pct, 1, 0);
  lv_obj_set_style_text_align(ui.pct, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(ui.pct, 0, 1);
  lv_obj_set_width(ui.pct, PCT_W);

  int bx = PCT_W + GAP;
  ui.bolt = lv_label_create(ui.root);
  lv_obj_set_style_text_font(ui.bolt, &plex_ui_12, 0);
  lv_obj_set_style_text_color(ui.bolt, COL_LOW, 0);
  lv_label_set_text(ui.bolt, LV_SYMBOL_CHARGE);
  lv_obj_set_pos(ui.bolt, bx, 1);

  bx += BOLT_W + GAP;
  ui.body = box(ui.root, bx, 1, BODY_W, BODY_H, lv_color_black());
  lv_obj_set_style_border_color(ui.body, COL_OUTLINE, 0);
  lv_obj_set_style_border_width(ui.body, 2, 0);
  lv_obj_set_style_radius(ui.body, 3, 0);
  ui.fill = box(ui.root, bx + 3, 4, BODY_W - 6, BODY_H - 6, COL_OK);
  lv_obj_set_style_radius(ui.fill, 1, 0);
  ui.dash = box(ui.root, bx + 9, 7, BODY_W - 18, 2, COL_MUTED);
  ui.nub = box(ui.root, bx + BODY_W, 4, NUB_W, NUB_H, COL_OUTLINE);

  ui.state = TG_BATT_UNKNOWN;
  ui.percent = -1;
  ui.created = true;
  torget_battery_badge_set(TG_BATT_UNKNOWN, -1);
}

static void apply(void) {
  if (!ui.created) return;
  lv_anim_delete(ui.fill, pulse_cb);
  lv_obj_set_style_bg_opa(ui.fill, LV_OPA_COVER, 0);
  bool unknown = ui.state == TG_BATT_UNKNOWN;
  bool vbus = ui.state == TG_BATT_CHARGING || ui.state == TG_BATT_FULL;
  lv_color_t fill = COL_OK;
  if (vbus) fill = COL_CHARGE;
  else if (ui.state == TG_BATT_LOW) fill = COL_LOW;
  else if (ui.state == TG_BATT_CRITICAL) fill = COL_CRIT;
  lv_obj_set_style_bg_color(ui.fill, fill, 0);

  int pct = ui.percent;
  int inner = BODY_W - 6;
  int w = unknown || pct < 0 ? 0 : (inner * pct + 50) / 100;
  if (w < 1 && !unknown && pct >= 0) w = 1;
  lv_obj_set_width(ui.fill, w > 0 ? w : 1);
  if (unknown || pct < 0) lv_obj_add_flag(ui.fill, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(ui.fill, LV_OBJ_FLAG_HIDDEN);
  if (unknown) lv_obj_clear_flag(ui.dash, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(ui.dash, LV_OBJ_FLAG_HIDDEN);
  if (vbus) lv_obj_clear_flag(ui.bolt, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(ui.bolt, LV_OBJ_FLAG_HIDDEN);

  if (pct >= 0 && !unknown) {
    char text[8];
    snprintf(text, sizeof text, "%d%%", pct);
    lv_label_set_text(ui.pct, text);
    lv_obj_clear_flag(ui.pct, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ui.pct, LV_OBJ_FLAG_HIDDEN);
  }

  if (ui.state == TG_BATT_CRITICAL) {
    lv_anim_init(&ui.pulse);
    lv_anim_set_var(&ui.pulse, ui.fill);
    lv_anim_set_exec_cb(&ui.pulse, pulse_cb);
    lv_anim_set_values(&ui.pulse, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_duration(&ui.pulse, 700);
    lv_anim_set_playback_duration(&ui.pulse, 700);
    lv_anim_set_repeat_count(&ui.pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&ui.pulse);
  }

  if (ui.covered) lv_obj_add_flag(ui.root, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(ui.root, LV_OBJ_FLAG_HIDDEN);
}

void torget_battery_badge_set(tg_batt_state state, int percent) {
  if (!ui.created) return;
  if (state == ui.state && percent == ui.percent) return;
  ui.state = state;
  ui.percent = percent;
  apply();
}

void torget_battery_badge_set_covered(bool covered) {
  if (!ui.created || covered == ui.covered) return;
  ui.covered = covered;
  apply();
}
```

Check `LV_SYMBOL_CHARGE` exists in the project's LVGL build (`grep -rn LV_SYMBOL_CHARGE sim/build/_deps/lvgl-src/src/font/lv_symbol_def.h`); it is a built-in FontAwesome glyph but only in fonts compiled with the symbol range. `plex_ui_12` is a project font without it, so if the glyph renders as a box, replace the label with two small `box()` rectangles forming a bolt (a 3 × 5 upper slab offset left and a 3 × 5 lower slab offset right) and delete the label. The simulator frame decides.

- [ ] **Step 3: Build it in the simulator and add key `B`**

`sim/CMakeLists.txt`, inside `add_executable(torget-sim` after `../platform/button_arbitration.c`:

```cmake
  ../platform/battery_badge.c
  ../components/torget_power/battery_policy.c
  ../components/torget_power/night_policy.c
```

`sim/main.c`:

1. Add includes near the other platform includes:

```c
#include "../platform/battery_badge.h"
#include "../components/torget_power/battery_policy.h"
```

2. After the call that creates the settings menu (search for `torget_settings_create();` in the simulator's startup) add `torget_battery_badge_create();`.

3. Add a fake-sample stepper near `apply_max_tracker_fixture`:

```c
/* Key B: steg genom batteritillstånd med riktiga policybeslut på
 * låtsasmätningar — samma policy som firmware, så bilden är sann. */
static int battery_fixture_idx = -1;
static tg_batt_policy battery_policy;
static void apply_battery_fixture(int idx) {
  static const tg_batt_sample fixtures[] = {
    { .valid = false },
    { .valid = true, .present = true, .vbus = true, .charging = true, .percent = 71, .mv = 4020 },
    { .valid = true, .present = true, .vbus = true, .charge_done = true, .percent = 100, .mv = 4180 },
    { .valid = true, .present = true, .percent = 64, .mv = 3900 },
    { .valid = true, .present = true, .percent = 18, .mv = 3650 },
    { .valid = true, .present = true, .percent = 4, .mv = 3400 },
  };
  const int count = (int)(sizeof fixtures / sizeof fixtures[0]);
  battery_fixture_idx = ((idx % count) + count) % count;
  const tg_batt_sample *s = &fixtures[battery_fixture_idx];
  int64_t now = torget_now_us();
  tg_batt_verdict v = tg_batt_update(&battery_policy, s, now);
  if (battery_fixture_idx == 0) {
    /* tre ogiltiga i rad ger UNKNOWN, som på glaset */
    tg_batt_update(&battery_policy, s, now);
    v = tg_batt_update(&battery_policy, s, now);
  }
  if (battery_fixture_idx == 5) {
    /* fördröjningen: 30 s på <= 5 % innan CRITICAL */
    v = tg_batt_update(&battery_policy, s, now + 31 * 1000000LL);
  }
  torget_battery_badge_set(v.state, v.percent);
}
```

4. Extend the key table: change `keys[12]` to `keys[13]`, append `SDL_SCANCODE_B`, change the loop bound to 13, and add before the final `else`:

```c
      else if (i == 12) apply_battery_fixture(battery_fixture_idx + 1);
```

- [ ] **Step 4: Run the simulator and look**

Run: `cmake -S sim -B sim/build -G Ninja && ninja -C sim/build && ./sim/build/torget-sim`
Press `B` six times. Expected, bottom-right: dash outline → `71%` green with bolt → `100%` green with bolt → `64%` white → `18%` yellow → `4%` red pulsing. Press `]` to change page: the badge stays. Hold `K` three seconds: SETTINGS opens with the badge still visible at the footer.

If the badge sits under the app's page dots or overlaps `TO RESET`, adjust `BADGE_Y` or `BADGE_RIGHT_MARGIN` until it clears both with at least 6 px, then note the final values in the header comment.

- [ ] **Step 5: Commit**

```bash
git add platform/battery_badge.h platform/battery_badge.c sim/CMakeLists.txt sim/main.c
git commit -m "Draw the battery badge on the top layer and step it in the simulator

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: ABOUT rows POWER and CLOCK

**Files:**
- Modify: `platform/settings_menu.h` (new setter)
- Modify: `platform/settings_menu.c:46-51` (geometry), `:85` (labels), `:222-228` (creation), `:279-293` (render), `:296-305` (open)
- Modify: `sim/main.c` (feed values before the ABOUT frames)

**Interfaces:**
- Produces:

```c
/* ABOUT's live rows. NULL or empty draws a dash. Deduped; no-op when closed. */
void torget_settings_set_power(const char *text);
void torget_settings_set_clock(const char *text);
```

- [ ] **Step 1: Geometry and rows**

In `platform/settings_menu.c`:

```c
#define SETTINGS_ABOUT_FIRST_LINE_Y 100
#define SETTINGS_ABOUT_LINE_GAP     50
#define SETTINGS_ABOUT_BACK_Y       320
#define ABOUT_VALUE_CAP 40
#define ABOUT_ROWS 4
```

```c
static const char *const ABOUT_LABEL[ABOUT_ROWS] = {
  "FIRMWARE", "ADDRESS", "POWER", "CLOCK"
};
```

Add to the `ui` struct: `char power[ABOUT_VALUE_CAP]; char clock_text[ABOUT_VALUE_CAP];`

In `render()` replace the two `lv_label_set_text(ui.about_values[...])` lines with:

```c
    lv_label_set_text(ui.about_values[0], ui.version[0] ? ui.version : "–");
    lv_label_set_text(ui.about_values[1], ui.ip[0] ? ui.ip : "–");
    lv_label_set_text(ui.about_values[2], ui.power[0] ? ui.power : "–");
    lv_label_set_text(ui.about_values[3], ui.clock_text[0] ? ui.clock_text : "–");
```

Add after `torget_settings_set_address`:

```c
void torget_settings_set_power(const char *text) {
  if (!ui.overlay) return;
  const char *next = text ? text : "";
  if (strncmp(ui.power, next, sizeof ui.power) == 0) return;
  snprintf(ui.power, sizeof ui.power, "%s", next);
  ui.about_dirty = true;
  if (ui.open) render();
}

void torget_settings_set_clock(const char *text) {
  if (!ui.overlay) return;
  const char *next = text ? text : "";
  if (strncmp(ui.clock_text, next, sizeof ui.clock_text) == 0) return;
  snprintf(ui.clock_text, sizeof ui.clock_text, "%s", next);
  ui.about_dirty = true;
  if (ui.open) render();
}
```

Include `<string.h>` if it is not already included.

- [ ] **Step 2: Check the landmark test's fixed coordinates**

`test/test_vibepulse_visual_landmarks.py` around line 823 crops the ADDRESS value at `(74, 226, 406, 258)` derived from `firstLineY 140 + lineGap 62 + 24`. With the new geometry the ADDRESS value row is at `100 + 50 + 24 = 174`; change the crop to `(74, 174, 406, 206)` and the "band just above BACK" check to use `backY 320` (the last value ends at `100 + 3*50 + 24 + 27 = 301`; assert black between 304 and 318). Read the test's comments and update the numbers in the same style.

- [ ] **Step 3: Feed values in the simulator's ABOUT frames**

In `sim/main.c` where `settings-about-found` is dumped (line ~1560), call before the dump:

```c
  torget_settings_set_power("USB · CHARGING 71 %");
  torget_settings_set_clock("RTC + NTP");
```

and before `settings-about-missing`:

```c
  torget_settings_set_power(NULL);
  torget_settings_set_clock(NULL);
```

- [ ] **Step 4: Rebuild, capture, run the landmark test**

Run: `ninja -C sim/build && TORGET_CAPTURE_DIR=/tmp/caps ./sim/build/torget-sim --vibepulse-static-qa && .venv/bin/python test/test_vibepulse_visual_landmarks.py`
Expected: exit 0. Open `/tmp/caps/torget-settings-about-found.bmp` and confirm four label/value pairs and BACK fit without overlap and with the same left alignment as before.

- [ ] **Step 5: Commit**

```bash
git add platform/settings_menu.h platform/settings_menu.c sim/main.c test/test_vibepulse_visual_landmarks.py
git commit -m "Add POWER and CLOCK rows to ABOUT

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Four static QA frames with landmarks

**Files:**
- Modify: `sim/main.c` (`run_vibepulse_static_qa`, after the existing VibePulse frames)
- Modify: `test/test_vibepulse_visual_landmarks.py` (frame list near line 223 and new test methods)

- [ ] **Step 1: Add the frames**

In `run_vibepulse_static_qa()` after the last `dump_frame(...)` of the usage views and before any settings frames:

```c
  /* Batteriikonen (design 2026-09-24): laddar, och kritisk. */
  tokens_show_view(VIEW_CLAUDE_FABLE);
  apply_battery_fixture(1);
  dump_frame("vibepulse-battery-charging");
  apply_battery_fixture(5);
  dump_frame("vibepulse-battery-critical");
  apply_battery_fixture(0);
```

`dump_frame` includes `lv_layer_top()` (comment at line 235), so the badge is in the capture.

The LABS frame with NIGHT DIM comes from `run_vibepulse_labs_qa`'s existing `settings-labs-github` capture, which now shows three rows; no new call is needed.

- [ ] **Step 2: Add landmarks**

In `test/test_vibepulse_visual_landmarks.py` add the two names to the frame list:

```python
    "torget-vibepulse-battery-charging.bmp",
    "torget-vibepulse-battery-critical.bmp",
```

and two tests in `VibePulseVisualLandmarkTests`:

```python
    def test_battery_badge_charging_is_green_bottom_right(self):
        """The badge lives bottom-right on every page and reads charge as green."""
        image = self.image("torget-vibepulse-battery-charging.bmp")
        badge = image.crop((372, 450, 458, 470)).get_flattened_data()
        green = sum(1 for r, g, b in badge if g > 150 and r < 170 and b < 130)
        self.assertGreater(green, 40, "a charging badge draws a green fill")
        ink = sum(1 for p in badge if p != (0, 0, 0))
        self.assertGreater(ink, 120, "the badge draws outline, bolt and percent")

    def test_battery_badge_critical_is_red_and_page_dots_untouched(self):
        image = self.image("torget-vibepulse-battery-critical.bmp")
        badge = image.crop((372, 450, 458, 470)).get_flattened_data()
        red = sum(1 for r, g, b in badge if r > 150 and g < 110 and b < 100)
        self.assertGreater(red, 20, "a critical badge draws a red fill")
        # The pager stays centred and unaffected (design: PAGER_Y 456).
        charging = self.image("torget-vibepulse-battery-charging.bmp")
        self.assertEqual(dot_runs(image, 459), dot_runs(charging, 459),
                         "the badge must not move the page dots")
```

The crop box `(372, 450, 458, 470)` assumes the final `BADGE_RIGHT_MARGIN`/`BADGE_Y` from Task 4; if those were adjusted, move the box so it covers the badge with 4 px of slack on each side, and say so in the test's docstring. `dot_runs` exists at line 123 of the test file; read its signature before using it.

- [ ] **Step 3: Run the gate**

Run: `./test/run.sh`
Expected: all green, including the two new frames.

- [ ] **Step 4: Commit**

```bash
git add sim/main.c test/test_vibepulse_visual_landmarks.py
git commit -m "Capture the battery badge in the static QA matrix

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: AXP2101 reader (target only)

**Files:**
- Create: `components/torget_power/axp2101.h`
- Create: `components/torget_power/axp2101.c`
- Create: `components/torget_power/CMakeLists.txt`
- Modify: `main/CMakeLists.txt` (add `torget_power` to `REQUIRES` or `PRIV_REQUIRES`)

**Interfaces:**
- Consumes: `bsp_i2c_init()`, `bsp_i2c_get_handle()` from `bsp/esp-bsp.h` (the Waveshare BSP), `tg_batt_sample` from Task 1.
- Produces:

```c
esp_err_t tg_axp2101_init(i2c_master_bus_handle_t bus); /* probes 0x34, enables the VBAT ADC */
tg_batt_sample tg_axp2101_read(void);                    /* valid=false on any I2C error */
```

- [ ] **Step 1: Component registration**

`components/torget_power/CMakeLists.txt`:

```cmake
# Torgets kraftpaket. De rena policyfilerna (battery_policy, night_policy)
# värdtestas i test/run.sh och byggs i simulatorn; axp2101.c rör I2C och
# finns bara på targetet. PMU:n läses, aldrig konfigureras (spec 2026-09-24):
# enda skrivningen är att slå på VBAT-ADC:n.
idf_component_register(
  SRCS "battery_policy.c" "night_policy.c" "axp2101.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES esp_driver_i2c esp_timer)
```

In `main/CMakeLists.txt` add `torget_power` beside the other `torget_*` components in the requires list (read the file; it lists `torget_ota` and friends).

- [ ] **Step 2: Header**

`components/torget_power/axp2101.h`:

```c
#ifndef TORGET_POWER_AXP2101_H
#define TORGET_POWER_AXP2101_H

#include "battery_policy.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * AXP2101 på I2C 0x34: LÄSES, konfigureras inte. Laddström, slutspänning
 * och termik är fabriksvärden tills en verifierad ändring finns
 * (spec/hardware-capabilities.yaml, power.axp2101). Enda skrivningen är
 * ADC-kanalen för batterispänning, en mätinställning.
 */
esp_err_t tg_axp2101_init(i2c_master_bus_handle_t bus);
tg_batt_sample tg_axp2101_read(void);

#endif
```

- [ ] **Step 3: Implementation**

`components/torget_power/axp2101.c`:

```c
#include "axp2101.h"

#include "esp_log.h"

static const char *TAG = "axp2101";

/* Registerkartan enligt AXP2101-databladet (och XPowersLib, som Waveshares
 * fabriksdemo använder). Verifiera mot databladet innan du litar på ett
 * fält: den första läsningen loggar råbyten just därför. */
#define REG_STATUS1      0x00 /* bit5 VBUS good, bit3 batteri anslutet */
#define REG_STATUS2      0x01 /* bit6:5 strömriktning 01=laddar 10=urladdar; bit4:2 laddstatus, 4=klar */
#define REG_IC_TYPE      0x03 /* (v & 0xCF) == 0x4A */
#define REG_ADC_ENABLE   0x30 /* bit0 VBAT-ADC */
#define REG_VBAT_H       0x34 /* 14 bitar, 1 mV/LSB, high i [5:0] */
#define REG_VBAT_L       0x35
#define REG_BAT_PERCENT  0xA4 /* 0..100 */

#define ADDR 0x34
#define TIMEOUT_MS 50

static i2c_master_dev_handle_t s_dev;
static bool s_logged_raw;

static esp_err_t rd(uint8_t reg, uint8_t *out, size_t n) {
  return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, TIMEOUT_MS);
}

static esp_err_t wr(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = { reg, val };
  return i2c_master_transmit(s_dev, buf, sizeof buf, TIMEOUT_MS);
}

esp_err_t tg_axp2101_init(i2c_master_bus_handle_t bus) {
  if (!bus) return ESP_ERR_INVALID_ARG;
  i2c_device_config_t cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADDR,
    .scl_speed_hz = 400 * 1000,
  };
  esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
  if (err != ESP_OK) return err;
  uint8_t id = 0;
  err = rd(REG_IC_TYPE, &id, 1);
  if (err != ESP_OK) return err;
  if ((id & 0xCF) != 0x4A) {
    ESP_LOGW(TAG, "oväntat chip-id 0x%02x på 0x34, PMU:n lämnas orörd", id);
    return ESP_ERR_NOT_FOUND;
  }
  uint8_t adc = 0;
  if (rd(REG_ADC_ENABLE, &adc, 1) == ESP_OK && !(adc & 0x01)) {
    err = wr(REG_ADC_ENABLE, adc | 0x01);
    if (err != ESP_OK) ESP_LOGW(TAG, "kunde inte slå på VBAT-ADC: %s", esp_err_to_name(err));
  }
  ESP_LOGI(TAG, "AXP2101 hittad (id 0x%02x), läses var 5 s, konfigureras inte", id);
  return ESP_OK;
}

tg_batt_sample tg_axp2101_read(void) {
  tg_batt_sample s = { .valid = false, .percent = -1, .mv = -1 };
  if (!s_dev) return s;
  uint8_t st[2], v[2], pct;
  if (rd(REG_STATUS1, st, 2) != ESP_OK) return s;
  if (rd(REG_VBAT_H, v, 2) != ESP_OK) return s;
  if (rd(REG_BAT_PERCENT, &pct, 1) != ESP_OK) return s;
  if (!s_logged_raw) {
    s_logged_raw = true;
    ESP_LOGI(TAG, "råbyten: status 0x%02x 0x%02x vbat 0x%02x 0x%02x pct %u",
             st[0], st[1], v[0], v[1], pct);
  }
  s.valid = true;
  s.present = (st[0] & 0x08) != 0;
  s.vbus = (st[0] & 0x20) != 0;
  unsigned dir = (st[1] >> 5) & 0x03;
  unsigned chg = (st[1] >> 2) & 0x07;
  s.charging = dir == 1;
  s.charge_done = chg == 4;
  s.mv = (int)(((v[0] & 0x3F) << 8) | v[1]);
  s.percent = pct <= 100 ? (int)pct : -1;
  return s;
}
```

- [ ] **Step 4: Build for the target**

Run: `. ~/esp/esp-idf/export.sh && idf.py build 2>&1 | tail -5`
Expected: `Project build complete`. Nothing calls the driver yet; this proves the component registers and compiles.

- [ ] **Step 5: Commit**

```bash
git add components/torget_power/CMakeLists.txt components/torget_power/axp2101.h components/torget_power/axp2101.c main/CMakeLists.txt
git commit -m "Add a read-only AXP2101 driver on the BSP I2C bus

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: `power_task` in `main.c`, badge publish, brightness cap, ABOUT values

**Files:**
- Modify: `main/main.c` (includes; a new static section near `rotation`; `app_main` after `sg_rotation_start`; `tick_cb`'s brightness block at lines 765-781; the KEY3 `open_menu` block at 735-749)

**Interfaces:**
- Consumes: `tg_axp2101_init/read` (Task 7), `tg_batt_update` (Task 1), `torget_battery_badge_*` (Task 4), `torget_settings_set_power/clock` (Task 5), `torget_ui_lock/unlock` (existing), `torget_ota_ui_visible()` or the existing "takeover owns the glass" signals used by `tick_cb` (read the `key3_in` struct fill at lines ~690-720 for the exact names).

- [ ] **Step 1: State and the task**

Add near the other `static` state after the brightness defines (line ~82):

```c
#include "axp2101.h"
#include "battery_badge.h"
#include "battery_policy.h"

/* Batteriet: pollas var 5 s av en låg task; policyn körs där, resultatet
 * publiceras till LVGL-tasken under UI-låset. Ljustaket läses av tick_cb. */
#define POWER_POLL_MS 5000
static tg_batt_policy s_batt;
static volatile int s_batt_bright_cap = 100;
static char s_power_text[40];

static void power_task(void *arg) {
  (void)arg;
  for (;;) {
    tg_batt_sample s = tg_axp2101_read();
    tg_batt_verdict v = tg_batt_update(&s_batt, &s, esp_timer_get_time());
    s_batt_bright_cap = v.bright_cap;
    if (v.changed) {
      ESP_LOGI(TAG, "batteri: %s%s%d %%", tg_batt_state_name(v.state),
               v.percent >= 0 ? " " : "", v.percent >= 0 ? v.percent : 0);
      char text[40];
      if (v.state == TG_BATT_UNKNOWN) snprintf(text, sizeof text, "NO BATTERY");
      else if (v.state == TG_BATT_FULL) snprintf(text, sizeof text, "USB · FULL");
      else if (v.state == TG_BATT_CHARGING && v.percent >= 0)
        snprintf(text, sizeof text, "USB · CHARGING %d %%", v.percent);
      else if (v.state == TG_BATT_CHARGING) snprintf(text, sizeof text, "USB · CHARGING");
      else if (v.percent >= 0 && s.mv > 0)
        snprintf(text, sizeof text, "BATTERY %d %% · %d.%02d V", v.percent,
                 s.mv / 1000, (s.mv % 1000) / 10);
      else snprintf(text, sizeof text, "BATTERY");
      torget_ui_lock();
      torget_battery_badge_set(v.state, v.percent);
      torget_settings_set_power(text);
      torget_ui_unlock();
    }
    if (v.shutdown) ESP_LOGW(TAG, "batteri: avstängning begärd men inte kompilerad in (del C)");
    vTaskDelay(pdMS_TO_TICKS(POWER_POLL_MS));
  }
}

static void power_start(void) {
  if (bsp_i2c_init() != ESP_OK || tg_axp2101_init(bsp_i2c_get_handle()) != ESP_OK) {
    ESP_LOGW(TAG, "ingen PMU att läsa, batteriikonen visar okänd");
    return;
  }
  if (xTaskCreate(power_task, "power", 3072, NULL, 2, NULL) != pdPASS)
    ESP_LOGW(TAG, "power-tasken kunde inte skapas, batteriikonen visar okänd");
}
```

The unknown-state log line prints `0 %` after "okänd" only if `percent >= 0`, which it never is for `UNKNOWN`; keep the ternaries as written so the format string has no dangling number.

- [ ] **Step 2: Create the badge and start the task in `app_main`**

After `torget_settings_create();` and its cost report (line ~1152) add, still under the UI lock:

```c
  overlay_cost_mark();
  torget_battery_badge_create();
  overlay_cost_report("battery");
```

After `sg_rotation_start(s_touch);` (line 1113) add `power_start();`. `sg_rotation_start` already calls `bsp_i2c_init`; calling it twice is safe only if the BSP guards it, so read `bsp_i2c_init` in the managed component; if it is not idempotent, call `bsp_i2c_init()` once before `sg_rotation_start` and drop it from `power_start`.

- [ ] **Step 3: Brightness cap in `tick_cb`**

Replace the target computation at lines 765-767 with:

```c
  int target = ((now - s_last_activity_us) > NIGHT_AFTER_US
                && (now - s_last_touch_us) > WAKE_HOLD_US)
               ? BRIGHT_NIGHT : BRIGHT_DAY;
  /* Ljustaket från batteripolicyn: lägsta källan vinner, ingen kan lyfta. */
  if (s_batt_bright_cap < target) target = s_batt_bright_cap;
```

- [ ] **Step 4: Hide the badge under takeovers**

In `tick_cb`, where the code already knows whether the OTA notice/overlay, the WiFi setup window or a Needs You takeover owns the glass (the `key3_in` fields around lines 690-720 are filled from those signals), add one line after those fields are computed:

```c
  torget_battery_badge_set_covered(key3_in.notice_showing || key3_in.ota_window_open ||
                                   key3_in.setup_window_open);
```

Use the real field names from `platform/button_arbitration.h`'s input struct; read it and substitute. Needs You draws its own full-screen layer above the badge already, so no extra signal is needed for it; confirm in the simulator by pressing `S` until a Needs You frame shows.

- [ ] **Step 5: CLOCK row placeholder**

In the `open_menu` block (line ~748) after `torget_settings_open(...)` add:

```c
    torget_settings_set_clock(s_time_synced ? "NTP ONLY" : "NOT SET");
```

`s_time_synced` does not exist yet; add `static bool s_time_synced;` beside the other net state and set it to `true` in `time_sync()` on the success branch (`ESP_LOGI(TAG, "tid synkad")`). Part B replaces this with the RTC-aware text.

- [ ] **Step 6: Build and run the simulator's key3 flow test**

Run: `. ~/esp/esp-idf/export.sh && idf.py build 2>&1 | tail -3`
Expected: `Project build complete`.

Run: `./test/run.sh`
Expected: green. `test_key3_flow.c` compiles `button_arbitration.c` alone, so it is unaffected; if it fails, the input struct change in Step 4 was wider than intended.

- [ ] **Step 7: Commit**

```bash
git add main/main.c
git commit -m "Poll the PMU, publish the battery badge and cap brightness on low battery

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: README, CHANGELOG, and the simulator frames for the README

**Files:**
- Modify: `README.md` (key list at line ~927; a new short section "Battery badge" near the SETTINGS section around line 682, with two frames)
- Modify: `CHANGELOG.md` (`Unreleased` → `Added`)
- Create: `docs/img/vibepulse-battery-charging.png`, `docs/img/vibepulse-battery-critical.png` (converted from the BMP captures)

- [ ] **Step 1: Capture and convert**

Run: `TORGET_CAPTURE_DIR=/tmp/caps ./sim/build/torget-sim --vibepulse-static-qa && .venv/bin/python -c "from PIL import Image; [Image.open(f'/tmp/caps/torget-vibepulse-battery-{n}.bmp').save(f'docs/img/vibepulse-battery-{n}.png') for n in ('charging','critical')]"`
Expected: two 480 × 480 PNGs in `docs/img/`.

- [ ] **Step 2: README**

In the key list sentence at line ~927 add: `` `B` steps the battery badge through charging, full, on battery, low and critical, ``.

Add a section after the SETTINGS section:

```markdown
### Battery badge

A 2.16 panel with the optional 3.7 V cell fitted shows it bottom-right on
every page: green with a bolt on USB, white by percentage on the cell,
yellow at 20 %, red and pulsing at 5 %. The percentage is the PMU's own
fuel gauge, never an estimate, and a missing gauge shows only the icon.
SETTINGS → ABOUT carries the voltage. The charge profile stays at the
PMU's defaults; see the design in
`docs/superpowers/specs/2026-09-24-battery-badge-and-rtc-night-dim-design.md`.

<img src="docs/img/vibepulse-battery-charging.png" width="31%" alt="Week page with a green charging badge and 71 % bottom-right"> <img src="docs/img/vibepulse-battery-critical.png" width="31%" alt="Week page with a red critical badge and 4 % bottom-right">
```

- [ ] **Step 3: CHANGELOG**

Under `## Unreleased` → `### Added`:

```markdown
- **Battery badge.** With the optional cell fitted, the 2.16 panel shows
  charge state and the PMU's own percentage bottom-right on every page,
  caps brightness at the night level below 20 %, and lists power in
  SETTINGS → ABOUT. Read-only: the AXP2101 charge profile is untouched.
  A NIGHT DIM row in LABS lands with it; the schedule it controls ships
  in the next step.
```

- [ ] **Step 4: Commit**

```bash
git add README.md CHANGELOG.md docs/img/vibepulse-battery-charging.png docs/img/vibepulse-battery-critical.png
git commit -m "Document the battery badge with simulator frames

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 10: Simulator review, PR, and the physical steps that follow

- [ ] **Step 1: Full gate**

Run: `./test/run.sh && . ~/esp/esp-idf/export.sh && idf.py build 2>&1 | tail -3`
Expected: green, `Project build complete`.

- [ ] **Step 2: Static review in the simulator**

Run `./sim/build/torget-sim`, press `B` through all six states on the week page, `]` through every page, hold `K` for SETTINGS and open ABOUT and LABS → MORE. Confirm: the badge never overlaps the page dots or `TO RESET`; ABOUT shows four rows and BACK; LABS's second view shows GITHUB PAGE, STAR POPUP, NIGHT DIM and SETTINGS. Save the frames you looked at in the PR description.

- [ ] **Step 3: Open the PR against `dev`**

```bash
git push -u origin HEAD
gh pr create --repo ftornberg/vibepulse --base dev --title "Show the battery on the glass (power design part A)" --body "..."
```

The body lists the spec, the ten tasks, the simulator frames, and states that the AXP2101 is read-only and that no flash was performed. End with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.

- [ ] **Step 4: Hand over for the physical steps**

After merge the owner pushes the build over the air (`idf.py build && GH_REPO=ftornberg/vibepulse tools/ota-flash.sh`, then UPDATE on the glass, with the panel on its charger, not on a Mac USB port). Physical steps 1 and 2 from the spec follow: badge on USB in/out, percent over an hour, ABOUT rows. The first serial boot log carries the `råbyten:` line; compare its bits with the datasheet before believing the state.

---

## Self-review notes

- Spec coverage: state table (T1), hysteresis and dwell (T1), night schedule pure (T2), LABS switch and defaults (T3), badge placement and colours (T4, T6), ABOUT rows (T5), read-only PMU with one ADC write (T7), poll cadence, transition logs, brightness minimum (T8), simulator keys and frames (T4, T6), README and CHANGELOG (T9). Not in this plan by design: RTC (part B), schedule wiring in `main.c` (part B), shutdown (part C).
- The spec says "no register writes"; this plan enables the VBAT ADC bit and says so in Global Constraints and in the driver header. Update the spec's driver paragraph in the same PR: "with one exception behind `CONFIG_TG_POWER_SHUTDOWN` … and one measurement setting: the battery-voltage ADC channel".
- Type consistency: `tg_batt_sample`, `tg_batt_policy`, `tg_batt_verdict`, `tg_batt_state`, `tg_batt_update`, `tg_batt_state_name` are used identically in T1, T4, T7, T8. `torget_battery_badge_create/set/set_covered` in T4 and T8. `torget_settings_set_power/set_clock` in T5 and T8. `TK_LABS_NIGHT_DIM`, `TK_LABS_RECORD_VERSION_V1` in T3 only until part B.
