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

static tg_rtc_reading reading(int year, bool os, bool utc_marked) {
  tg_rtc_reading r = { { year, 9, 25, 10, 0, 0 }, os, utc_marked };
  return r;
}

static void test_trust_rule(void) {
  tg_rtc_reading r = reading(2026, false, true);
  check("clean, marked 2026 is trusted", tg_rtc_reading_trusted(&r));
  r = reading(2099, false, true);
  check("clean, marked 2099 is trusted", tg_rtc_reading_trusted(&r));
  r = reading(2026, true, true);
  check("oscillator stop is not trusted", !tg_rtc_reading_trusted(&r));
  r = reading(2026, false, false);
  check("unmarked (vendor demo, maybe local time) is not trusted", !tg_rtc_reading_trusted(&r));
  r = reading(2025, false, true);
  check("2025 is not trusted", !tg_rtc_reading_trusted(&r));
  r = reading(2100, false, true);
  check("2100 is not trusted", !tg_rtc_reading_trusted(&r));
  r = reading(2000, false, false);
  check("2000 (fresh chip) is not trusted", !tg_rtc_reading_trusted(&r));
  check("NULL is not trusted", !tg_rtc_reading_trusted(NULL));
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
