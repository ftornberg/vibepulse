/* Bootskärmens avkliv (platform/boot_screen_policy.c). Läxan 2026-11-02: en
 * panel utan nät höll bootskärmen över TID:s klocka i 45 s, fast klockan
 * inte väntar på någon data. */
#include <stdio.h>

#include "../platform/boot_screen_policy.h"

static int failures;

static void check(const char *what, int condition) {
  if (!condition) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

int main(void) {
  const int64_t s = 1000000LL;

  check("VibePulse without data holds the boot screen at 10 s",
        tg_boot_screen_decide(false, 10 * s, true) == TG_BOOT_SCREEN_HOLD);
  check("and still at exactly 45 s",
        tg_boot_screen_decide(false, TG_BOOT_SCREEN_GIVE_UP_US, true) == TG_BOOT_SCREEN_HOLD);
  check("after 45 s it gives up and leaves the honest NO DATA",
        tg_boot_screen_decide(false, 46 * s, true) == TG_BOOT_SCREEN_GIVE_UP);
  check("first data takes it down at once",
        tg_boot_screen_decide(true, 3 * s, true) == TG_BOOT_SCREEN_DATA);
  check("TID on the glass takes it down at once, without network",
        tg_boot_screen_decide(false, 1 * s, false) == TG_BOOT_SCREEN_OFFLINE_APP);
  check("TID never waits for the 45 s ceiling",
        tg_boot_screen_decide(false, 44 * s, false) == TG_BOOT_SCREEN_OFFLINE_APP);
  check("data still names itself when both are true",
        tg_boot_screen_decide(true, 1 * s, false) == TG_BOOT_SCREEN_DATA);

  if (failures == 0) {
    printf("OK: boot screen policy tests pass\n");
    return 0;
  }
  printf("%d tests failed\n", failures);
  return 1;
}
