#ifndef TORGET_AUDIO_CUE_H
#define TORGET_AUDIO_CUE_H

/* De ljud plattformen kan spela. LVGL-fri, så att både torget.h och den rena
 * ljudpolicyn (hosttestad utan LVGL) delar samma uppräkning. */
typedef enum {
  TG_AUDIO_CUE_DONE = 0,   /* tre stigande toner: en timer eller fokusfas är klar */
  TG_AUDIO_CUE_BREAK_OVER, /* två fallande, mjukare toner: pausen är slut */
  TG_AUDIO_CUE_COUNT,
} tg_audio_cue;

#endif
