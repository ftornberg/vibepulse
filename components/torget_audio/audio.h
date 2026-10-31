#ifndef TORGET_AUDIO_ENGINE_H
#define TORGET_AUDIO_ENGINE_H

#include <stdbool.h>

#include "audio_cue.h"

/* Startar en signal i en kortlivad task: mäter DMA, skapar I2S + ES8311,
 * spelar, river allt. Svarar false om en signal redan spelas, ljudet är
 * avstängt efter fel, eller tasken inte kunde skapas. Aldrig blockerande. */
bool tg_audio_engine_start_cue(tg_audio_cue cue);
bool tg_audio_engine_playing(void);
bool tg_audio_engine_disabled(void);

#endif
