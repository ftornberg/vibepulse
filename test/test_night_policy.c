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
