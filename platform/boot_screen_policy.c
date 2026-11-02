#include "boot_screen_policy.h"

tg_boot_screen_decision tg_boot_screen_decide(bool data_alive, int64_t now_us,
                                              bool foreground_needs_network) {
  if (data_alive) return TG_BOOT_SCREEN_DATA;
  if (!foreground_needs_network) return TG_BOOT_SCREEN_OFFLINE_APP;
  if (now_us > TG_BOOT_SCREEN_GIVE_UP_US) return TG_BOOT_SCREEN_GIVE_UP;
  return TG_BOOT_SCREEN_HOLD;
}
