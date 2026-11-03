#ifndef TORGET_BOOT_SCREEN_POLICY_H
#define TORGET_BOOT_SCREEN_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * När kliver bootskärmen (WIFI → TIME → DATA) av? Ren regel utan LVGL och
 * utan klocka, värdtestad i test/test_boot_screen_policy.c; värdlagrets
 * tick_cb frågar en gång per tick tills svaret inte längre är HOLD.
 *
 * Bootskärmen väntar på första datan åt en app som behöver nätet. En app
 * som klarar sig utan (TID) har inget att vänta på: står den på glaset —
 * eller launchern, vägen dit — kliver skärmen av direkt i stället för att
 * täcka klockan i 45 s utan nät (läxan 2026-11-02 i docs/lessons.md).
 */

#define TG_BOOT_SCREEN_GIVE_UP_US (45LL * 1000000LL) /* två hämtcykler + marginal */

typedef enum {
  TG_BOOT_SCREEN_HOLD = 0,      /* stå kvar                                   */
  TG_BOOT_SCREEN_DATA,          /* första datan landade                       */
  TG_BOOT_SCREEN_OFFLINE_APP,   /* glaset visar en app som inte behöver nätet */
  TG_BOOT_SCREEN_GIVE_UP,       /* nätet kom aldrig; bakom står ärlig NO DATA */
} tg_boot_screen_decision;

tg_boot_screen_decision tg_boot_screen_decide(bool data_alive, int64_t now_us,
                                              bool foreground_needs_network);

#endif
