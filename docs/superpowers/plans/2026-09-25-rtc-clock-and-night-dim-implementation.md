# RTC Clock and Night Dimming Implementation Plan (part B of the power design)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the panel the right time at boot from the battery-backed PCF85063, keep that clock fresh from NTP, and dim the glass to the night level on a fixed local-time schedule that the LABS row NIGHT DIM (shipped in part A) switches on and off.

**Architecture:** A pure `clock_policy` (trust rule for an RTC reading, UTC civil↔epoch conversion, the CLOCK text) joins `night_policy` (already shipped) in `components/torget_power/`; a target-only `pcf85063.c` reads and writes the RTC over the BSP I2C bus. `main/main.c` sets the firmware's timezone once at boot, applies a trusted RTC reading before WiFi, writes the clock back after each SNTP sync, and folds the schedule into the existing brightness target as a third source that can only lower it. The simulator gets the CLOCK text through the same pure function; the bench does not model brightness, so there is no night key (the spec is amended to say so).

**Tech Stack:** ESP-IDF 5.5 (`i2c_master`, `esp_netif_sntp`, newlib `setenv/tzset/localtime_r/settimeofday`), FreeRTOS, LVGL 9.5, C11 host tests in `test/run.sh`, the SDL simulator.

**Spec:** `docs/superpowers/specs/2026-09-24-battery-badge-and-rtc-night-dim-design.md` (sections "Night policy", "`main/main.c`", "Error handling and safety", "Testing", delivery steps 3–4).

**Out of this plan (part C):** the gated low-battery shutdown.

## Global Constraints

- Board scope is the Waveshare 2.16 only. RTC and schedule code in `main.c` sits inside the existing `#ifndef TORGET_BOARD_241_V2` power section; the V2 build keeps today's behaviour.
- The RTC can never move the clock backwards: an RTC reading is applied only at boot while the system clock is unset (year < 2026). After SNTP the network always wins. A reading with the oscillator-stop flag set or a year outside 2026..2099 is ignored.
- The RTC stores UTC. The firmware's local time comes from one POSIX TZ string, `TG_TIMEZONE`, default `"CET-1CEST,M3.5.0,M10.5.0/3"` (Europe/Stockholm rules), overridable in `secrets.h`. Setting it also makes the existing `RUNS OUT DDD HH:MM` line local instead of UTC on the panel; that is documented as a fix.
- Night window default 23:00–07:00 local (`TG_NIGHT_START_HHMM 2300`, `TG_NIGHT_END_HHMM 700`, `TK_NIGHT_ENABLED_DEFAULT 1`, all already defined in `components/app_tokens/app_tokens_config.h`); level `BRIGHT_NIGHT` (20). Without a valid clock (neither RTC applied nor NTP synced) the schedule does not apply.
- The brightness target is the minimum of the inactivity rule, the schedule, and the battery cap; no source can raise it. A touch still raises to `BRIGHT_DAY` for `WAKE_HOLD_US` (30 s) and settles back while the window applies.
- Logs are Swedish transitions only: `tid från RTC …`, `RTC opålitlig …` (once), `ingen RTC att läsa` (once), `RTC uppdaterad`, `natt: dimmar` / `natt: dag`. Never one line per tick or per poll.
- ABOUT CLOCK texts exactly: `RTC + NTP`, `RTC ONLY`, `NTP ONLY`, `NOT SET`.
- Pure policies have no ESP-IDF includes and join `test/run.sh` like `night_policy`. UI behaviour lives in `platform/` or pure policy; `main.c`/`sim/main.c` are glue.
- The only RTC configuration write is clearing the 12/24-hour bit if it is set (24-hour mode is required for the BCD decode); no PMU writes at all.
- Commit messages: imperative sentence ending with a `Co-Authored-By:` trailer.

---

## File map

| File | Responsibility |
| --- | --- |
| `components/torget_power/clock_policy.h/.c` | Pure: RTC trust rule, UTC civil↔epoch, CLOCK text |
| `components/torget_power/pcf85063.h/.c` | Target-only I2C read/write of the RTC time registers |
| `components/torget_power/CMakeLists.txt` | Registers the two new sources |
| `components/app_tokens/app_tokens_config.h` | `TG_TIMEZONE` default |
| `secrets.h.example` | Documents `TG_TIMEZONE`; night comment says the schedule is live |
| `main/main.c` | TZ at boot, RTC boot read, SNTP write-back, CLOCK text, schedule in the brightness target |
| `sim/main.c` | CLOCK text through `tg_clock_text` |
| `test/test_clock_policy.c`, `test/run.sh` | Host tests |
| `docs/superpowers/specs/…design.md` | `TG_TIMEZONE` paragraph; bench note (no night key) |
| `README.md`, `CHANGELOG.md`, `docs/labs/README.md`, `docs/observability.md` | Docs |

---

### Task 1: `clock_policy` — trust rule, UTC conversions, CLOCK text

**Files:**
- Create: `components/torget_power/clock_policy.h`
- Create: `components/torget_power/clock_policy.c`
- Create: `test/test_clock_policy.c`
- Modify: `test/run.sh` (after the `night_policy` block)

**Interfaces:**
- Produces:

```c
typedef struct { int year, month, day, hour, minute, second; } tg_civil; /* proleptic Gregorian, UTC */
bool tg_rtc_reading_trusted(bool os_flag, int year);        /* !os_flag && 2026 <= year <= 2099 */
int64_t tg_civil_to_epoch(const tg_civil *c);               /* seconds since 1970-01-01T00:00Z; -1 on out-of-range fields */
bool tg_epoch_to_civil(int64_t epoch, tg_civil *out);       /* false for epoch < 0 */
int tg_civil_weekday(const tg_civil *c);                    /* 0 = Sunday .. 6 = Saturday, -1 on bad input */
const char *tg_clock_text(bool rtc_applied, bool ntp_synced); /* "RTC + NTP", "RTC ONLY", "NTP ONLY", "NOT SET" */
```

- [ ] **Step 1: Write the failing tests**

`test/test_clock_policy.c`:

```c
#include <stdio.h>
#include <string.h>

#include "../components/torget_power/clock_policy.h"

static int failures;

static void check(const char *what, int condition) {
  if (!condition) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

static void test_trust_rule(void) {
  check("clean 2026 is trusted", tg_rtc_reading_trusted(false, 2026));
  check("clean 2099 is trusted", tg_rtc_reading_trusted(false, 2099));
  check("oscillator stop is not trusted", !tg_rtc_reading_trusted(true, 2026));
  check("2025 is not trusted", !tg_rtc_reading_trusted(false, 2025));
  check("2100 is not trusted", !tg_rtc_reading_trusted(false, 2100));
  check("2000 (fresh chip) is not trusted", !tg_rtc_reading_trusted(false, 2000));
}

static void test_epoch_round_trips(void) {
  tg_civil c = { 2026, 9, 25, 0, 0, 0 };
  check("2026-09-25T00:00Z", tg_civil_to_epoch(&c) == 1790294400LL);
  tg_civil zero = { 1970, 1, 1, 0, 0, 0 };
  check("epoch zero", tg_civil_to_epoch(&zero) == 0);
  tg_civil leap = { 2028, 2, 29, 23, 59, 59 };
  int64_t e = tg_civil_to_epoch(&leap);
  tg_civil back;
  check("leap day converts", e > 0 && tg_epoch_to_civil(e, &back));
  check("leap day round-trips",
        back.year == 2028 && back.month == 2 && back.day == 29 &&
        back.hour == 23 && back.minute == 59 && back.second == 59);
  check("next second is March 1st",
        tg_epoch_to_civil(e + 1, &back) && back.month == 3 && back.day == 1 &&
        back.hour == 0);
  tg_civil y2k = { 2000, 3, 1, 12, 30, 15 };
  check("2000-03-01T12:30:15Z", tg_civil_to_epoch(&y2k) == 951913815LL);
  check("negative epoch refused", !tg_epoch_to_civil(-1, &back));
}

static void test_bad_civil_fields(void) {
  tg_civil bad_month = { 2026, 13, 1, 0, 0, 0 };
  check("month 13 refused", tg_civil_to_epoch(&bad_month) == -1);
  tg_civil bad_day = { 2026, 2, 30, 0, 0, 0 };
  check("Feb 30 refused", tg_civil_to_epoch(&bad_day) == -1);
  tg_civil bad_hour = { 2026, 1, 1, 24, 0, 0 };
  check("hour 24 refused", tg_civil_to_epoch(&bad_hour) == -1);
  tg_civil bad_sec = { 2026, 1, 1, 0, 0, 60 };
  check("second 60 refused", tg_civil_to_epoch(&bad_sec) == -1);
  check("NULL refused", tg_civil_to_epoch(NULL) == -1);
}

static void test_weekday(void) {
  tg_civil fri = { 2026, 9, 25, 10, 0, 0 };
  check("2026-09-25 is a Friday", tg_civil_weekday(&fri) == 5);
  tg_civil thu = { 1970, 1, 1, 0, 0, 0 };
  check("1970-01-01 is a Thursday", tg_civil_weekday(&thu) == 4);
  tg_civil bad = { 2026, 2, 30, 0, 0, 0 };
  check("bad date has no weekday", tg_civil_weekday(&bad) == -1);
}

static void test_clock_text(void) {
  check("both", strcmp(tg_clock_text(true, true), "RTC + NTP") == 0);
  check("rtc only", strcmp(tg_clock_text(true, false), "RTC ONLY") == 0);
  check("ntp only", strcmp(tg_clock_text(false, true), "NTP ONLY") == 0);
  check("neither", strcmp(tg_clock_text(false, false), "NOT SET") == 0);
}

int main(void) {
  test_trust_rule();
  test_epoch_round_trips();
  test_bad_civil_fields();
  test_weekday();
  test_clock_text();
  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("clock policy: ok\n");
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd test && cc -std=c11 -Wall -Wextra -Werror -O1 ../components/torget_power/clock_policy.c test_clock_policy.c -o /tmp/torget-clock-policy-test`
Expected: compile error, `clock_policy.c: No such file or directory`.

- [ ] **Step 3: Write header and implementation**

`components/torget_power/clock_policy.h`:

```c
#ifndef TORGET_POWER_CLOCK_POLICY_H
#define TORGET_POWER_CLOCK_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Klockpolicyn: ren. Avgör om en RTC-avläsning får bli systemtid, räknar
 * civil tid ↔ epok i UTC utan libc (så samma tal gäller på värd och target),
 * och ger ABOUT-radens CLOCK-text. RTC:n lagrar UTC; lokal tid är TZ:s sak.
 *
 * Förtroenderegeln (spec 2026-09-24): oscillatorflaggan ren och året
 * 2026..2099. En färsk krets säger år 2000 och avvisas därmed, precis som
 * en krets vars batteri tagit slut (flaggan satt).
 */

typedef struct {
  int year, month, day, hour, minute, second;
} tg_civil;

bool tg_rtc_reading_trusted(bool os_flag, int year);
int64_t tg_civil_to_epoch(const tg_civil *c);
bool tg_epoch_to_civil(int64_t epoch, tg_civil *out);
int tg_civil_weekday(const tg_civil *c);
const char *tg_clock_text(bool rtc_applied, bool ntp_synced);

#endif
```

`components/torget_power/clock_policy.c`:

```c
#include "clock_policy.h"

#include <stddef.h>

bool tg_rtc_reading_trusted(bool os_flag, int year) {
  return !os_flag && year >= 2026 && year <= 2099;
}

static bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int days_in_month(int y, int m) {
  static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m == 2 && leap(y)) return 29;
  return d[m - 1];
}

static bool civil_valid(const tg_civil *c) {
  if (!c) return false;
  if (c->year < 1970 || c->year > 2199) return false;
  if (c->month < 1 || c->month > 12) return false;
  if (c->day < 1 || c->day > days_in_month(c->year, c->month)) return false;
  if (c->hour < 0 || c->hour > 23) return false;
  if (c->minute < 0 || c->minute > 59) return false;
  if (c->second < 0 || c->second > 59) return false;
  return true;
}

/* Howard Hinnant's days_from_civil: dagar sedan 1970-01-01 för ett datum. */
static int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

int64_t tg_civil_to_epoch(const tg_civil *c) {
  if (!civil_valid(c)) return -1;
  return days_from_civil(c->year, c->month, c->day) * 86400LL +
         c->hour * 3600LL + c->minute * 60LL + c->second;
}

bool tg_epoch_to_civil(int64_t epoch, tg_civil *out) {
  if (epoch < 0 || !out) return false;
  int64_t days = epoch / 86400;
  int64_t rem = epoch % 86400;
  /* civil_from_days, samma källa. */
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  int64_t d = doy - (153 * mp + 2) / 5 + 1;
  int64_t m = mp + (mp < 10 ? 3 : -9);
  out->year = (int)(y + (m <= 2));
  out->month = (int)m;
  out->day = (int)d;
  out->hour = (int)(rem / 3600);
  out->minute = (int)((rem % 3600) / 60);
  out->second = (int)(rem % 60);
  return true;
}

int tg_civil_weekday(const tg_civil *c) {
  if (!civil_valid(c)) return -1;
  int64_t days = days_from_civil(c->year, c->month, c->day);
  return (int)(((days % 7) + 11) % 7); /* 1970-01-01 var en torsdag (4) */
}

const char *tg_clock_text(bool rtc_applied, bool ntp_synced) {
  if (rtc_applied && ntp_synced) return "RTC + NTP";
  if (rtc_applied) return "RTC ONLY";
  if (ntp_synced) return "NTP ONLY";
  return "NOT SET";
}
```

Note on the weekday formula: `days % 7` for 1970-01-01 is 0 and must give 4, so `(days % 7 + 11) % 7` = `(0 + 11) % 7` = 4; negative days are excluded by `civil_valid`.

- [ ] **Step 4: Add the test to `test/run.sh`** after the `night_policy` block:

```sh
cc -std=c11 -Wall -Wextra -Werror -O1 \
  ../components/torget_power/clock_policy.c \
  test_clock_policy.c \
  -o /tmp/torget-clock-policy-test
/tmp/torget-clock-policy-test
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd test && cc -std=c11 -Wall -Wextra -Werror -O1 ../components/torget_power/clock_policy.c test_clock_policy.c -o /tmp/torget-clock-policy-test && /tmp/torget-clock-policy-test`
Expected: `clock policy: ok`. If `2000-03-01T12:30:15Z` fails, recompute: 951868800 is 2000-03-01T00:00Z, plus 45015 s = 951913815.

- [ ] **Step 6: Commit**

```bash
git add components/torget_power/clock_policy.h components/torget_power/clock_policy.c test/test_clock_policy.c test/run.sh
git commit -m "Add the pure clock policy: RTC trust rule, UTC calendar maths and the CLOCK text

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: PCF85063 driver (target only)

**Files:**
- Create: `components/torget_power/pcf85063.h`
- Create: `components/torget_power/pcf85063.c`
- Modify: `components/torget_power/CMakeLists.txt` (add `clock_policy.c` and `pcf85063.c` to SRCS)

**Interfaces:**
- Consumes: `tg_civil`, `tg_civil_weekday` (Task 1); `i2c_master_bus_handle_t`.
- Produces:

```c
esp_err_t tg_pcf85063_init(i2c_master_bus_handle_t bus);          /* adds 0x51; forces 24-hour mode if needed */
esp_err_t tg_pcf85063_read(tg_civil *out, bool *os_flag);          /* UTC civil time + oscillator-stop flag */
esp_err_t tg_pcf85063_write(const tg_civil *utc);                  /* sets time, clears OS */
```

- [ ] **Step 1: Header**

`components/torget_power/pcf85063.h`:

```c
#ifndef TORGET_POWER_PCF85063_H
#define TORGET_POWER_PCF85063_H

#include <stdbool.h>

#include "clock_policy.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * PCF85063ATL på I2C 0x51 (spec/hardware.md). Bara tidsregistren 0x04..0x0A
 * läses och skrivs, i UTC. Enda konfigurationsskrivningen är att nolla
 * 12/24-biten i Control_1 om den råkar vara satt: BCD-avkodningen nedan
 * förutsätter 24-timmarsläge. Larm, timer och avbrott rörs inte (del B
 * har inget schemalagt väckande, spec 2026-09-24).
 */
esp_err_t tg_pcf85063_init(i2c_master_bus_handle_t bus);
esp_err_t tg_pcf85063_read(tg_civil *out, bool *os_flag);
esp_err_t tg_pcf85063_write(const tg_civil *utc);

#endif
```

- [ ] **Step 2: Implementation**

`components/torget_power/pcf85063.c`:

```c
#include "pcf85063.h"

#include "esp_log.h"

static const char *TAG = "pcf85063";

#define ADDR 0x51
#define TIMEOUT_MS 50
#define REG_CONTROL1 0x00 /* bit5 STOP, bit1 12_24 (0 = 24 h) */
#define REG_SECONDS  0x04 /* bit7 OS (oscillatorn har stannat), BCD 0..59 */
/* 0x05 minuter, 0x06 timmar, 0x07 dag, 0x08 veckodag, 0x09 månad, 0x0A år (00..99) */

static i2c_master_dev_handle_t s_dev;

static esp_err_t rd(uint8_t reg, uint8_t *out, size_t n) {
  return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, TIMEOUT_MS);
}

static esp_err_t wr(uint8_t reg, const uint8_t *data, size_t n) {
  uint8_t buf[1 + 8];
  if (n > 8) return ESP_ERR_INVALID_SIZE;
  buf[0] = reg;
  for (size_t i = 0; i < n; i++) buf[1 + i] = data[i];
  return i2c_master_transmit(s_dev, buf, 1 + n, TIMEOUT_MS);
}

static int bcd_to_int(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t int_to_bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t tg_pcf85063_init(i2c_master_bus_handle_t bus) {
  if (!bus) return ESP_ERR_INVALID_ARG;
  i2c_device_config_t cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADDR,
    .scl_speed_hz = 400 * 1000,
  };
  esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
  if (err != ESP_OK) return err;
  uint8_t ctl = 0;
  err = rd(REG_CONTROL1, &ctl, 1);
  if (err != ESP_OK) goto fail;
  if (ctl & 0x02) {
    uint8_t fixed = (uint8_t)(ctl & ~0x02);
    err = wr(REG_CONTROL1, &fixed, 1);
    if (err != ESP_OK) goto fail;
    ESP_LOGI(TAG, "12-timmarsläge nollat, RTC:n räknar nu 24 h");
  }
  ESP_LOGI(TAG, "PCF85063 hittad (Control_1 0x%02x)", ctl);
  return ESP_OK;
fail:
  i2c_master_bus_rm_device(s_dev);
  s_dev = NULL;
  return err;
}

esp_err_t tg_pcf85063_read(tg_civil *out, bool *os_flag) {
  if (!s_dev) return ESP_ERR_INVALID_STATE;
  if (!out || !os_flag) return ESP_ERR_INVALID_ARG;
  uint8_t r[7];
  esp_err_t err = rd(REG_SECONDS, r, sizeof r);
  if (err != ESP_OK) return err;
  *os_flag = (r[0] & 0x80) != 0;
  out->second = bcd_to_int(r[0] & 0x7F);
  out->minute = bcd_to_int(r[1] & 0x7F);
  out->hour = bcd_to_int(r[2] & 0x3F);
  out->day = bcd_to_int(r[3] & 0x3F);
  /* r[4] veckodag, härleds ur datumet i stället */
  out->month = bcd_to_int(r[5] & 0x1F);
  out->year = 2000 + bcd_to_int(r[6]);
  return ESP_OK;
}

esp_err_t tg_pcf85063_write(const tg_civil *utc) {
  if (!s_dev) return ESP_ERR_INVALID_STATE;
  if (!utc || utc->year < 2000 || utc->year > 2099) return ESP_ERR_INVALID_ARG;
  int wd = tg_civil_weekday(utc);
  if (wd < 0) return ESP_ERR_INVALID_ARG;
  uint8_t w[7] = {
    int_to_bcd(utc->second) /* OS-biten (bit7) skrivs 0 = klockan gäller */,
    int_to_bcd(utc->minute),
    int_to_bcd(utc->hour),
    int_to_bcd(utc->day),
    (uint8_t)wd,
    int_to_bcd(utc->month),
    int_to_bcd(utc->year - 2000),
  };
  return wr(REG_SECONDS, w, sizeof w);
}
```

- [ ] **Step 3: CMake**

`components/torget_power/CMakeLists.txt`: change the SRCS line to
`SRCS "battery_policy.c" "night_policy.c" "clock_policy.c" "axp2101.c" "pcf85063.c"`. Requires are unchanged (`REQUIRES esp_driver_i2c`, `PRIV_REQUIRES esp_timer`).

- [ ] **Step 4: Build for the target**

Run: `.superpowers/sdd/<this plan's workspace>/fw-build.sh` (or `. ~/esp/esp-idf/export.sh && idf.py build` outside a guarded session).
Expected: `Project build complete`, no warnings in the new files.

- [ ] **Step 5: Commit**

```bash
git add components/torget_power/pcf85063.h components/torget_power/pcf85063.c components/torget_power/CMakeLists.txt
git commit -m "Add a PCF85063 time driver on the BSP I2C bus

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Timezone, RTC boot time, SNTP write-back, CLOCK text

**Files:**
- Modify: `components/app_tokens/app_tokens_config.h` (add `TG_TIMEZONE`)
- Modify: `secrets.h.example` (document `TG_TIMEZONE`)
- Modify: `main/main.c`: includes; the power section (`#ifndef TORGET_BOARD_241_V2`) gains `clock_start()` and `clock_write_back()`; `time_sync()`; `app_main` (TZ before anything reads local time, `clock_start()` between `power_start()` and `wifi_start()`); the `open_menu` block's CLOCK placeholder.

**Interfaces:**
- Consumes: `tg_pcf85063_init/read/write` (Task 2), `tg_rtc_reading_trusted`, `tg_civil_to_epoch`, `tg_epoch_to_civil`, `tg_clock_text` (Task 1), `bsp_i2c_init()`, `bsp_i2c_get_handle()`, `s_time_synced` (exists), `torget_settings_set_clock`.
- Produces: `static volatile bool s_rtc_applied;` and `static volatile bool s_rtc_write_pending;` read by Task 4; `TG_TIMEZONE`.

- [ ] **Step 1: The timezone default**

Append to `components/app_tokens/app_tokens_config.h` before the final `#endif`:

```c
/* The firmware's local time, as a POSIX TZ string. The RTC and SNTP keep
 * UTC; this only decides what "23:00" means for the night schedule and
 * what the RUNS OUT line prints. Default: Europe/Stockholm rules. */
#ifndef TG_TIMEZONE
#define TG_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#endif
```

`secrets.h.example`, in the night block: add before the three commented night defines

```c
/* Local time for the schedule and the RUNS OUT line, as a POSIX TZ string
 * (the panel keeps UTC internally). Default is Europe/Stockholm:
 *   "CET-1CEST,M3.5.0,M10.5.0/3"
 * Examples: London "GMT0BST,M3.5.0/1,M10.5.0", New York
 * "EST5EDT,M3.2.0,M11.1.0", UTC "UTC0". */
/* #define TG_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3" */
```

and change the sentence "The switch is saved now, but the schedule ships in the next release, so today it has no effect." to "The switch is saved in NVS; the schedule needs a valid clock (RTC or NTP) to apply."

- [ ] **Step 2: main.c — includes and state**

Add includes next to the power ones: `#include "clock_policy.h"`, `#include "pcf85063.h"`, `#include "app_tokens_config.h"`, `#include <sys/time.h>`, `#include <time.h>`.

Inside the existing `#ifndef TORGET_BOARD_241_V2` power section (near `s_batt`):

```c
/* Klockan: RTC:n läses EN gång vid boot innan nätet, och skrivs tillbaka
 * efter varje lyckad SNTP-synk. Skrivningen görs i power-tasken, som redan
 * äger I2C-trafiken efter boot; net_task flaggar bara. */
static volatile bool s_rtc_applied;
static volatile bool s_rtc_write_pending;
static bool s_rtc_present;

static void clock_start(void) {
  if (bsp_i2c_init() != ESP_OK || tg_pcf85063_init(bsp_i2c_get_handle()) != ESP_OK) {
    ESP_LOGW(TAG, "ingen RTC att läsa, tiden väntar på SNTP");
    return;
  }
  s_rtc_present = true;
  tg_civil c;
  bool os = false;
  if (tg_pcf85063_read(&c, &os) != ESP_OK) {
    ESP_LOGW(TAG, "RTC svarar inte, tiden väntar på SNTP");
    return;
  }
  if (!tg_rtc_reading_trusted(os, c.year)) {
    ESP_LOGW(TAG, "RTC opålitlig (OS=%d, år %d), tiden väntar på SNTP", os ? 1 : 0, c.year);
    return;
  }
  time_t now = time(NULL);
  struct tm sys;
  gmtime_r(&now, &sys);
  if (sys.tm_year + 1900 >= 2026) {
    ESP_LOGI(TAG, "systemklockan är redan satt, RTC:n lämnas orörd");
    return;
  }
  int64_t epoch = tg_civil_to_epoch(&c);
  if (epoch < 0) {
    ESP_LOGW(TAG, "RTC gav ett ogiltigt datum, tiden väntar på SNTP");
    return;
  }
  struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
  settimeofday(&tv, NULL);
  s_rtc_applied = true;
  ESP_LOGI(TAG, "tid från RTC: %04d-%02d-%02d %02d:%02d UTC",
           c.year, c.month, c.day, c.hour, c.minute);
}

/* Kallas från power_task när net_task flaggat en lyckad synk. */
static void clock_write_back(void) {
  if (!s_rtc_present) return;
  time_t now = time(NULL);
  tg_civil c;
  if (!tg_epoch_to_civil((int64_t)now, &c)) return;
  esp_err_t err = tg_pcf85063_write(&c);
  if (err == ESP_OK) ESP_LOGI(TAG, "RTC uppdaterad från SNTP");
  else ESP_LOGW(TAG, "RTC kunde inte skrivas: %s", esp_err_to_name(err));
}
```

In `power_task`'s loop, after the badge/ABOUT publish and before `vTaskDelay`:

```c
    if (s_rtc_write_pending) {
      s_rtc_write_pending = false;
      clock_write_back();
    }
```

- [ ] **Step 3: main.c — `time_sync()` and the stale comment**

Replace the comment above `time_sync` ("Kortets RTC är inte batteri-backad, så SNTP är förutsättningen för NET_READY") with: "RTC:n (batteribackad, del B) kan ge tiden vid boot; SNTP är ändå vägen till en klocka som certifikaten litar på, och varje lyckad synk skrivs tillbaka till RTC:n." On the success branch add `s_rtc_write_pending = true;` after `s_time_synced = true;` (inside `#ifndef TORGET_BOARD_241_V2`, or guard the variable so the V2 build compiles — define `s_rtc_write_pending` outside the power section if simpler, and only `clock_write_back` inside it).

- [ ] **Step 4: main.c — `app_main`**

Right after `nvs_flash_init()` succeeds (before any UI or app is created), add:

```c
  /* Lokal tid för nattschemat och RUNS OUT-raden. Klockan hålls i UTC. */
  setenv("TZ", TG_TIMEZONE, 1);
  tzset();
```

After `power_start();` and before `wifi_start();` add `clock_start();` (inside the same `#ifndef TORGET_BOARD_241_V2` as `power_start`, matching how it is guarded there).

- [ ] **Step 5: main.c — CLOCK text**

Replace the placeholder line in the `open_menu` block with:

```c
    torget_settings_set_clock(tg_clock_text(s_rtc_applied, s_time_synced));
```

On the V2 build `s_rtc_applied` must exist and stay false: define it outside the power `#ifndef` (a `static volatile bool` costs nothing) so this line compiles on both boards.

- [ ] **Step 6: Build and gate**

Run: the firmware build helper → `Project build complete`, no warnings. Run `PYTHON_BIN=<venv python> ./test/run.sh --skip-js` → green (main.c is not built on the host; the config header is, and its `#ifndef` default must not break any host compile that includes it).

- [ ] **Step 7: Commit**

```bash
git add components/app_tokens/app_tokens_config.h secrets.h.example main/main.c
git commit -m "Take the boot time from the RTC, keep it fresh from SNTP and name the clock source in ABOUT

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: The night schedule in the brightness target

**Files:**
- Modify: `main/main.c`: includes (`night_policy.h`, `labs_features.h`), the brightness block in `tick_cb`, a new static schedule and log state.

**Interfaces:**
- Consumes: `tg_night_applies(const tg_night_schedule*, bool enabled, bool time_valid, int hour, int minute)`, `tk_labs_active(TK_LABS_NIGHT_DIM)` (app_tokens), `s_time_synced`, `s_rtc_applied` (Task 3), `TG_NIGHT_START_HHMM`/`TG_NIGHT_END_HHMM` (app_tokens_config.h), `BRIGHT_NIGHT`, `BRIGHT_DAY`, `WAKE_HOLD_US`, `s_last_touch_us`, `s_batt_bright_cap`.

- [ ] **Step 1: State**

Near the brightness defines:

```c
/* Nattschemat (del B): en tredje källa till ljusmålet, bredvid
 * inaktivitetsregeln och batteritaket. Lägsta källan vinner. */
static const tg_night_schedule s_night_schedule = { TG_NIGHT_START_HHMM, TG_NIGHT_END_HHMM };
static bool s_night_active;
static bool s_night_logged_once;
```

- [ ] **Step 2: The target**

Replace the target computation in `tick_cb` with:

```c
  int target = ((now - s_last_activity_us) > NIGHT_AFTER_US
                && (now - s_last_touch_us) > WAKE_HOLD_US)
               ? BRIGHT_NIGHT : BRIGHT_DAY;

  /* Nattschemat: utvärderas på hel sekund, aldrig varje tick. Utan giltig
   * klocka (varken RTC eller NTP) gäller det inte alls. */
  static int64_t s_night_checked_us;
  if (now - s_night_checked_us >= 1000000LL) {
    s_night_checked_us = now;
    time_t now_s = time(NULL);
    struct tm lt;
    localtime_r(&now_s, &lt);
    bool time_valid = s_time_synced || s_rtc_applied;
    bool night = tg_night_applies(&s_night_schedule,
                                  tk_labs_active(TK_LABS_NIGHT_DIM),
                                  time_valid, lt.tm_hour, lt.tm_min);
    if (night != s_night_active || !s_night_logged_once) {
      s_night_active = night;
      s_night_logged_once = true;
      ESP_LOGI(TAG, "natt: %s (%02d:%02d, schema %04d–%04d, %s)",
               night ? "dimmar" : "dag", lt.tm_hour, lt.tm_min,
               TG_NIGHT_START_HHMM, TG_NIGHT_END_HHMM,
               time_valid ? "klocka giltig" : "klocka saknas");
    }
  }
  if (s_night_active && (now - s_last_touch_us) > WAKE_HOLD_US && target > BRIGHT_NIGHT)
    target = BRIGHT_NIGHT;

  /* Ljustaket från batteripolicyn: lägsta källan vinner, ingen kan lyfta. */
  int cap = s_batt_bright_cap;
  if (cap < target) target = cap;
```

`tk_labs_active` reads the *active* mask (applied at boot), matching how the other LABS switches behave ("RESTART TO APPLY"). Keep the schedule outside the `#ifndef TORGET_BOARD_241_V2` power section? No: `s_rtc_applied` exists on both boards (Task 3) and the schedule is pure, so this block compiles for both; the spec's board scope is respected because the V2 has no RTC path and `tk_labs_active` is board-neutral. Ruling for the executor: leave the schedule active on both boards — a LABS switch that exists on the V2 menu must do what its row says.

- [ ] **Step 3: Build, gate, commit**

Run the firmware build helper → `Project build complete`; `./test/run.sh --skip-js` → green.

```bash
git add main/main.c
git commit -m "Dim to the night level on the LABS schedule when the clock is valid

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: Simulator parity for the CLOCK text and the spec's bench note

**Files:**
- Modify: `sim/CMakeLists.txt` (add `../components/torget_power/clock_policy.c`)
- Modify: `sim/main.c` (the two ABOUT frames: `torget_settings_set_clock(tg_clock_text(true, true))` for `found`, keep `NULL` for `missing`; include `../components/torget_power/clock_policy.h`)
- Modify: `docs/superpowers/specs/2026-09-24-battery-badge-and-rtc-night-dim-design.md`: in the `### sim/` paragraph replace the deferred-key sentence with "The bench does not model panel brightness, so there is no night-schedule key; the schedule is pinned by the host tests of `night_policy` and by the transition log on the panel." Add a short `TG_TIMEZONE` paragraph under "Night policy": "Local time comes from `TG_TIMEZONE`, a POSIX TZ string defaulting to Europe/Stockholm rules; the RTC and SNTP keep UTC. Setting it also makes `RUNS OUT DDD HH:MM` local on the panel."

- [ ] **Step 1: Make the changes; rebuild the sim; run the drift test**

Run: `cmake -S sim -B sim/build -G Ninja && ninja -C sim/build && <venv python> test/test_docs_frame_drift.py`
Expected: green; the pinned ABOUT frame is byte-identical (the text "RTC + NTP" is unchanged).

- [ ] **Step 2: Commit**

```bash
git add sim/CMakeLists.txt sim/main.c docs/superpowers/specs/2026-09-24-battery-badge-and-rtc-night-dim-design.md
git commit -m "Feed the simulator's CLOCK row from the clock policy and note the bench has no night key

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Docs — README, CHANGELOG, labs README, observability

**Files:**
- Modify: `README.md` (Battery badge section: NIGHT DIM now live, one sentence on `TG_TIMEZONE`, CLOCK row values; the "Evidence, honestly" block gains "RTC time at boot and the schedule are not physically verified")
- Modify: `CHANGELOG.md` (`Unreleased` → `Added`): "**RTC clock and scheduled night dimming.** The 2.16 panel takes its time from the battery-backed PCF85063 at boot when the reading is trustworthy, writes NTP time back after each sync, and shows the clock source in SETTINGS → ABOUT. The LABS row NIGHT DIM now dims the glass to the night level between `TG_NIGHT_START_HHMM` and `TG_NIGHT_END_HHMM` (23:00–07:00 by default) whenever the clock is valid. Local time comes from the new `TG_TIMEZONE` (Europe/Stockholm rules by default), which also makes the RUNS OUT line local instead of UTC." Also amend the earlier battery entry's "the schedule it controls ships in the next step" to "the schedule follows below".
- Modify: `docs/labs/README.md` NIGHT DIM row: drop "so today it has no effect"; say "needs a valid clock (RTC or NTP); local time per `TG_TIMEZONE`".
- Modify: `docs/observability.md`: in the boot-log narrative add `tid från RTC` / `RTC opålitlig` / `ingen RTC att läsa` after the WiFi lines, `RTC uppdaterad från SNTP` after `tid synkad`, and `natt: dimmar (…)` / `natt: dag (…)` as a transition line; one table row: `natt: dimmar/dag (HH:MM, schema …, klocka …)` | fw `torget` | the schedule's decision; one line per change, plus one at boot.

- [ ] **Step 1: Edit, run `./test/run.sh --skip-js` (docs tests read README/labs wording), commit**

```bash
git add README.md CHANGELOG.md docs/labs/README.md docs/observability.md
git commit -m "Document the RTC clock, the timezone and the live night schedule

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Gate, PR, handover

- [ ] Full gate `./test/run.sh` (with JS) and the firmware build both green.
- [ ] Push `rtc-night`, open a PR against `dev` titled "Keep time from the RTC and dim on the night schedule (power design part B)" listing the seven tasks, the `TG_TIMEZONE` decision and the RUNS OUT side effect, and stating that nothing was flashed and no hardware capability is promoted.
- [ ] Handover: after merge the owner OTA-pushes (panel on its charger) and runs physical steps 3 and 4 from the spec: boot with WiFi disabled shows the right time from the RTC (log `tid från RTC`), and night dimming with a temporarily moved window (edit `TG_NIGHT_START_HHMM` in `secrets.h`, rebuild, OTA, watch `natt: dimmar` and the glass). The RTC only sets the clock on a true power-on boot: system time survives `esp_restart()` (OTA, panic, watchdog), and those boots log `klockan behållen över omstarten`. The first power-on boot after this firmware will still log `RTC opålitlig` (a fresh chip says year 2000) until the first SNTP write-back; the real test is a later power-on boot (power fully removed — with the cell fitted, pulling USB is not a reset) with no network, which must log `tid från RTC`. (Amended after the final review: the original "second boot" wording did not exercise the RTC.)

---

## Self-review notes

- Spec coverage: RTC boot read with trust rule and no backwards clock (T1, T3); SNTP write-back once per sync (T3); CLOCK texts (T1, T3, T5); schedule with LABS switch, midnight wrap, invalid clock (existing `night_policy` + T4); touch wake preserved (T4); brightness minimum of three sources (T4); logs as transitions (T3, T4); docs (T6); bench note (T5). Not in the spec but required to make "local time" mean anything: `TG_TIMEZONE` (T3), recorded in the spec (T5) and the CHANGELOG (T6).
- The spec's "the comment claiming the RTC has no backup is replaced" — T3 step 3.
- Type consistency: `tg_civil`, `tg_civil_to_epoch`, `tg_epoch_to_civil`, `tg_civil_weekday`, `tg_rtc_reading_trusted`, `tg_clock_text` (T1) used in T2, T3, T5; `tg_pcf85063_init/read/write` (T2) used in T3; `s_rtc_applied`, `s_rtc_write_pending` (T3) used in T3's power_task hook and T4.
- V2: `s_rtc_applied` is defined for both boards (T3 step 5) so T4's block compiles; the RTC path itself is inside the power `#ifndef`.
