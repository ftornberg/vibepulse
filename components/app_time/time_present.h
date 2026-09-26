#ifndef TORGET_TIME_PRESENT_H
#define TORGET_TIME_PRESENT_H

#include <stdbool.h>
#include <stdint.h>

#include "time_core.h"

/*
 * Presentatören: kärnans tillstånd in, ett vymodell-objekt ut. Ren C, så att
 * "vilken text står var" hosttestas i stället för att läsas ur LVGL-koden.
 * Vymodellen fylls alltid från noll (memset) så att två lika indata ger lika
 * BYTES — vyn jämför med memcmp för att hoppa över onödiga omritningar.
 * Strängpekarna pekar på statiska litteraler, `big` är en egen buffert.
 */

typedef enum {
  TG_TIME_DONE_NONE = 0,
  TG_TIME_DONE_POMODORO,
  TG_TIME_DONE_TIMER,
} tg_time_done_source;

typedef struct {
  tg_time_mode mode;
  char big[16];          /* "HH:MM" | "MM:SS" | TG_TIME_NO_VALUE (13 byte) */
  const char *caption;   /* "", "FOCUS", "SHORT BREAK", "LONG BREAK", "TIMER" */
  const char *hint;      /* "", "TAP TO START", "TAP TO PAUSE", "PAUSED", "CHOOSE MINUTES" */
  int dots_done;         /* pomodoro: klara fokusblock 0..4; -1 döljer raden */
  bool show_presets;     /* timern i vila: 20/40/50 */
  bool show_reset;       /* något går eller är pausat */
  bool done;             /* helskärmsmarkeringen */
  bool muted;            /* pausad: siffran dämpas */
  tg_time_done_source done_source;
  int ring_start;        /* ringbågen i promille, medurs från 12 */
  int ring_end;          /* -1 döljer ringen */
} tg_time_view_model;

/* Vem av timrarna som är klar (pomodoro först), annars NONE. NULL-säker. */
tg_time_done_source tg_time_done_source_of(const tg_pomo *p, const tg_countdown *c);

/* Anroparen har redan tickat timrarna. KLAR-markeringen gäller i alla lägen. */
void tg_time_present(tg_time_view_model *m, tg_time_mode mode,
                     bool clock_valid, int hour, int minute, int second,
                     const tg_pomo *pomo, const tg_countdown *count,
                     int64_t now_us);

#endif
