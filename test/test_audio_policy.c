#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../components/torget_audio/audio_policy.h"

static int failures;
static void check(const char *what, int c) {
  if (!c) { printf("FAIL %s\n", what); failures++; }
}

static tg_audio_state ok_state(void) {
  tg_audio_state s = {0};
  s.built = true;
  return s;
}

static void test_verdicts(void) {
  tg_audio_state s = ok_state();
  check("ok", tg_audio_allowed(&s, TG_AUDIO_CUE_DONE) == TG_AUDIO_OK);
  s.built = false;
  check("not built", tg_audio_allowed(&s, TG_AUDIO_CUE_DONE) == TG_AUDIO_NOT_BUILT);
  s = ok_state(); s.night = true;
  check("night", tg_audio_allowed(&s, TG_AUDIO_CUE_DONE) == TG_AUDIO_NIGHT);
  s = ok_state(); s.ota_busy = true;
  check("ota", tg_audio_allowed(&s, TG_AUDIO_CUE_DONE) == TG_AUDIO_OTA);
  s = ok_state(); s.playing = true;
  check("busy", tg_audio_allowed(&s, TG_AUDIO_CUE_DONE) == TG_AUDIO_BUSY);
  s = ok_state(); s.disabled = true;
  check("disabled", tg_audio_allowed(&s, TG_AUDIO_CUE_DONE) == TG_AUDIO_DISABLED);
  s = ok_state();
  check("bad cue", tg_audio_allowed(&s, TG_AUDIO_CUE_COUNT) == TG_AUDIO_BAD_CUE);
  check("negative cue", tg_audio_allowed(&s, -1) == TG_AUDIO_BAD_CUE);
  check("NULL state", tg_audio_allowed(NULL, TG_AUDIO_CUE_DONE) == TG_AUDIO_NOT_BUILT);
  check("names exist", strcmp(tg_audio_verdict_name(TG_AUDIO_NIGHT), "night") == 0);
}

static void test_dma_gate(void) {
  check("needed is flush + audio + margin",
        TG_AUDIO_DMA_NEEDED == 11520u + 1536u + 8192u);
  check("one byte under refuses", !tg_audio_dma_ok(TG_AUDIO_DMA_NEEDED - 1));
  check("exactly enough plays", tg_audio_dma_ok(TG_AUDIO_DMA_NEEDED));
  check("zero refuses", !tg_audio_dma_ok(0));
}

static void test_failures(void) {
  uint8_t f = 0;
  check("first failure keeps sound", !tg_audio_after_attempt(&f, false));
  check("second failure keeps sound", !tg_audio_after_attempt(&f, false));
  check("third failure disables", tg_audio_after_attempt(&f, false));
  f = 2;
  check("a success resets", !tg_audio_after_attempt(&f, true) && f == 0);
  check("NULL safe", !tg_audio_after_attempt(NULL, false));
}

static int crossings(const int16_t *b, uint32_t n) {
  int c = 0;
  for (uint32_t i = 1; i < n; i++)
    if ((b[i - 1] < 0 && b[i] >= 0) || (b[i - 1] >= 0 && b[i] < 0)) c++;
  return c;
}

static void test_render(void) {
  uint32_t total = tg_audio_cue_frames(TG_AUDIO_CUE_DONE);
  check("620 ms at 16 kHz", total == 620u * 16u);
  check("bad cue has no frames", tg_audio_cue_frames(TG_AUDIO_CUE_COUNT) == 0);

  int16_t *all = calloc(total, sizeof *all);
  check("render all", tg_audio_render(TG_AUDIO_CUE_DONE, 0, all, total, 45) == total);
  check("starts at zero", all[0] == 0);
  check("ends at zero", all[total - 1] == 0);

  int peak = 0;
  for (uint32_t i = 0; i < total; i++) {
    int v = all[i] < 0 ? -all[i] : all[i];
    if (v > peak) peak = v;
  }
  check("peak within the level", peak <= 32767 * 45 / 100 + 1);
  check("peak is audible", peak > 32767 * 45 / 100 * 9 / 10);

  /* note 1: 180 ms = 2880 frames at 1047 Hz -> ~377 crossings */
  int c1 = crossings(all, 2880);
  check("note 1 is ~1047 Hz", c1 >= 370 && c1 <= 384);
  /* the 20 ms gap after note 1 is silent */
  int gap_peak = 0;
  for (uint32_t i = 2880; i < 2880 + 320; i++)
    if (abs(all[i]) > gap_peak) gap_peak = abs(all[i]);
  check("gap is silent", gap_peak == 0);
  int c2 = crossings(all + 3200, 2880);
  check("note 2 is ~1319 Hz", c2 >= 468 && c2 <= 482);
  int c3 = crossings(all + 6400, 3520);
  check("note 3 is ~1568 Hz", c3 >= 682 && c3 <= 698);

  /* chunked render equals a single render */
  int16_t *chunked = calloc(total, sizeof *chunked);
  uint32_t off = 0;
  for (;;) {
    uint32_t n = tg_audio_render(TG_AUDIO_CUE_DONE, off, chunked + off,
                                 off + 256 <= total ? 256 : total - off, 45);
    if (n == 0) break;
    off += n;
  }
  check("chunks cover everything", off == total);
  check("chunked equals one-shot", memcmp(all, chunked, total * sizeof *all) == 0);
  check("past the end renders nothing",
        tg_audio_render(TG_AUDIO_CUE_DONE, total, chunked, 256, 45) == 0);
  check("level 0 is silence",
        tg_audio_render(TG_AUDIO_CUE_DONE, 0, chunked, 256, 0) == 256 &&
        chunked[100] == 0);
  free(all);
  free(chunked);
}

static void test_outcomes(void) {
  uint8_t f = 0;
  /* Only an init failure counts toward disabling sound (spec: memory and
   * init failure are separate refusals; final review 2026-10-04). */
  check("no memory never counts", !tg_audio_after_outcome(&f, TG_AUDIO_OUTCOME_NO_MEMORY) && f == 0);
  for (int i = 0; i < 5; i++) tg_audio_after_outcome(&f, TG_AUDIO_OUTCOME_NO_MEMORY);
  check("five memory refusals keep sound on", f == 0);
  check("init failure 1", !tg_audio_after_outcome(&f, TG_AUDIO_OUTCOME_INIT_FAILED));
  check("memory refusal between keeps the count", !tg_audio_after_outcome(&f, TG_AUDIO_OUTCOME_NO_MEMORY) && f == 1);
  check("init failure 2", !tg_audio_after_outcome(&f, TG_AUDIO_OUTCOME_INIT_FAILED));
  check("init failure 3 disables", tg_audio_after_outcome(&f, TG_AUDIO_OUTCOME_INIT_FAILED));
  f = 2;
  check("played resets", !tg_audio_after_outcome(&f, TG_AUDIO_OUTCOME_PLAYED) && f == 0);
  /* The tail must cover every DMA buffer still queued when the last write
   * returns, or teardown cuts the last note's release ramp. */
  check("tail covers the queued DMA", TG_AUDIO_TAIL_FRAMES >= 3u * 256u);
  /* The ES8311 driver enables the amplifier and unmutes in the same instant
   * (es8311_enable), so the first note used to play while the amplifier was
   * still waking: heard on the owner's panel 2026-10-05 as a faint first note.
   * At least 150 ms of silence leads every cue. */
  check("lead-in lets the amplifier wake", TG_AUDIO_LEAD_FRAMES >= 150u * 16u);
  check("lead-in fills whole DMA chunks", TG_AUDIO_LEAD_FRAMES % 256u == 0);
}

int main(void) {
  test_verdicts();
  test_outcomes();
  test_dma_gate();
  test_failures();
  test_render();
  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("audio policy: ok\n");
  return 0;
}
