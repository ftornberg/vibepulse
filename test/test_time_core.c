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
  /* Measured on the owner's 2.16 panel (2026-09-26): buttons up reports
   * rotation 0, buttons left 1, buttons right 3; buttons down is 2. The owner
   * wants the clock with the buttons right (the panel's resting pose), the
   * timer with the buttons up, pomodoro with the buttons left. */
  check("buttons right (3) is the clock", tg_time_mode_for(3, TG_TIME_MODE_MEETING) == TG_TIME_MODE_CLOCK);
  check("buttons up (0) is the meeting", tg_time_mode_for(0, TG_TIME_MODE_CLOCK) == TG_TIME_MODE_MEETING);
  check("buttons left (1) is pomodoro", tg_time_mode_for(1, TG_TIME_MODE_CLOCK) == TG_TIME_MODE_POMODORO);
  check("buttons down (2) keeps the last face", tg_time_mode_for(2, TG_TIME_MODE_POMODORO) == TG_TIME_MODE_POMODORO);
  check("rot clock -> clock",
        tg_time_mode_for(TG_TIME_ROT_CLOCK, TG_TIME_MODE_MEETING) == TG_TIME_MODE_CLOCK);
  check("rot pomodoro -> pomodoro",
        tg_time_mode_for(TG_TIME_ROT_POMODORO, TG_TIME_MODE_CLOCK) == TG_TIME_MODE_POMODORO);
  check("rot timer -> timer",
        tg_time_mode_for(TG_TIME_ROT_MEETING, TG_TIME_MODE_CLOCK) == TG_TIME_MODE_MEETING);
  /* rotation 2 is the button edge down: no mode of its own */
  for (int last = TG_TIME_MODE_CLOCK; last <= TG_TIME_MODE_MEETING; last++)
    check("button edge down keeps last",
          tg_time_mode_for(2, (tg_time_mode)last) == (tg_time_mode)last);
  check("no IMU (-1) keeps last",
        tg_time_mode_for(-1, TG_TIME_MODE_POMODORO) == TG_TIME_MODE_POMODORO);
  check("out of range 4 keeps last",
        tg_time_mode_for(4, TG_TIME_MODE_MEETING) == TG_TIME_MODE_MEETING);
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

static void test_meeting(void) {
  tg_meeting m;
  tg_meeting_init(&m, TG_MEETING_PEOPLE_DEFAULT, TG_MEETING_RATE_DEFAULT);
  check("defaults 6 x 900", m.people == 6 && m.rate == 900 && m.state == TG_MEETING_IDLE);
  check("hourly 5 400", tg_meeting_hourly_kr(&m) == 5400);
  check("idle costs nothing", tg_meeting_cost_kr(&m, MIN_US(99)) == 0);
  check("idle has no ring", tg_meeting_ring(&m, MIN_US(5)) == -1);

  tg_meeting_init(&m, 0, 40);
  check("init clamps people up", m.people == TG_MEETING_PEOPLE_MIN);
  check("init clamps rate up", m.rate == TG_MEETING_RATE_MIN);
  tg_meeting_init(&m, 500, 99999);
  check("init clamps people down", m.people == TG_MEETING_PEOPLE_MAX);
  check("init clamps rate down", m.rate == TG_MEETING_RATE_MAX);
  tg_meeting_init(&m, 6, 949);
  check("rate rounds to the step", m.rate == 900);
  tg_meeting_init(&m, 6, 950);
  check("rate rounds half up", m.rate == 1000);

  tg_meeting_init(&m, 1, 100);
  check("people floor holds", !tg_meeting_step_people(&m, -1) && m.people == 1);
  check("rate floor holds", !tg_meeting_step_rate(&m, -1) && m.rate == 100);
  check("people up", tg_meeting_step_people(&m, +1) && m.people == 2);
  check("rate up one step", tg_meeting_step_rate(&m, +1) && m.rate == 200);
  for (int i = 0; i < 200; i++) tg_meeting_step_people(&m, +1);
  for (int i = 0; i < 200; i++) tg_meeting_step_rate(&m, +1);
  check("people ceiling", m.people == 99);
  check("rate ceiling", m.rate == 5000);
  check("past the ceiling is no change", !tg_meeting_step_people(&m, +1));

  tg_meeting_init(&m, 6, 900);
  tg_meeting_tap(&m, SEC_US(100));
  check("tap starts", m.state == TG_MEETING_RUNNING);
  check("steppers locked while running",
        !tg_meeting_step_people(&m, +1) && !tg_meeting_step_rate(&m, +1) &&
        m.people == 6 && m.rate == 900);
  int64_t t = SEC_US(100) + MIN_US(27) + SEC_US(40);
  check("elapsed 27:40", tg_meeting_elapsed_us(&m, t) == MIN_US(27) + SEC_US(40));
  check("6 x 900 for 27:40 is 2 490 kr", tg_meeting_cost_kr(&m, t) == 2490);
  check("ring 27:40 is 461 permille", tg_meeting_ring(&m, t) == 461);
  check("one microsecond in costs nothing yet",
        tg_meeting_cost_kr(&m, SEC_US(100) + 1) == 0);
  check("cost rounds down",
        tg_meeting_cost_kr(&m, SEC_US(100) + SEC_US(1)) == 1); /* 1.5 kr */
  check("a clock before the start is zero, not negative",
        tg_meeting_elapsed_us(&m, SEC_US(50)) == 0);

  tg_meeting_tap(&m, t);
  check("tap pauses", m.state == TG_MEETING_PAUSED);
  check("paused cost holds", tg_meeting_cost_kr(&m, t + MIN_US(60)) == 2490);
  check("steppers still locked while paused", !tg_meeting_step_people(&m, +1));
  tg_meeting_tap(&m, t + MIN_US(60));
  check("tap resumes", m.state == TG_MEETING_RUNNING);
  check("the paused hour is not billed",
        tg_meeting_elapsed_us(&m, t + MIN_US(60)) == MIN_US(27) + SEC_US(40));
  check("ring wraps each hour",
        tg_meeting_ring(&m, t + MIN_US(60) + MIN_US(32) + SEC_US(20)) == 0);

  tg_meeting_reset(&m);
  check("reset -> idle", m.state == TG_MEETING_IDLE);
  check("reset keeps the setup", m.people == 6 && m.rate == 900);
  check("reset costs nothing", tg_meeting_cost_kr(&m, MIN_US(500)) == 0);

  /* Taket: tio timmar, sedan pausat. */
  tg_meeting_init(&m, 99, 5000);
  tg_meeting_tap(&m, 0);
  tg_meeting_tick(&m, TG_MEETING_MAX_US - 1);
  check("just before the cap still runs", m.state == TG_MEETING_RUNNING);
  tg_meeting_tick(&m, TG_MEETING_MAX_US + MIN_US(5));
  check("the cap pauses", m.state == TG_MEETING_PAUSED);
  check("the cap is exactly ten hours",
        tg_meeting_elapsed_us(&m, TG_MEETING_MAX_US * 3) == TG_MEETING_MAX_US);
  check("largest cost is 4 950 000", tg_meeting_cost_kr(&m, 0) == 4950000);
  tg_meeting_tap(&m, TG_MEETING_MAX_US * 2);
  check("a capped meeting does not resume", m.state == TG_MEETING_PAUSED);
  tg_meeting_init(&m, 2, 100);
  tg_meeting_tap(&m, 0);
  tg_meeting_tap(&m, TG_MEETING_MAX_US + MIN_US(1));
  check("a tap past the cap pauses at the cap",
        m.state == TG_MEETING_PAUSED && m.elapsed_us == TG_MEETING_MAX_US);

  /* independence: the meeting and the pomodoro never touch each other */
  tg_pomo p;
  tg_pomo_init(&p);
  tg_meeting_init(&m, 6, 900);
  tg_meeting_tap(&m, 0);
  tg_pomo_tap(&p, 0);
  tg_meeting_reset(&m);
  check("pomodoro unaffected by meeting reset", p.timer.state == TG_TIMER_RUNNING);
  tg_pomo_reset(&p);
  tg_meeting_tap(&m, 0);
  check("meeting unaffected by pomodoro reset", m.state == TG_MEETING_RUNNING);
}

static void test_meeting_text(void) {
  char b[24];
  tg_meeting_kr_text(0, b, sizeof b);
  check("0", strcmp(b, "0") == 0);
  tg_meeting_kr_text(999, b, sizeof b);
  check("999", strcmp(b, "999") == 0);
  tg_meeting_kr_text(5400, b, sizeof b);
  check("5 400", strcmp(b, "5 400") == 0);
  tg_meeting_kr_text(107917, b, sizeof b);
  check("107 917", strcmp(b, "107 917") == 0);
  tg_meeting_kr_text(4950000, b, sizeof b);
  check("4 950 000", strcmp(b, "4 950 000") == 0);
  tg_meeting_kr_text(-5, b, sizeof b);
  check("negative is 0", strcmp(b, "0") == 0);
  char tiny[4];
  tg_meeting_kr_text(123456, tiny, sizeof tiny);
  check("truncates without overflow", strlen(tiny) <= 3);

  tg_meeting_elapsed_text(0, b, sizeof b);
  check("00:00", strcmp(b, "00:00") == 0);
  tg_meeting_elapsed_text(MIN_US(27) + SEC_US(40) + 999999, b, sizeof b);
  check("27:40 rounds down", strcmp(b, "27:40") == 0);
  tg_meeting_elapsed_text(MIN_US(60), b, sizeof b);
  check("1:00:00", strcmp(b, "1:00:00") == 0);
  tg_meeting_elapsed_text(TG_MEETING_MAX_US, b, sizeof b);
  check("10:00:00", strcmp(b, "10:00:00") == 0);
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

static void test_advance(void) {
  tg_pomo p;
  tg_pomo_init(&p);
  check("nothing running, nothing expires", tg_time_advance(&p, MIN_US(99)) == 0);
  tg_pomo_tap(&p, 0);
  check("before the deadline, nothing", tg_time_advance(&p, MIN_US(24)) == 0);
  check("the pomodoro expires once",
        tg_time_advance(&p, MIN_US(25)) == TG_TIME_EXPIRED_POMODORO);
  check("the step stays on the phase that ended", tg_pomo_phase_of(&p) == TG_POMO_FOCUS);
  check("...and only once", tg_time_advance(&p, MIN_US(26)) == 0);
  check("NULL safe", tg_time_advance(NULL, 0) == 0);
}

/* Vilken signal en utgång ger: pausens slut låter annorlunda. */
static void test_cue_for(void) {
  tg_pomo p;
  tg_pomo_init(&p);
  check("nothing expired, no cue", tg_time_cue_for(0, &p) == -1);
  check("NULL pomodoro still sounds",
        tg_time_cue_for(TG_TIME_EXPIRED_POMODORO, NULL) == TG_AUDIO_CUE_DONE);
  for (int step = 0; step < TG_POMO_STEPS; step++) {
    p.step = step;
    int want = step % 2 == 0 ? TG_AUDIO_CUE_DONE : TG_AUDIO_CUE_BREAK_OVER;
    check("focus ends DONE, every break ends BREAK_OVER",
          tg_time_cue_for(TG_TIME_EXPIRED_POMODORO, &p) == want);
  }
  p.step = 7;
  check("the long break is a break", tg_pomo_phase_of(&p) == TG_POMO_LONG_BREAK);
}

int main(void) {
  test_mode_for();
  test_advance();
  test_cue_for();
  test_tick_expired();
  test_ring();
  test_timer();
  test_pomodoro();
  test_meeting();
  test_meeting_text();
  test_text();
  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("time core: ok\n");
  return 0;
}
