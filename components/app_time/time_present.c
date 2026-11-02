#include "time_present.h"

#include <stdio.h>
#include <string.h>

#define DOT "  \xC2\xB7  " /* mittpunkt med luft, plex_ui_21 bär U+00B7 */

static const char *pomo_caption(tg_pomo_phase phase) {
  switch (phase) {
    case TG_POMO_FOCUS: return "FOCUS";
    case TG_POMO_SHORT_BREAK: return "SHORT BREAK";
    case TG_POMO_LONG_BREAK: return "LONG BREAK";
  }
  return "";
}

tg_time_done_source tg_time_done_source_of(const tg_pomo *p) {
  if (p && p->timer.state == TG_TIMER_DONE) return TG_TIME_DONE_POMODORO;
  return TG_TIME_DONE_NONE;
}

bool tg_time_big_is_compact(const char *big) {
  int digits = 0;
  for (const char *c = big; c && *c; c++)
    if (*c >= '0' && *c <= '9') digits++;
  return digits >= 6;
}

static void set_hint(tg_time_view_model *m, const char *text) {
  snprintf(m->hint, sizeof m->hint, "%s", text);
}

static void present_meeting(tg_time_view_model *m, const tg_meeting *mt,
                            int64_t now_us) {
  m->caption = "MEETING";
  if (mt->state == TG_MEETING_IDLE) {
    /* I vila är siffran mötets pris per timme: varje tryck syns direkt. */
    tg_meeting_kr_text(tg_meeting_hourly_kr(mt), m->big, sizeof m->big);
    set_hint(m, "KR PER HOUR" DOT "TAP TO START");
    m->show_steppers = true;
    snprintf(m->people, sizeof m->people, "%d", mt->people);
    snprintf(m->rate, sizeof m->rate, "%d", mt->rate);
    return;
  }
  int64_t elapsed = tg_meeting_elapsed_us(mt, now_us);
  char clock[16];
  tg_meeting_elapsed_text(elapsed, clock, sizeof clock);
  tg_meeting_kr_text(tg_meeting_cost_kr(mt, now_us), m->big, sizeof m->big);
  if (mt->state == TG_MEETING_RUNNING) {
    snprintf(m->hint, sizeof m->hint, "KR" DOT "%s" DOT "%d %s", clock,
             mt->people, mt->people == 1 ? "PERSON" : "PEOPLE");
  } else {
    snprintf(m->hint, sizeof m->hint, "PAUSED" DOT "%s", clock);
    m->muted = true;
  }
  m->show_reset = true;
  m->ring_end = tg_meeting_ring(mt, now_us);
}

/* En timer som inte är i vila: siffran är kvarvarande tid. */
static void fill_active(tg_time_view_model *m, const tg_timer *t, int64_t now_us) {
  tg_time_mmss_text(tg_timer_remaining_us(t, now_us), m->big, sizeof m->big);
  if (t->state == TG_TIMER_RUNNING) {
    set_hint(m, "TAP TO PAUSE");
    m->show_reset = true;
  } else if (t->state == TG_TIMER_PAUSED) {
    set_hint(m, "PAUSED");
    m->muted = true;
    m->show_reset = true;
  }
}

void tg_time_present(tg_time_view_model *m, tg_time_mode mode,
                     bool clock_valid, int hour, int minute, int second,
                     const tg_pomo *pomo, const tg_meeting *meeting,
                     int64_t now_us, bool attention, bool sound_on) {
  memset(m, 0, sizeof *m);
  m->mode = mode;
  m->caption = "";
  m->dots_done = -1;
  m->ring_start = 0;
  m->ring_end = -1;

  switch (mode) {
    case TG_TIME_MODE_CLOCK:
      tg_time_clock_text(clock_valid, hour, minute, m->big, sizeof m->big);
      {
        tg_ring_arc arc = tg_ring_seconds(clock_valid, minute, second);
        m->ring_start = arc.start;
        m->ring_end = arc.end;
      }
      /* Bara klockan: pomodoro och möte har sin rubrik på samma plats. */
      m->show_attention = attention;
      break;

    case TG_TIME_MODE_POMODORO: {
      tg_pomo_phase phase = tg_pomo_phase_of(pomo);
      m->caption = pomo_caption(phase);
      /* Klara fokusblock: (steg+1)/2, och ett fokusblock som just gått ut
       * (DONE) räknas redan. */
      m->dots_done = (pomo->step + 1) / 2;
      if (pomo->timer.state == TG_TIMER_DONE && phase == TG_POMO_FOCUS)
        m->dots_done += 1;
      if (pomo->timer.state == TG_TIMER_IDLE) {
        tg_time_mmss_text(tg_pomo_phase_us(pomo), m->big, sizeof m->big);
        set_hint(m, "TAP TO START");
      } else {
        fill_active(m, &pomo->timer, now_us);
        m->ring_end = tg_ring_remaining(&pomo->timer, now_us);
      }
      break;
    }

    case TG_TIME_MODE_MEETING:
      present_meeting(m, meeting, now_us);
      break;
  }

  /* Högtalaren bara på klockan: pomodoron har sina prickar där och mötet
   * sina stegare och RESET. Mötet låter aldrig. */
  m->speaker_on = sound_on;
  m->show_speaker = mode == TG_TIME_MODE_CLOCK;
  m->big_compact = tg_time_big_is_compact(m->big);

  /* KLAR gäller i alla lägen så länge appen syns; pomodoron kvitteras först. */
  m->done_source = tg_time_done_source_of(pomo);
  if (m->done_source != TG_TIME_DONE_NONE) {
    m->ring_start = 0;
    m->ring_end = -1;
    m->show_attention = false; /* KLAR-lagret täcker allt */
    m->show_speaker = false;
  } /* KLAR-lagret täcker ringen */
  if (m->done_source == TG_TIME_DONE_POMODORO) {
    m->done = true;
    m->caption = pomo_caption(tg_pomo_phase_of(pomo));
  }
}
