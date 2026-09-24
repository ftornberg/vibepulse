#include "battery_policy.h"

static tg_batt_state next_state(const tg_batt_policy *p, const tg_batt_sample *s,
                                int64_t now_us) {
  if (!s->present) return TG_BATT_UNKNOWN;
  if (s->vbus) return s->charge_done ? TG_BATT_FULL : TG_BATT_CHARGING;
  int pct = s->percent;
  if (pct < 0) return TG_BATT_ON_BATTERY; /* ingen mätare: aldrig larm på gissning */
  switch (p->state) {
    case TG_BATT_CRITICAL:
      if (pct >= TG_BATT_CRITICAL_EXIT_PCT)
        return pct >= TG_BATT_LOW_EXIT_PCT ? TG_BATT_ON_BATTERY : TG_BATT_LOW;
      return TG_BATT_CRITICAL;
    case TG_BATT_LOW:
      if (pct >= TG_BATT_LOW_EXIT_PCT) return TG_BATT_ON_BATTERY;
      if (pct <= TG_BATT_CRITICAL_PCT && p->critical_since_us &&
          (p->critical_since_us == 1 ? now_us : now_us - p->critical_since_us) >= TG_BATT_CRITICAL_DWELL_US)
        return TG_BATT_CRITICAL;
      return TG_BATT_LOW;
    default:
      return pct <= TG_BATT_LOW_PCT ? TG_BATT_LOW : TG_BATT_ON_BATTERY;
  }
}

tg_batt_verdict tg_batt_update(tg_batt_policy *p, const tg_batt_sample *s,
                               int64_t now_us) {
  tg_batt_verdict v = {0};
  tg_batt_state before = p->state;
  int before_pct = p->last_percent;

  if (!s->valid) {
    if (++p->failures >= TG_BATT_FAILURES_TO_UNKNOWN) {
      p->state = TG_BATT_UNKNOWN;
      p->critical_since_us = 0;
    }
  } else {
    p->failures = 0;
    /* Fördröjningsklockan: går bara medan USB är borta och pct <= 5. */
    if (!s->vbus && s->present && s->percent >= 0 &&
        s->percent <= TG_BATT_CRITICAL_PCT) {
      if (!p->critical_since_us) p->critical_since_us = now_us ? now_us : 1;
    } else {
      if (p->critical_since_us) p->critical_since_us = now_us ? now_us : 1;
    }
    p->state = next_state(p, s, now_us);
    if (p->state != TG_BATT_CRITICAL && p->state != TG_BATT_LOW)
      p->shutdown_sent = false;
  }

  v.state = p->state;
  v.percent = (p->state == TG_BATT_UNKNOWN || !s->valid) ? p->last_percent
                                                          : s->percent;
  if (p->state == TG_BATT_UNKNOWN) v.percent = -1;
  if (s->valid && p->state != TG_BATT_UNKNOWN) p->last_percent = s->percent;
  if (p->state == TG_BATT_UNKNOWN) p->last_percent = -1;

  v.bright_cap = (p->state == TG_BATT_LOW || p->state == TG_BATT_CRITICAL)
                     ? TG_BATT_NIGHT_CAP : 100;
  if (p->state == TG_BATT_CRITICAL && p->shutdown_armed && !p->shutdown_sent) {
    p->shutdown_sent = true;
    v.shutdown = true;
  }
  v.changed = (p->state != before) || (v.percent != before_pct);
  return v;
}

const char *tg_batt_state_name(tg_batt_state s) {
  switch (s) {
    case TG_BATT_CHARGING: return "laddar";
    case TG_BATT_FULL: return "full";
    case TG_BATT_ON_BATTERY: return "på batteri";
    case TG_BATT_LOW: return "låg";
    case TG_BATT_CRITICAL: return "kritisk";
    default: return "okänd";
  }
}
