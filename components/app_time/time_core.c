#include "time_core.h"

#include <stdio.h>

_Static_assert(TG_TIME_ROT_CLOCK >= 0 && TG_TIME_ROT_CLOCK <= 3 &&
               TG_TIME_ROT_POMODORO >= 0 && TG_TIME_ROT_POMODORO <= 3 &&
               TG_TIME_ROT_TIMER >= 0 && TG_TIME_ROT_TIMER <= 3,
               "rotationsvärden är kvartsvarv 0..3");
_Static_assert(TG_TIME_ROT_CLOCK != TG_TIME_ROT_POMODORO &&
               TG_TIME_ROT_CLOCK != TG_TIME_ROT_TIMER &&
               TG_TIME_ROT_POMODORO != TG_TIME_ROT_TIMER,
               "varje läge behöver en egen rotation");

tg_time_mode tg_time_mode_for(int rot, tg_time_mode last) {
  if (rot == TG_TIME_ROT_CLOCK) return TG_TIME_MODE_CLOCK;
  if (rot == TG_TIME_ROT_POMODORO) return TG_TIME_MODE_POMODORO;
  if (rot == TG_TIME_ROT_TIMER) return TG_TIME_MODE_TIMER;
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

static const int k_preset_minutes[TG_COUNTDOWN_PRESETS] = {20, 40, 50};

void tg_countdown_init(tg_countdown *c) {
  tg_timer_init(&c->timer);
  c->preset = 0;
}

int tg_countdown_preset_minutes(int preset) {
  if (preset < 0 || preset >= TG_COUNTDOWN_PRESETS) return 0;
  return k_preset_minutes[preset];
}

bool tg_countdown_start(tg_countdown *c, int preset, int64_t now_us) {
  int minutes = tg_countdown_preset_minutes(preset);
  if (minutes <= 0) return false;
  if (!tg_timer_start(&c->timer, now_us, (int64_t)minutes * 60LL * 1000000LL))
    return false;
  c->preset = preset;
  return true;
}

void tg_countdown_tap(tg_countdown *c, int64_t now_us) {
  if (tg_timer_tick_expired(&c->timer, now_us)) return;
  switch (c->timer.state) {
    case TG_TIMER_RUNNING:
    case TG_TIMER_PAUSED:
      tg_timer_toggle(&c->timer, now_us);
      break;
    case TG_TIMER_DONE:
      tg_timer_cancel(&c->timer);
      break;
    case TG_TIMER_IDLE:
      break;
  }
}

void tg_countdown_reset(tg_countdown *c) { tg_countdown_init(c); }

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

int tg_ring_seconds(bool valid, int second) {
  if (!valid || second < 0 || second > 59) return -1;
  return (second + 1) * 1000 / 60;
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
