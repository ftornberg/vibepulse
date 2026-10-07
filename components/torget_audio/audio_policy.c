#include "audio_policy.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
  uint16_t hz;
  uint16_t ms;
  uint16_t gap_ms;
} note;

/* C6, E6, G6: tre stigande toner, ~0,6 s (ägaren 2026-10-02). */
static const note k_done[] = {
  {1047, 180, 20},
  {1319, 180, 20},
  {1568, 220, 0},
};
/* G5, E5: två fallande, lägre och längre toner, ~0,57 s — pausen är slut
 * (ägaren 2026-10-06). Mjukare genom tonhöjd och längd, inte genom nivå. */
static const note k_break_over[] = {
  {784, 240, 30},
  {659, 300, 0},
};

typedef struct {
  const note *notes;
  unsigned count;
} cue_def;

#define CUE(a) {a, sizeof a / sizeof a[0]}
static const cue_def k_cues[TG_AUDIO_CUE_COUNT] = {
  [TG_AUDIO_CUE_DONE] = CUE(k_done),
  [TG_AUDIO_CUE_BREAK_OVER] = CUE(k_break_over),
};

static const cue_def *cue_of(int cue) {
  if (cue < 0 || cue >= TG_AUDIO_CUE_COUNT) return NULL;
  return &k_cues[cue];
}

#define FRAMES_PER_MS (TG_AUDIO_RATE / 1000u)
#define ENVELOPE_FRAMES (8u * FRAMES_PER_MS)

tg_audio_verdict tg_audio_allowed(const tg_audio_state *s, int cue) {
  if (!s || !s->built) return TG_AUDIO_NOT_BUILT;
  if (cue < 0 || cue >= TG_AUDIO_CUE_COUNT) return TG_AUDIO_BAD_CUE;
  if (s->disabled) return TG_AUDIO_DISABLED;
  if (s->night) return TG_AUDIO_NIGHT;
  if (s->ota_busy) return TG_AUDIO_OTA;
  if (s->playing) return TG_AUDIO_BUSY;
  return TG_AUDIO_OK;
}

const char *tg_audio_verdict_name(tg_audio_verdict v) {
  switch (v) {
    case TG_AUDIO_OK: return "ok";
    case TG_AUDIO_NOT_BUILT: return "not built";
    case TG_AUDIO_NIGHT: return "night";
    case TG_AUDIO_OTA: return "ota";
    case TG_AUDIO_BUSY: return "busy";
    case TG_AUDIO_DISABLED: return "disabled";
    case TG_AUDIO_BAD_CUE: return "bad cue";
  }
  return "?";
}

bool tg_audio_dma_ok(size_t largest_block) {
  return largest_block >= TG_AUDIO_DMA_NEEDED;
}

bool tg_audio_after_attempt(uint8_t *consecutive_failures, bool ok) {
  if (!consecutive_failures) return false;
  if (ok) {
    *consecutive_failures = 0;
    return false;
  }
  if (*consecutive_failures < 255) (*consecutive_failures)++;
  return *consecutive_failures >= TG_AUDIO_MAX_FAILURES;
}

bool tg_audio_after_outcome(uint8_t *consecutive_failures, tg_audio_outcome o) {
  if (o == TG_AUDIO_OUTCOME_NO_MEMORY) return false;
  return tg_audio_after_attempt(consecutive_failures, o == TG_AUDIO_OUTCOME_PLAYED);
}

uint32_t tg_audio_cue_frames(int cue) {
  const cue_def *d = cue_of(cue);
  if (!d) return 0;
  uint32_t ms = 0;
  for (unsigned i = 0; i < d->count; i++) ms += d->notes[i].ms + d->notes[i].gap_ms;
  return ms * FRAMES_PER_MS;
}

/* Ett prov ur tonen: frame räknas inom tonen (0..n-1). */
static int16_t sample_of(const note *t, uint32_t frame, uint32_t n, float amp) {
  uint32_t edge = frame < n - 1 - frame ? frame : n - 1 - frame;
  float env = edge >= ENVELOPE_FRAMES ? 1.0f : (float)edge / (float)ENVELOPE_FRAMES;
  float phase = 2.0f * (float)M_PI * (float)t->hz * (float)frame / (float)TG_AUDIO_RATE;
  return (int16_t)lrintf(amp * env * sinf(phase));
}

uint32_t tg_audio_render(int cue, uint32_t offset, int16_t *buf,
                         uint32_t frames, uint8_t level_percent) {
  const cue_def *d = cue_of(cue);
  uint32_t total = tg_audio_cue_frames(cue);
  if (!d || !buf || offset >= total) return 0;
  if (level_percent > 100) level_percent = 100;
  float amp = 32767.0f * (float)level_percent / 100.0f;
  uint32_t written = 0;
  while (written < frames && offset + written < total) {
    uint32_t pos = offset + written, start = 0;
    int16_t value = 0;
    for (unsigned i = 0; i < d->count; i++) {
      const note *t = &d->notes[i];
      uint32_t n = t->ms * FRAMES_PER_MS;
      uint32_t gap = t->gap_ms * FRAMES_PER_MS;
      if (pos < start + n) { value = sample_of(t, pos - start, n, amp); break; }
      start += n;
      if (pos < start + gap) { value = 0; break; }
      start += gap;
    }
    buf[written++] = value;
  }
  return written;
}
