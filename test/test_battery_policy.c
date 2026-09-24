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
  check("30 s from the old entry is not enough", v.state == TG_BATT_LOW);
  v = tg_batt_update(&p, &s, SEC(74));
  check("29 s after the restart is still low", v.state == TG_BATT_LOW);
  v = tg_batt_update(&p, &s, SEC(75));
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

static void test_single_low_sample_after_gap_is_not_critical(void) {
  tg_batt_policy p = {0};
  tg_batt_sample s = sample(false, false, false, 4);
  tg_batt_update(&p, &s, SEC(0));
  s.percent = 9;
  tg_batt_update(&p, &s, SEC(1));
  s.percent = 4;
  tg_batt_verdict v = tg_batt_update(&p, &s, SEC(31));
  check("single low sample after gap is not critical", v.state == TG_BATT_LOW && !v.shutdown);
}

static void test_invalid_sample_stops_dwell(void) {
  tg_batt_policy p = {0};
  tg_batt_sample s = sample(false, false, false, 4);
  tg_batt_verdict v = tg_batt_update(&p, &s, SEC(0));
  check("pct 4 is low", v.state == TG_BATT_LOW);
  tg_batt_sample bad = {0};
  v = tg_batt_update(&p, &bad, SEC(1));
  check("one invalid keeps state", v.state == TG_BATT_LOW);
  s = sample(false, false, false, 4);
  v = tg_batt_update(&p, &s, SEC(31));
  check("single low after invalid is not critical", v.state == TG_BATT_LOW && !v.shutdown);
  v = tg_batt_update(&p, &s, SEC(60));
  check("still low at 29 s after restart", v.state == TG_BATT_LOW);
  v = tg_batt_update(&p, &s, SEC(61));
  check("critical 30 s after restart", v.state == TG_BATT_CRITICAL);
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
  test_single_low_sample_after_gap_is_not_critical();
  test_invalid_sample_stops_dwell();
  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("battery policy: ok\n");
  return 0;
}
