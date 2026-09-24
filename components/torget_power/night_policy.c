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
