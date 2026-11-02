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
 * `caption` pekar på statiska litteraler; texterna som byggs (`big`, `hint`,
 * stegarnas värden) är egna buffertar.
 */

typedef enum {
  TG_TIME_DONE_NONE = 0,
  TG_TIME_DONE_POMODORO,
} tg_time_done_source;

typedef struct {
  tg_time_mode mode;
  char big[16];          /* "HH:MM" | "MM:SS" | "4 950 000" | TG_TIME_NO_VALUE */
  bool big_compact;      /* sex eller sju siffror: den mindre sifferfonten */
  const char *caption;   /* "", "FOCUS", "SHORT BREAK", "LONG BREAK", "MEETING" */
  char hint[48];         /* "TAP TO START", "KR  ·  27:40  ·  6 PEOPLE", ... */
  int dots_done;         /* pomodoro: klara fokusblock 0..4; -1 döljer raden */
  bool show_steppers;    /* mötet i vila: antal och timkostnad */
  char people[4];        /* "1".."99" */
  char rate[8];          /* "100".."5000" */
  bool show_reset;       /* något går eller är pausat */
  bool done;             /* helskärmsmarkeringen */
  bool muted;            /* pausad: siffran dämpas */
  tg_time_done_source done_source;
  int ring_start;        /* ringbågen i promille, medurs från 12 */
  int ring_end;          /* -1 döljer ringen */
  bool show_attention;   /* en annan app väntar på dig: ikonen ovanför klockan */
  bool show_speaker;     /* högtalarsymbolen nere i ringen */
  bool speaker_on;       /* ljudet påslaget (annars överstruken symbol) */
} tg_time_view_model;

/* Om pomodoron är klar, annars NONE. NULL-säker. */
tg_time_done_source tg_time_done_source_of(const tg_pomo *p);

/* Den stora siffran blir för bred för 118 px innanför ringen vid sex siffror
 * ("107 917"); därifrån används 84 px. Ren funktion, så gränsen testas. */
bool tg_time_big_is_compact(const char *big);

/* Anroparen har redan tickat pomodoron och mötet. KLAR-markeringen gäller i
 * alla lägen. */
void tg_time_present(tg_time_view_model *m, tg_time_mode mode,
                     bool clock_valid, int hour, int minute, int second,
                     const tg_pomo *pomo, const tg_meeting *meeting,
                     int64_t now_us, bool attention, bool sound_on);

#endif
