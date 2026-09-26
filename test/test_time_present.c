#include <stdio.h>
#include <string.h>

#include "../components/app_time/time_present.h"

static int failures;

static void check(const char *what, int condition) {
  if (!condition) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

#define SEC_US(s) ((int64_t)(s) * 1000000LL)
#define MIN_US(m) ((int64_t)(m) * 60LL * 1000000LL)

static void test_clock(void) {
  tg_pomo p; tg_countdown c; tg_time_view_model m;
  tg_pomo_init(&p); tg_countdown_init(&c);

  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 0, &p, &c, 0);
  check("clock text", strcmp(m.big, "09:05") == 0);
  check("clock caption empty", m.caption && m.caption[0] == '\0');
  check("clock hint empty", m.hint && m.hint[0] == '\0');
  check("clock has no dots", m.dots_done == -1);
  check("clock no presets/reset/done", !m.show_presets && !m.show_reset && !m.done);

  tg_time_present(&m, TG_TIME_MODE_CLOCK, false, 9, 5, 0, &p, &c, 0);
  check("invalid clock shows dashes", strcmp(m.big, TG_TIME_NO_VALUE) == 0);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 24, 0, 0, &p, &c, 0);
  check("hour 24 shows dashes", strcmp(m.big, TG_TIME_NO_VALUE) == 0);
}

static void test_pomodoro(void) {
  tg_pomo p; tg_countdown c; tg_time_view_model m;
  tg_pomo_init(&p); tg_countdown_init(&c);

  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, 0);
  check("idle shows 25:00", strcmp(m.big, "25:00") == 0);
  check("idle caption FOCUS", strcmp(m.caption, "FOCUS") == 0);
  check("idle hint", strcmp(m.hint, "TAP TO START") == 0);
  check("idle 0 dots done", m.dots_done == 0);
  check("idle has no reset", !m.show_reset);

  tg_pomo_tap(&p, 0);
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(1));
  check("running 24:00", strcmp(m.big, "24:00") == 0);
  check("running hint", strcmp(m.hint, "TAP TO PAUSE") == 0);
  check("running shows reset", m.show_reset);
  check("running not muted", !m.muted);

  tg_pomo_tap(&p, MIN_US(1));
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(9));
  check("paused frozen at 24:00", strcmp(m.big, "24:00") == 0);
  check("paused hint", strcmp(m.hint, "PAUSED") == 0);
  check("paused muted", m.muted);

  tg_pomo_tap(&p, MIN_US(10));                 /* resume */
  tg_timer_tick(&p.timer, MIN_US(60));
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(60));
  check("done marker", m.done && m.done_source == TG_TIME_DONE_POMODORO);
  check("done caption is the finished phase", strcmp(m.caption, "FOCUS") == 0);
  check("first focus done counts one dot", m.dots_done == 1);

  tg_pomo_tap(&p, MIN_US(61));                 /* dismiss -> short break idle */
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(61));
  check("short break idle 05:00", strcmp(m.big, "05:00") == 0);
  check("short break caption", strcmp(m.caption, "SHORT BREAK") == 0);
  check("short break keeps one dot", m.dots_done == 1);
  check("not done any more", !m.done);

  p.step = 7;                                   /* long break */
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(62));
  check("long break 15:00", strcmp(m.big, "15:00") == 0);
  check("long break caption", strcmp(m.caption, "LONG BREAK") == 0);
  check("long break four dots", m.dots_done == 4);

  p.step = 6;                                   /* fourth focus finishing */
  tg_pomo_tap(&p, 0);
  tg_timer_tick(&p.timer, MIN_US(30));
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(30));
  check("fourth focus done shows four dots", m.dots_done == 4);
}

static void test_timer_mode(void) {
  tg_pomo p; tg_countdown c; tg_time_view_model m;
  tg_pomo_init(&p); tg_countdown_init(&c);

  tg_time_present(&m, TG_TIME_MODE_TIMER, true, 9, 5, 0, &p, &c, 0);
  check("idle timer shows dashes, not zero", strcmp(m.big, TG_TIME_NO_VALUE) == 0);
  check("idle timer caption", strcmp(m.caption, "TIMER") == 0);
  check("idle timer offers presets", m.show_presets);
  check("idle timer hint", strcmp(m.hint, "CHOOSE MINUTES") == 0);
  check("timer has no dots", m.dots_done == -1);

  tg_countdown_start(&c, 1, 0);
  tg_time_present(&m, TG_TIME_MODE_TIMER, true, 9, 5, 0, &p, &c, MIN_US(1));
  check("running 39:00", strcmp(m.big, "39:00") == 0);
  check("running hides presets", !m.show_presets);
  check("running shows reset", m.show_reset);

  tg_timer_tick(&c.timer, MIN_US(41));
  tg_time_present(&m, TG_TIME_MODE_TIMER, true, 9, 5, 0, &p, &c, MIN_US(41));
  check("timer done marker", m.done && m.done_source == TG_TIME_DONE_TIMER);
  check("timer done caption", strcmp(m.caption, "TIMER") == 0);
}

static void test_independence_and_done_everywhere(void) {
  tg_pomo p; tg_countdown c; tg_time_view_model m;
  tg_pomo_init(&p); tg_countdown_init(&c);

  /* pomodoro running while standing in timer mode: the timer view is the
   * timer's own state, not the pomodoro's */
  tg_pomo_tap(&p, 0);
  tg_time_present(&m, TG_TIME_MODE_TIMER, true, 9, 5, 0, &p, &c, MIN_US(2));
  check("timer view unaffected by running pomodoro", m.show_presets);
  check("no done marker while both run/idle", !m.done);

  /* Review focus 6: a finished pomodoro shows in every mode */
  tg_timer_tick(&p.timer, MIN_US(30));
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 0, &p, &c, MIN_US(30));
  check("done shows over the clock", m.done && m.done_source == TG_TIME_DONE_POMODORO);
  tg_time_present(&m, TG_TIME_MODE_TIMER, true, 9, 5, 0, &p, &c, MIN_US(30));
  check("done shows over the timer view", m.done);

  /* both done: the pomodoro is acknowledged first */
  tg_countdown_start(&c, 0, 0);
  tg_timer_tick(&c.timer, MIN_US(30));
  check("pomodoro first", tg_time_done_source_of(&p, &c) == TG_TIME_DONE_POMODORO);
  tg_pomo_tap(&p, MIN_US(31));
  check("then the timer", tg_time_done_source_of(&p, &c) == TG_TIME_DONE_TIMER);
  tg_countdown_tap(&c, MIN_US(32));
  check("then nothing", tg_time_done_source_of(&p, &c) == TG_TIME_DONE_NONE);
  check("NULL-safe", tg_time_done_source_of(NULL, NULL) == TG_TIME_DONE_NONE);
}

static void test_stable_bytes(void) {
  tg_pomo p; tg_countdown c; tg_time_view_model a, b;
  tg_pomo_init(&p); tg_countdown_init(&c);
  tg_time_present(&a, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, 0);
  tg_time_present(&b, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, 0);
  /* the view compares models with memcmp to skip redundant redraws */
  check("identical inputs give identical bytes", memcmp(&a, &b, sizeof a) == 0);
}

static void test_ring(void) {
  tg_pomo p; tg_countdown c; tg_time_view_model m;
  tg_pomo_init(&p); tg_countdown_init(&c);

  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 30, &p, &c, 0);
  check("minute 5 fills clockwise", m.ring_start == 0 && m.ring_end == 516);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 4, 30, &p, &c, 0);
  check("minute 4 empties clockwise", m.ring_start == 516 && m.ring_end == 1000);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, false, 9, 5, 30, &p, &c, 0);
  check("no clock, no ring", m.ring_end == -1);
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 30, &p, &c, 0);
  check("idle pomodoro has no ring", m.ring_end == -1);

  tg_pomo_tap(&p, 0);
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 30, &p, &c, MIN_US(5));
  check("pomodoro ring is the remaining fraction (20/25)", m.ring_start == 0 && m.ring_end == 800);
  tg_pomo_tap(&p, MIN_US(5));
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 30, &p, &c, MIN_US(9));
  check("paused pomodoro keeps its ring", m.ring_start == 0 && m.ring_end == 800);

  tg_time_present(&m, TG_TIME_MODE_TIMER, true, 9, 5, 30, &p, &c, MIN_US(9));
  check("idle timer has no ring even while the pomodoro runs", m.ring_end == -1);
  tg_countdown_start(&c, 1, 0);
  tg_time_present(&m, TG_TIME_MODE_TIMER, true, 9, 5, 30, &p, &c, MIN_US(10));
  check("timer ring is the remaining fraction (30/40)", m.ring_start == 0 && m.ring_end == 750);

  tg_timer_tick(&c.timer, MIN_US(50));
  tg_time_present(&m, TG_TIME_MODE_TIMER, true, 9, 5, 30, &p, &c, MIN_US(50));
  check("the DONE marker hides the ring", m.done && m.ring_end == -1);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 30, &p, &c, MIN_US(50));
  check("...also over the clock", m.done && m.ring_end == -1);
}

int main(void) {
  test_clock();
  test_ring();
  test_pomodoro();
  test_timer_mode();
  test_independence_and_done_everywhere();
  test_stable_bytes();
  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("time present: ok\n");
  return 0;
}
