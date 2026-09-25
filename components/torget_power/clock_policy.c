#include "clock_policy.h"

#include <stddef.h>

bool tg_rtc_reading_trusted(const tg_rtc_reading *r) {
  if (!r) return false;
  return !r->os && r->utc_marked && r->civil.year >= 2026 && r->civil.year <= 2099;
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
