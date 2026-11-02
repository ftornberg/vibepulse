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
  tg_pomo p; tg_meeting c; tg_time_view_model m;
  tg_pomo_init(&p); tg_meeting_init(&c, 6, 900);

  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 0, &p, &c, 0, false, true);
  check("clock text", strcmp(m.big, "09:05") == 0);
  check("clock caption empty", m.caption && m.caption[0] == '\0');
  check("clock hint empty", m.hint[0] == 0);
  check("clock has no dots", m.dots_done == -1);
  check("clock no steppers/reset/done", !m.show_steppers && !m.show_reset && !m.done);

  tg_time_present(&m, TG_TIME_MODE_CLOCK, false, 9, 5, 0, &p, &c, 0, false, true);
  check("invalid clock shows dashes", strcmp(m.big, TG_TIME_NO_VALUE) == 0);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 24, 0, 0, &p, &c, 0, false, true);
  check("hour 24 shows dashes", strcmp(m.big, TG_TIME_NO_VALUE) == 0);
}

static void test_pomodoro(void) {
  tg_pomo p; tg_meeting c; tg_time_view_model m;
  tg_pomo_init(&p); tg_meeting_init(&c, 6, 900);

  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, 0, false, true);
  check("idle shows 25:00", strcmp(m.big, "25:00") == 0);
  check("idle caption FOCUS", strcmp(m.caption, "FOCUS") == 0);
  check("idle hint", strcmp(m.hint, "TAP TO START") == 0);
  check("idle 0 dots done", m.dots_done == 0);
  check("idle has no reset", !m.show_reset);

  tg_pomo_tap(&p, 0);
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(1), false, true);
  check("running 24:00", strcmp(m.big, "24:00") == 0);
  check("running hint", strcmp(m.hint, "TAP TO PAUSE") == 0);
  check("running shows reset", m.show_reset);
  check("running not muted", !m.muted);

  tg_pomo_tap(&p, MIN_US(1));
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(9), false, true);
  check("paused frozen at 24:00", strcmp(m.big, "24:00") == 0);
  check("paused hint", strcmp(m.hint, "PAUSED") == 0);
  check("paused muted", m.muted);

  tg_pomo_tap(&p, MIN_US(10));                 /* resume */
  tg_timer_tick(&p.timer, MIN_US(60));
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(60), false, true);
  check("done marker", m.done && m.done_source == TG_TIME_DONE_POMODORO);
  check("done caption is the finished phase", strcmp(m.caption, "FOCUS") == 0);
  check("first focus done counts one dot", m.dots_done == 1);

  tg_pomo_tap(&p, MIN_US(61));                 /* dismiss -> short break idle */
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(61), false, true);
  check("short break idle 05:00", strcmp(m.big, "05:00") == 0);
  check("short break caption", strcmp(m.caption, "SHORT BREAK") == 0);
  check("short break keeps one dot", m.dots_done == 1);
  check("not done any more", !m.done);

  p.step = 7;                                   /* long break */
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(62), false, true);
  check("long break 15:00", strcmp(m.big, "15:00") == 0);
  check("long break caption", strcmp(m.caption, "LONG BREAK") == 0);
  check("long break four dots", m.dots_done == 4);

  p.step = 6;                                   /* fourth focus finishing */
  tg_pomo_tap(&p, 0);
  tg_timer_tick(&p.timer, MIN_US(30));
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, MIN_US(30), false, true);
  check("fourth focus done shows four dots", m.dots_done == 4);
}

static void test_meeting_mode(void) {
  tg_pomo p; tg_meeting c; tg_time_view_model m;
  tg_pomo_init(&p); tg_meeting_init(&c, 6, 900);

  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 0, &p, &c, 0, false, true);
  check("idle meeting shows the hourly cost", strcmp(m.big, "5 400") == 0);
  check("idle caption", strcmp(m.caption, "MEETING") == 0);
  check("idle offers steppers", m.show_steppers);
  check("idle stepper values", strcmp(m.people, "6") == 0 && strcmp(m.rate, "900") == 0);
  check("idle hint", strcmp(m.hint, "KR PER HOUR  \xC2\xB7  TAP TO START") == 0);
  check("meeting has no dots", m.dots_done == -1);
  check("idle has no ring and no reset", m.ring_end == -1 && !m.show_reset);
  check("five digits keep the big font", !m.big_compact);

  tg_meeting_tap(&c, 0);
  int64_t t = MIN_US(27) + SEC_US(40);
  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 0, &p, &c, t, false, true);
  check("running shows the cost so far", strcmp(m.big, "2 490") == 0);
  check("running hint", strcmp(m.hint, "KR  \xC2\xB7  27:40  \xC2\xB7  6 PEOPLE") == 0);
  check("running hides steppers", !m.show_steppers);
  check("running shows reset", m.show_reset && !m.muted);
  check("running ring is the share of the hour", m.ring_start == 0 && m.ring_end == 461);

  tg_meeting_tap(&c, t);
  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 0, &p, &c, t + MIN_US(5), false, true);
  check("paused holds the cost", strcmp(m.big, "2 490") == 0);
  check("paused hint", strcmp(m.hint, "PAUSED  \xC2\xB7  27:40") == 0);
  check("paused is muted with reset", m.muted && m.show_reset);
  check("paused keeps its ring", m.ring_end == 461);

  tg_meeting_init(&c, 1, 100);
  tg_meeting_tap(&c, 0);
  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 0, &p, &c, MIN_US(90), false, true);
  check("one person is singular", strcmp(m.hint, "KR  \xC2\xB7  1:30:00  \xC2\xB7  1 PERSON") == 0);

  tg_meeting_init(&c, 99, 5000);
  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 0, &p, &c, 0, false, true);
  check("widest setup", strcmp(m.big, "495 000") == 0 && m.big_compact);
  check("widest stepper values", strcmp(m.people, "99") == 0 && strcmp(m.rate, "5000") == 0);
  tg_meeting_tap(&c, 0);
  tg_meeting_tick(&c, TG_MEETING_MAX_US * 2);
  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 0, &p, &c, TG_MEETING_MAX_US * 2, false, true);
  check("largest sum fits the buffer", strcmp(m.big, "4 950 000") == 0 && m.big_compact);
  check("the cap reads as paused", strcmp(m.hint, "PAUSED  \xC2\xB7  10:00:00") == 0);
}

static void test_compact(void) {
  check("5 digits large", !tg_time_big_is_compact("99 999"));
  check("6 digits compact", tg_time_big_is_compact("100 000"));
  check("clock never compact", !tg_time_big_is_compact("10:09"));
  check("dashes never compact", !tg_time_big_is_compact(TG_TIME_NO_VALUE));
  check("NULL safe", !tg_time_big_is_compact(NULL));
}

static void test_independence_and_done_everywhere(void) {
  tg_pomo p; tg_meeting c; tg_time_view_model m;
  tg_pomo_init(&p); tg_meeting_init(&c, 6, 900);

  /* pomodoro running while standing in meeting mode: the meeting view is the
   * meeting's own state, not the pomodoro's */
  tg_pomo_tap(&p, 0);
  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 0, &p, &c, MIN_US(2), false, true);
  check("meeting view unaffected by running pomodoro", m.show_steppers);
  check("no done marker while running", !m.done);

  /* Review focus 6: a finished pomodoro shows in every mode */
  tg_timer_tick(&p.timer, MIN_US(30));
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 0, &p, &c, MIN_US(30), false, true);
  check("done shows over the clock", m.done && m.done_source == TG_TIME_DONE_POMODORO);
  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 0, &p, &c, MIN_US(30), false, true);
  check("done shows over the meeting view", m.done);
  check("done caption is the pomodoro phase", strcmp(m.caption, "FOCUS") == 0);
  tg_pomo_tap(&p, MIN_US(31));
  check("then nothing", tg_time_done_source_of(&p) == TG_TIME_DONE_NONE);
  check("NULL-safe", tg_time_done_source_of(NULL) == TG_TIME_DONE_NONE);
}

static void test_stable_bytes(void) {
  tg_pomo p; tg_meeting c; tg_time_view_model a, b;
  tg_pomo_init(&p); tg_meeting_init(&c, 6, 900);
  tg_time_present(&a, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, 0, false, true);
  tg_time_present(&b, TG_TIME_MODE_POMODORO, true, 9, 5, 0, &p, &c, 0, false, true);
  /* the view compares models with memcmp to skip redundant redraws */
  check("identical inputs give identical bytes", memcmp(&a, &b, sizeof a) == 0);
}

static void test_ring(void) {
  tg_pomo p; tg_meeting c; tg_time_view_model m;
  tg_pomo_init(&p); tg_meeting_init(&c, 6, 900);

  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 30, &p, &c, 0, false, true);
  check("minute 5 fills clockwise", m.ring_start == 0 && m.ring_end == 516);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 4, 30, &p, &c, 0, false, true);
  check("minute 4 empties clockwise", m.ring_start == 516 && m.ring_end == 1000);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, false, 9, 5, 30, &p, &c, 0, false, true);
  check("no clock, no ring", m.ring_end == -1);
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 30, &p, &c, 0, false, true);
  check("idle pomodoro has no ring", m.ring_end == -1);

  tg_pomo_tap(&p, 0);
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 30, &p, &c, MIN_US(5), false, true);
  check("pomodoro ring is the remaining fraction (20/25)", m.ring_start == 0 && m.ring_end == 800);
  tg_pomo_tap(&p, MIN_US(5));
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 30, &p, &c, MIN_US(9), false, true);
  check("paused pomodoro keeps its ring", m.ring_start == 0 && m.ring_end == 800);

  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 30, &p, &c, MIN_US(9), false, true);
  check("idle meeting has no ring even while the pomodoro runs", m.ring_end == -1);
  tg_pomo_tap(&p, MIN_US(9));              /* resume; ends at 29 min */
  tg_timer_tick(&p.timer, MIN_US(50));
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 30, &p, &c, MIN_US(50), false, true);
  check("the DONE marker hides the ring", m.done && m.ring_end == -1);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 30, &p, &c, MIN_US(50), false, true);
  check("...also over the clock", m.done && m.ring_end == -1);
}

static void test_attention(void) {
  tg_pomo p; tg_meeting c; tg_time_view_model m;
  tg_pomo_init(&p); tg_meeting_init(&c, 6, 900);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 30, &p, &c, 0, true, true);
  check("clock shows the attention icon", m.show_attention);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 30, &p, &c, 0, false, true);
  check("no attention, no icon", !m.show_attention);
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 30, &p, &c, 0, true, true);
  check("pomodoro face has its caption there, no icon", !m.show_attention);
  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 30, &p, &c, 0, true, true);
  check("meeting face has no icon", !m.show_attention);
  tg_pomo_tap(&p, 0);
  tg_timer_tick(&p.timer, MIN_US(30));
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 30, &p, &c, MIN_US(30), true, true);
  check("the DONE marker covers the icon", m.done && !m.show_attention);
}

static void test_speaker(void) {
  tg_pomo p; tg_meeting c; tg_time_view_model m;
  tg_pomo_init(&p); tg_meeting_init(&c, 6, 900);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 30, &p, &c, 0, false, true);
  check("clock shows the speaker", m.show_speaker && m.speaker_on);
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 30, &p, &c, 0, false, false);
  check("off is shown crossed", m.show_speaker && !m.speaker_on);
  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 30, &p, &c, 0, false, true);
  check("the meeting never sounds, so no speaker there", !m.show_speaker);
  tg_meeting_tap(&c, 0);
  tg_time_present(&m, TG_TIME_MODE_MEETING, true, 9, 5, 30, &p, &c, 1, false, true);
  check("nor while it runs", !m.show_speaker);
  tg_time_present(&m, TG_TIME_MODE_POMODORO, true, 9, 5, 30, &p, &c, 1, false, true);
  check("pomodoro has its dots there", !m.show_speaker);
  tg_pomo_tap(&p, 0);
  tg_timer_tick(&p.timer, MIN_US(30));
  tg_time_present(&m, TG_TIME_MODE_CLOCK, true, 9, 5, 30, &p, &c, MIN_US(30), false, true);
  check("DONE covers it", m.done && !m.show_speaker);
}

int main(void) {
  test_clock();
  test_speaker();
  test_attention();
  test_ring();
  test_pomodoro();
  test_meeting_mode();
  test_compact();
  test_independence_and_done_everywhere();
  test_stable_bytes();
  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("time present: ok\n");
  return 0;
}
