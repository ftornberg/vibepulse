#include "time_present.h"

#include <string.h>

static const char *pomo_caption(tg_pomo_phase phase) {
  switch (phase) {
    case TG_POMO_FOCUS: return "FOCUS";
    case TG_POMO_SHORT_BREAK: return "SHORT BREAK";
    case TG_POMO_LONG_BREAK: return "LONG BREAK";
  }
  return "";
}

tg_time_done_source tg_time_done_source_of(const tg_pomo *p, const tg_countdown *c) {
  if (p && p->timer.state == TG_TIMER_DONE) return TG_TIME_DONE_POMODORO;
  if (c && c->timer.state == TG_TIMER_DONE) return TG_TIME_DONE_TIMER;
  return TG_TIME_DONE_NONE;
}

/* En timer som inte är i vila: siffran är kvarvarande tid. */
static void fill_active(tg_time_view_model *m, const tg_timer *t, int64_t now_us) {
  tg_time_mmss_text(tg_timer_remaining_us(t, now_us), m->big, sizeof m->big);
  if (t->state == TG_TIMER_RUNNING) {
    m->hint = "TAP TO PAUSE";
    m->show_reset = true;
  } else if (t->state == TG_TIMER_PAUSED) {
    m->hint = "PAUSED";
    m->muted = true;
    m->show_reset = true;
  }
}

void tg_time_present(tg_time_view_model *m, tg_time_mode mode,
                     bool clock_valid, int hour, int minute, int second,
                     const tg_pomo *pomo, const tg_countdown *count,
                     int64_t now_us) {
  memset(m, 0, sizeof *m);
  m->mode = mode;
  m->caption = "";
  m->hint = "";
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
        m->hint = "TAP TO START";
      } else {
        fill_active(m, &pomo->timer, now_us);
        m->ring_end = tg_ring_remaining(&pomo->timer, now_us);
      }
      break;
    }

    case TG_TIME_MODE_TIMER:
      m->caption = "TIMER";
      if (count->timer.state == TG_TIMER_IDLE) {
        /* Ingen längd vald: ett streck, aldrig en påhittad nolla. */
        tg_time_clock_text(false, 0, 0, m->big, sizeof m->big);
        m->show_presets = true;
        m->hint = "CHOOSE MINUTES";
      } else {
        fill_active(m, &count->timer, now_us);
        m->ring_end = tg_ring_remaining(&count->timer, now_us);
      }
      break;
  }

  /* KLAR gäller i alla lägen så länge appen syns; pomodoron kvitteras först. */
  m->done_source = tg_time_done_source_of(pomo, count);
  if (m->done_source != TG_TIME_DONE_NONE) { m->ring_start = 0; m->ring_end = -1; } /* KLAR-lagret täcker ringen */
  if (m->done_source == TG_TIME_DONE_POMODORO) {
    m->done = true;
    m->caption = pomo_caption(tg_pomo_phase_of(pomo));
  } else if (m->done_source == TG_TIME_DONE_TIMER) {
    m->done = true;
    m->caption = "TIMER";
  }
}
