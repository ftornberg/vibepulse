#include <stdio.h>
#include <string.h>

#include "../components/app_time/time_core.h"

static int failures;

static void check(const char *what, int condition) {
  if (!condition) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

#define SEC_US(s) ((int64_t)(s) * 1000000LL)
#define MIN_US(m) ((int64_t)(m) * 60LL * 1000000LL)

static void test_mode_for(void) {
  check("rot clock -> clock",
        tg_time_mode_for(TG_TIME_ROT_CLOCK, TG_TIME_MODE_TIMER) == TG_TIME_MODE_CLOCK);
  check("rot pomodoro -> pomodoro",
        tg_time_mode_for(TG_TIME_ROT_POMODORO, TG_TIME_MODE_CLOCK) == TG_TIME_MODE_POMODORO);
  check("rot timer -> timer",
        tg_time_mode_for(TG_TIME_ROT_TIMER, TG_TIME_MODE_CLOCK) == TG_TIME_MODE_TIMER);
  /* rotation 2 is the button edge down: no mode of its own */
  for (int last = TG_TIME_MODE_CLOCK; last <= TG_TIME_MODE_TIMER; last++)
    check("button edge down keeps last",
          tg_time_mode_for(2, (tg_time_mode)last) == (tg_time_mode)last);
  check("no IMU (-1) keeps last",
        tg_time_mode_for(-1, TG_TIME_MODE_POMODORO) == TG_TIME_MODE_POMODORO);
  check("out of range 4 keeps last",
        tg_time_mode_for(4, TG_TIME_MODE_TIMER) == TG_TIME_MODE_TIMER);
  check("out of range 99 keeps last",
        tg_time_mode_for(99, TG_TIME_MODE_CLOCK) == TG_TIME_MODE_CLOCK);
}

static void test_timer(void) {
  tg_timer t;
  tg_timer_init(&t);
  check("init idle", t.state == TG_TIMER_IDLE);
  check("idle remaining is 0", tg_timer_remaining_us(&t, SEC_US(5)) == 0);
  check("start rejects zero", !tg_timer_start(&t, 0, 0));
  check("start rejects negative", !tg_timer_start(&t, 0, -1));
  check("still idle after rejects", t.state == TG_TIMER_IDLE);

  check("start ok", tg_timer_start(&t, SEC_US(100), MIN_US(1)));
  check("running", t.state == TG_TIMER_RUNNING);
  check("start while running rejected", !tg_timer_start(&t, SEC_US(100), MIN_US(1)));
  check("remaining full at start", tg_timer_remaining_us(&t, SEC_US(100)) == MIN_US(1));
  check("remaining after 10 s", tg_timer_remaining_us(&t, SEC_US(110)) == SEC_US(50));

  tg_timer_tick(&t, SEC_US(100) + MIN_US(1) - 1);
  check("one microsecond early still running", t.state == TG_TIMER_RUNNING);
  tg_timer_tick(&t, SEC_US(100) + MIN_US(1));
  check("at deadline done", t.state == TG_TIMER_DONE);
  check("done remaining 0", tg_timer_remaining_us(&t, SEC_US(9999)) == 0);
  check("cannot start from done", !tg_timer_start(&t, SEC_US(200), MIN_US(1)));
  tg_timer_cancel(&t);
  check("cancel -> idle", t.state == TG_TIMER_IDLE);

  /* pause/resume keeps the remaining time exactly */
  tg_timer_start(&t, 0, MIN_US(10));
  tg_timer_toggle(&t, MIN_US(4));
  check("paused", t.state == TG_TIMER_PAUSED);
  check("paused remaining 6 min", tg_timer_remaining_us(&t, MIN_US(4)) == MIN_US(6));
  check("paused remaining frozen", tg_timer_remaining_us(&t, MIN_US(99)) == MIN_US(6));
  tg_timer_toggle(&t, MIN_US(50));
  check("resumed", t.state == TG_TIMER_RUNNING);
  check("resumed remaining 6 min", tg_timer_remaining_us(&t, MIN_US(50)) == MIN_US(6));
  tg_timer_tick(&t, MIN_US(56));
  check("resumed timer finishes 6 min later", t.state == TG_TIMER_DONE);

  /* Review focus 3: pause after the deadline but before any tick */
  tg_timer_cancel(&t);
  tg_timer_start(&t, 0, MIN_US(1));
  tg_timer_toggle(&t, MIN_US(5));
  check("pause after deadline becomes done, not paused", t.state == TG_TIMER_DONE);
  check("never negative remaining", tg_timer_remaining_us(&t, MIN_US(5)) == 0);

  /* Review focus 5: a long gap with no ticks, then one tick */
  tg_timer_cancel(&t);
  tg_timer_start(&t, 0, MIN_US(25));
  tg_timer_tick(&t, MIN_US(60 * 24 * 3));
  check("three days later, one tick, done", t.state == TG_TIMER_DONE);

  /* toggle on idle or done does nothing */
  tg_timer_cancel(&t);
  tg_timer_toggle(&t, 0);
  check("toggle idle stays idle", t.state == TG_TIMER_IDLE);
}

static void test_pomodoro(void) {
  static const int expect_min[TG_POMO_STEPS] = {25, 5, 25, 5, 25, 5, 25, 15};
  static const tg_pomo_phase expect_phase[TG_POMO_STEPS] = {
    TG_POMO_FOCUS, TG_POMO_SHORT_BREAK, TG_POMO_FOCUS, TG_POMO_SHORT_BREAK,
    TG_POMO_FOCUS, TG_POMO_SHORT_BREAK, TG_POMO_FOCUS, TG_POMO_LONG_BREAK,
  };
  tg_pomo p;
  tg_pomo_init(&p);
  int64_t now = 0;

  for (int round = 0; round < 2; round++) {          /* the cycle wraps */
    for (int step = 0; step < TG_POMO_STEPS; step++) {
      check("step index", p.step == step);
      check("phase", tg_pomo_phase_of(&p) == expect_phase[step]);
      check("phase length", tg_pomo_phase_us(&p) == MIN_US(expect_min[step]));
      check("waits idle for a tap", p.timer.state == TG_TIMER_IDLE);

      tg_pomo_tap(&p, now);                            /* start */
      check("tap starts", p.timer.state == TG_TIMER_RUNNING);
      tg_pomo_tap(&p, now + SEC_US(30));               /* pause */
      check("tap pauses", p.timer.state == TG_TIMER_PAUSED);
      tg_pomo_tap(&p, now + SEC_US(60));               /* resume */
      check("tap resumes", p.timer.state == TG_TIMER_RUNNING);

      now += MIN_US(expect_min[step]) + MIN_US(2);     /* far past the end */
      tg_timer_tick(&p.timer, now);
      check("done at the end", p.timer.state == TG_TIMER_DONE);
      tg_pomo_tap(&p, now);                            /* dismiss */
      check("dismiss does not auto-start the next phase",
            p.timer.state == TG_TIMER_IDLE);
    }
  }
  check("wrapped back to step 0", p.step == 0);

  tg_pomo_tap(&p, now);
  tg_pomo_reset(&p);
  check("reset -> idle", p.timer.state == TG_TIMER_IDLE);
  check("reset -> step 0", p.step == 0);

  tg_pomo_init(&p);
  tg_pomo_tap(&p, 0);
  tg_pomo_tap(&p, MIN_US(30));   /* tap on a run whose deadline already passed */
  check("tap after deadline reveals done", p.timer.state == TG_TIMER_DONE);
  check("...and does not dismiss it unseen", p.step == 0);
  tg_pomo_tap(&p, MIN_US(31));
  check("the next tap dismisses", p.timer.state == TG_TIMER_IDLE && p.step == 1);
}

static void test_countdown(void) {
  tg_countdown c;
  tg_countdown_init(&c);
  check("preset 0 is 20", tg_countdown_preset_minutes(0) == 20);
  check("preset 1 is 40", tg_countdown_preset_minutes(1) == 40);
  check("preset 2 is 50", tg_countdown_preset_minutes(2) == 50);
  check("preset -1 is 0", tg_countdown_preset_minutes(-1) == 0);
  check("preset 3 is 0", tg_countdown_preset_minutes(3) == 0);
  check("bad preset rejected", !tg_countdown_start(&c, 3, 0));
  check("negative preset rejected", !tg_countdown_start(&c, -1, 0));
  check("still idle", c.timer.state == TG_TIMER_IDLE);

  check("start 40", tg_countdown_start(&c, 1, SEC_US(10)));
  check("40 minutes remain", tg_timer_remaining_us(&c.timer, SEC_US(10)) == MIN_US(40));
  check("second start while running rejected", !tg_countdown_start(&c, 0, SEC_US(11)));
  tg_countdown_tap(&c, SEC_US(20));
  check("tap pauses", c.timer.state == TG_TIMER_PAUSED);
  tg_countdown_tap(&c, SEC_US(30));
  check("tap resumes", c.timer.state == TG_TIMER_RUNNING);
  tg_timer_tick(&c.timer, MIN_US(90));
  check("done", c.timer.state == TG_TIMER_DONE);
  tg_countdown_tap(&c, MIN_US(91));
  check("tap on done -> idle", c.timer.state == TG_TIMER_IDLE);
  tg_countdown_start(&c, 0, MIN_US(100));
  tg_countdown_tap(&c, MIN_US(200));            /* deadline passed, no tick yet */
  check("countdown tap after deadline reveals done", c.timer.state == TG_TIMER_DONE);
  tg_countdown_tap(&c, MIN_US(201));
  check("countdown next tap dismisses", c.timer.state == TG_TIMER_IDLE);
  check("start again works", tg_countdown_start(&c, 2, MIN_US(92)));
  tg_countdown_reset(&c);
  check("reset -> idle", c.timer.state == TG_TIMER_IDLE);

  /* independence: touching one never moves the other */
  tg_pomo p;
  tg_pomo_init(&p);
  tg_countdown_start(&c, 0, 0);
  tg_pomo_tap(&p, 0);
  tg_countdown_reset(&c);
  check("pomodoro unaffected by countdown reset", p.timer.state == TG_TIMER_RUNNING);
  tg_pomo_reset(&p);
  tg_countdown_start(&c, 0, 0);
  check("countdown unaffected by pomodoro reset", c.timer.state == TG_TIMER_RUNNING);
}

static void test_text(void) {
  char buf[16];

  check("epoch just before 2026 invalid", !tg_time_clock_valid(1767225599LL));
  check("epoch 2026-01-01 valid", tg_time_clock_valid(1767225600LL));
  check("epoch 0 invalid", !tg_time_clock_valid(0));
  check("negative epoch invalid", !tg_time_clock_valid(-5));

  tg_time_clock_text(true, 9, 5, buf, sizeof buf);
  check("clock 09:05", strcmp(buf, "09:05") == 0);
  tg_time_clock_text(true, 23, 59, buf, sizeof buf);
  check("clock 23:59", strcmp(buf, "23:59") == 0);
  tg_time_clock_text(false, 9, 5, buf, sizeof buf);
  check("invalid clock shows dashes", strcmp(buf, TG_TIME_NO_VALUE) == 0);
  tg_time_clock_text(true, 24, 0, buf, sizeof buf);
  check("hour 24 shows dashes", strcmp(buf, TG_TIME_NO_VALUE) == 0);
  tg_time_clock_text(true, 12, 60, buf, sizeof buf);
  check("minute 60 shows dashes", strcmp(buf, TG_TIME_NO_VALUE) == 0);
  tg_time_clock_text(true, -1, 0, buf, sizeof buf);
  check("negative hour shows dashes", strcmp(buf, TG_TIME_NO_VALUE) == 0);

  char tiny[3] = {'x', 'x', 'x'};
  tg_time_clock_text(true, 9, 5, tiny, sizeof tiny);
  check("small buffer stays terminated", tiny[2] == '\0');
  char untouched[3] = {'x', 'x', 'x'};
  tg_time_clock_text(true, 9, 5, untouched, 0);
  check("zero capacity writes nothing", untouched[0] == 'x');

  tg_time_mmss_text(0, buf, sizeof buf);
  check("0 -> 00:00", strcmp(buf, "00:00") == 0);
  tg_time_mmss_text(1, buf, sizeof buf);
  check("1 us rounds up to 00:01", strcmp(buf, "00:01") == 0);
  tg_time_mmss_text(SEC_US(1), buf, sizeof buf);
  check("exactly 1 s -> 00:01", strcmp(buf, "00:01") == 0);
  tg_time_mmss_text(SEC_US(1) + 1, buf, sizeof buf);
  check("1 s + 1 us -> 00:02", strcmp(buf, "00:02") == 0);
  tg_time_mmss_text(MIN_US(25), buf, sizeof buf);
  check("25 min -> 25:00", strcmp(buf, "25:00") == 0);
  tg_time_mmss_text(MIN_US(50), buf, sizeof buf);
  check("50 min -> 50:00", strcmp(buf, "50:00") == 0);
  tg_time_mmss_text(-SEC_US(3), buf, sizeof buf);
  check("negative -> 00:00", strcmp(buf, "00:00") == 0);
  tg_time_mmss_text(MIN_US(100000), buf, sizeof buf);
  check("huge clamps to 99:59", strcmp(buf, "99:59") == 0);
}

static void test_ring(void) {
  /* Always clockwise (owner, 2026-09-26): odd minutes the filled part grows
   * clockwise from 12, even minutes the empty part does, so the ring never
   * jumps from full to empty nor runs backwards. Permille of the circle. */
  tg_ring_arc a;
  a = tg_ring_seconds(true, 11, 0);
  check("odd minute starts as a sliver at 12", a.start == 0 && a.end == 16);
  a = tg_ring_seconds(true, 11, 29);
  check("odd minute :29 is the first half", a.start == 0 && a.end == 500);
  a = tg_ring_seconds(true, 11, 59);
  check("odd minute ends full", a.start == 0 && a.end == 1000);
  a = tg_ring_seconds(true, 12, 0);
  check("even minute starts with a sliver emptied", a.start == 16 && a.end == 1000);
  a = tg_ring_seconds(true, 12, 29);
  check("even minute :29 keeps the second half", a.start == 500 && a.end == 1000);
  a = tg_ring_seconds(true, 12, 59);
  check("even minute ends empty", a.start == 1000 && a.end == 1000);

  /* One edge moves exactly one sixtieth clockwise every second, across every
   * second and every minute boundary of the hour. */
  tg_ring_arc prev = tg_ring_seconds(true, 59, 59);
  for (int minute = 0; minute < 60; minute++)
    for (int second = 0; second < 60; second++) {
      a = tg_ring_seconds(true, minute, second);
      int ds = a.start - prev.start, de = a.end - prev.end;
      bool wrap = prev.start == prev.end || prev.end - prev.start == 1000;
      bool one_edge_forward =
          (ds == 0 && (de == 16 || de == 17)) || (de == 0 && (ds == 16 || ds == 17));
      check("one edge moves one step clockwise each second",
            one_edge_forward || (wrap && a.end - a.start <= 17));
      check("start never passes end", a.start <= a.end);
      prev = a;
    }

  a = tg_ring_seconds(false, 11, 10);
  check("seconds ring hidden without a valid clock", a.end == -1);
  check("negative second hides the ring", tg_ring_seconds(true, 10, -1).end == -1);
  check("second 60 hides the ring", tg_ring_seconds(true, 10, 60).end == -1);
  check("minute 60 hides the ring", tg_ring_seconds(true, 60, 10).end == -1);
  check("negative minute hides the ring", tg_ring_seconds(true, -1, 10).end == -1);

  tg_timer t;
  tg_timer_init(&t);
  check("idle timer has no ring", tg_ring_remaining(&t, 0) == -1);
  tg_timer_start(&t, 0, MIN_US(1));
  check("full ring at the start", tg_ring_remaining(&t, 0) == 1000);
  check("half ring at 30 s", tg_ring_remaining(&t, SEC_US(30)) == 500);
  check("rounded up, never 0 while running",
        tg_ring_remaining(&t, MIN_US(1) - 1) == 1);
  tg_timer_toggle(&t, SEC_US(15));
  check("paused ring keeps its fraction", tg_ring_remaining(&t, SEC_US(999)) == 750);
  tg_timer_toggle(&t, SEC_US(999));
  tg_timer_tick(&t, SEC_US(999) + MIN_US(5));
  check("done timer has no ring", tg_ring_remaining(&t, SEC_US(999) + MIN_US(5)) == -1);
}

static void test_tick_expired(void) {
  tg_timer t;
  tg_timer_init(&t);
  check("idle never expires", !tg_timer_tick_expired(&t, MIN_US(99)));
  tg_timer_start(&t, 0, MIN_US(1));
  check("running before the deadline does not expire", !tg_timer_tick_expired(&t, MIN_US(1) - 1));
  check("still running", t.state == TG_TIMER_RUNNING);
  check("reports the RUNNING -> DONE transition", tg_timer_tick_expired(&t, MIN_US(1)));
  check("...and moved to done", t.state == TG_TIMER_DONE);
  check("only once: already done is not a new expiry", !tg_timer_tick_expired(&t, MIN_US(2)));
  tg_timer_cancel(&t);
  tg_timer_start(&t, 0, MIN_US(1));
  tg_timer_toggle(&t, SEC_US(10));
  check("paused never expires", !tg_timer_tick_expired(&t, MIN_US(99)));
  check("paused stays paused", t.state == TG_TIMER_PAUSED);
}

int main(void) {
  test_mode_for();
  test_tick_expired();
  test_ring();
  test_timer();
  test_pomodoro();
  test_countdown();
  test_text();
  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("time core: ok\n");
  return 0;
}
