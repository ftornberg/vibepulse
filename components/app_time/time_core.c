#include "time_core.h"

#include <stdio.h>
#include <string.h>

_Static_assert(TG_TIME_ROT_CLOCK >= 0 && TG_TIME_ROT_CLOCK <= 3 &&
               TG_TIME_ROT_POMODORO >= 0 && TG_TIME_ROT_POMODORO <= 3 &&
               TG_TIME_ROT_MEETING >= 0 && TG_TIME_ROT_MEETING <= 3,
               "rotationsvärden är kvartsvarv 0..3");
_Static_assert(TG_TIME_ROT_CLOCK != TG_TIME_ROT_POMODORO &&
               TG_TIME_ROT_CLOCK != TG_TIME_ROT_MEETING &&
               TG_TIME_ROT_POMODORO != TG_TIME_ROT_MEETING,
               "varje läge behöver en egen rotation");

tg_time_mode tg_time_mode_for(int rot, tg_time_mode last) {
  if (rot == TG_TIME_ROT_CLOCK) return TG_TIME_MODE_CLOCK;
  if (rot == TG_TIME_ROT_POMODORO) return TG_TIME_MODE_POMODORO;
  if (rot == TG_TIME_ROT_MEETING) return TG_TIME_MODE_MEETING;
  return last;
}

void tg_timer_init(tg_timer *t) {
  t->state = TG_TIMER_IDLE;
  t->deadline_us = 0;
  t->remaining_us = 0;
  t->total_us = 0;
}

bool tg_timer_start(tg_timer *t, int64_t now_us, int64_t duration_us) {
  if (t->state != TG_TIMER_IDLE || duration_us <= 0) return false;
  t->state = TG_TIMER_RUNNING;
  t->deadline_us = now_us + duration_us;
  t->remaining_us = 0;
  t->total_us = duration_us;
  return true;
}

bool tg_timer_tick_expired(tg_timer *t, int64_t now_us) {
  if (t->state == TG_TIMER_RUNNING && now_us >= t->deadline_us) {
    t->state = TG_TIMER_DONE;
    return true;
  }
  return false;
}

void tg_timer_tick(tg_timer *t, int64_t now_us) {
  (void)tg_timer_tick_expired(t, now_us);
}

void tg_timer_toggle(tg_timer *t, int64_t now_us) {
  tg_timer_tick(t, now_us);
  if (t->state == TG_TIMER_RUNNING) {
    t->remaining_us = t->deadline_us - now_us;
    t->state = TG_TIMER_PAUSED;
  } else if (t->state == TG_TIMER_PAUSED) {
    t->deadline_us = now_us + t->remaining_us;
    t->state = TG_TIMER_RUNNING;
  }
}

void tg_timer_cancel(tg_timer *t) { tg_timer_init(t); }

int64_t tg_timer_remaining_us(const tg_timer *t, int64_t now_us) {
  switch (t->state) {
    case TG_TIMER_RUNNING: {
      int64_t left = t->deadline_us - now_us;
      return left > 0 ? left : 0;
    }
    case TG_TIMER_PAUSED:
      return t->remaining_us > 0 ? t->remaining_us : 0;
    default:
      return 0;
  }
}

static const int k_pomo_minutes[TG_POMO_STEPS] = {25, 5, 25, 5, 25, 5, 25, 15};

void tg_pomo_init(tg_pomo *p) {
  tg_timer_init(&p->timer);
  p->step = 0;
}

tg_pomo_phase tg_pomo_phase_of(const tg_pomo *p) {
  if (p->step == TG_POMO_STEPS - 1) return TG_POMO_LONG_BREAK;
  return (p->step % 2 == 0) ? TG_POMO_FOCUS : TG_POMO_SHORT_BREAK;
}

int64_t tg_pomo_phase_us(const tg_pomo *p) {
  return (int64_t)k_pomo_minutes[p->step] * 60LL * 1000000LL;
}

void tg_pomo_tap(tg_pomo *p, int64_t now_us) {
  /* Löpet gick ut just under trycket: visa KLAR först, kvittera aldrig en
   * markering användaren inte hunnit se. */
  if (tg_timer_tick_expired(&p->timer, now_us)) return;
  switch (p->timer.state) {
    case TG_TIMER_IDLE:
      tg_timer_start(&p->timer, now_us, tg_pomo_phase_us(p));
      break;
    case TG_TIMER_RUNNING:
    case TG_TIMER_PAUSED:
      tg_timer_toggle(&p->timer, now_us);
      break;
    case TG_TIMER_DONE:
      tg_timer_cancel(&p->timer);
      p->step = (p->step + 1) % TG_POMO_STEPS;
      break;
  }
}

void tg_pomo_reset(tg_pomo *p) { tg_pomo_init(p); }

static int clamp_int(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static int round_rate(int rate) {
  int steps = (rate + TG_MEETING_RATE_STEP / 2) / TG_MEETING_RATE_STEP;
  return clamp_int(steps * TG_MEETING_RATE_STEP, TG_MEETING_RATE_MIN,
                   TG_MEETING_RATE_MAX);
}

void tg_meeting_init(tg_meeting *m, int people, int rate) {
  memset(m, 0, sizeof *m);
  m->state = TG_MEETING_IDLE;
  m->people = clamp_int(people, TG_MEETING_PEOPLE_MIN, TG_MEETING_PEOPLE_MAX);
  m->rate = round_rate(rate);
}

bool tg_meeting_step_people(tg_meeting *m, int delta) {
  if (m->state != TG_MEETING_IDLE) return false;
  int next = clamp_int(m->people + delta, TG_MEETING_PEOPLE_MIN,
                       TG_MEETING_PEOPLE_MAX);
  if (next == m->people) return false;
  m->people = next;
  return true;
}

bool tg_meeting_step_rate(tg_meeting *m, int delta_steps) {
  if (m->state != TG_MEETING_IDLE) return false;
  int next = clamp_int(m->rate + delta_steps * TG_MEETING_RATE_STEP,
                       TG_MEETING_RATE_MIN, TG_MEETING_RATE_MAX);
  if (next == m->rate) return false;
  m->rate = next;
  return true;
}

int64_t tg_meeting_elapsed_us(const tg_meeting *m, int64_t now_us) {
  int64_t e = 0;
  if (m->state == TG_MEETING_RUNNING) e = now_us - m->start_us;
  else if (m->state == TG_MEETING_PAUSED) e = m->elapsed_us;
  if (e < 0) e = 0;
  if (e > TG_MEETING_MAX_US) e = TG_MEETING_MAX_US;
  return e;
}

void tg_meeting_tick(tg_meeting *m, int64_t now_us) {
  if (m->state == TG_MEETING_RUNNING &&
      now_us - m->start_us >= TG_MEETING_MAX_US) {
    m->state = TG_MEETING_PAUSED;
    m->elapsed_us = TG_MEETING_MAX_US;
  }
}

void tg_meeting_tap(tg_meeting *m, int64_t now_us) {
  tg_meeting_tick(m, now_us);
  switch (m->state) {
    case TG_MEETING_IDLE:
      m->state = TG_MEETING_RUNNING;
      m->start_us = now_us;
      m->elapsed_us = 0;
      break;
    case TG_MEETING_RUNNING:
      m->elapsed_us = tg_meeting_elapsed_us(m, now_us);
      m->state = TG_MEETING_PAUSED;
      break;
    case TG_MEETING_PAUSED:
      /* Vid maxtiden finns inget att återuppta: den står kvar pausad. */
      if (m->elapsed_us >= TG_MEETING_MAX_US) break;
      m->start_us = now_us - m->elapsed_us;
      m->state = TG_MEETING_RUNNING;
      break;
  }
}

void tg_meeting_reset(tg_meeting *m) { tg_meeting_init(m, m->people, m->rate); }

int64_t tg_meeting_hourly_kr(const tg_meeting *m) {
  return (int64_t)m->people * (int64_t)m->rate;
}

int64_t tg_meeting_cost_kr(const tg_meeting *m, int64_t now_us) {
  /* Högst 495 000 kr/h x 3.6e10 us: ryms gott i int64. */
  return tg_meeting_hourly_kr(m) * tg_meeting_elapsed_us(m, now_us) /
         (3600LL * 1000000LL);
}

int tg_meeting_ring(const tg_meeting *m, int64_t now_us) {
  if (m->state == TG_MEETING_IDLE) return -1;
  const int64_t hour = 3600LL * 1000000LL;
  return (int)(tg_meeting_elapsed_us(m, now_us) % hour * 1000 / hour);
}

void tg_meeting_kr_text(int64_t kr, char *out, size_t cap) {
  if (cap == 0) return;
  if (kr < 0) kr = 0;
  char digits[24];
  int n = snprintf(digits, sizeof digits, "%lld", (long long)kr);
  size_t o = 0;
  for (int i = 0; i < n && o + 1 < cap; i++) {
    if (i > 0 && (n - i) % 3 == 0) {
      if (o + 2 >= cap) break;
      out[o++] = ' ';
    }
    out[o++] = digits[i];
  }
  out[o] = '\0';
}

void tg_meeting_elapsed_text(int64_t elapsed_us, char *out, size_t cap) {
  if (cap == 0) return;
  if (elapsed_us < 0) elapsed_us = 0;
  int64_t s = elapsed_us / 1000000LL;
  int h = (int)(s / 3600), mi = (int)(s / 60 % 60), se = (int)(s % 60);
  if (h > 0) snprintf(out, cap, "%d:%02d:%02d", h, mi, se);
  else snprintf(out, cap, "%02d:%02d", mi, se);
}

int tg_time_advance(tg_pomo *p, int64_t now_us) {
  if (p && tg_timer_tick_expired(&p->timer, now_us)) return TG_TIME_EXPIRED_POMODORO;
  return 0;
}

int tg_time_cue_for(int expired, const tg_pomo *p) {
  if (!(expired & TG_TIME_EXPIRED_POMODORO)) return -1;
  if (p && tg_pomo_phase_of(p) != TG_POMO_FOCUS) return TG_AUDIO_CUE_BREAK_OVER;
  return TG_AUDIO_CUE_DONE;
}

bool tg_time_clock_valid(int64_t epoch_s) {
  return epoch_s >= TG_CLOCK_VALID_EPOCH_S;
}

void tg_time_clock_text(bool valid, int hour, int minute, char *out, size_t cap) {
  if (cap == 0) return;
  if (valid && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59)
    snprintf(out, cap, "%02d:%02d", hour, minute);
  else
    snprintf(out, cap, "%s", TG_TIME_NO_VALUE);
}

void tg_time_mmss_text(int64_t remaining_us, char *out, size_t cap) {
  if (cap == 0) return;
  if (remaining_us < 0) remaining_us = 0;
  int64_t seconds = (remaining_us + 999999LL) / 1000000LL; /* runda UPP */
  if (seconds > 99LL * 60LL + 59LL) seconds = 99LL * 60LL + 59LL;
  snprintf(out, cap, "%02d:%02d", (int)(seconds / 60), (int)(seconds % 60));
}

tg_ring_arc tg_ring_seconds(bool valid, int minute, int second) {
  tg_ring_arc arc = { 0, -1 };
  if (!valid || minute < 0 || minute > 59 || second < 0 || second > 59)
    return arc;
  int edge = (second + 1) * 1000 / 60;
  if (minute % 2 == 1) {        /* udda: den fyllda delen växer medurs */
    arc.start = 0;
    arc.end = edge;
  } else {                      /* jämn: den tomma delen växer medurs */
    arc.start = edge;
    arc.end = 1000;
  }
  return arc;
}

int tg_ring_remaining(const tg_timer *t, int64_t now_us) {
  if ((t->state != TG_TIMER_RUNNING && t->state != TG_TIMER_PAUSED) ||
      t->total_us <= 0)
    return -1;
  int64_t left = tg_timer_remaining_us(t, now_us);
  int64_t permille = (left * 1000 + t->total_us - 1) / t->total_us;
  if (permille > 1000) permille = 1000;
  if (permille < 0) permille = 0;
  return (int)permille;
}
