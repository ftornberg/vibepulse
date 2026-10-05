#ifndef TORGET_AUDIO_POLICY_H
#define TORGET_AUDIO_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../platform/audio_cue.h"

/*
 * Ljudets regler, ren C och hosttestad (test/test_audio_policy.c). Motorn
 * lånar I2S och ES8311 per signal och lämnar tillbaka allt; reglerna här
 * bestämmer OM den får, och genererar PCM i små bitar så att ingen stor
 * buffert behövs. Spec: docs/superpowers/specs/2026-10-02-tid-sound-design.md.
 */

#define TG_AUDIO_RATE 16000u
/* Skärmens flush: 480 x 12 rader x 2 byte. Panelen har frusit två gånger när
 * något annat åt upp det blocket (docs/lessons.md 2026-08-14/16). */
#define TG_AUDIO_FLUSH_BYTES 11520u
/* Ljudets DMA: 3 deskriptorer x 256 ramar x 2 byte (16-bit mono). */
#define TG_AUDIO_DMA_BYTES 1536u
#define TG_AUDIO_DMA_MARGIN 8192u
#define TG_AUDIO_DMA_NEEDED \
  (TG_AUDIO_FLUSH_BYTES + TG_AUDIO_DMA_BYTES + TG_AUDIO_DMA_MARGIN)
#define TG_AUDIO_MAX_FAILURES 3u
/* Tystnad efter sista tonen: minst alla DMA-buffertar (3 x 256 ramar) som kan
 * ligga kvar när sista skrivningen returnerar, annars klipper nedrivningen
 * sista tonens nedtoning (ett klick). 1024 ramar = 64 ms. */
#define TG_AUDIO_TAIL_FRAMES 1024u
/* Tystnad FÖRE signalen: ES8311-drivrutinen slår på förstärkaren och slår av
 * mute i samma ögonblick (es8311_enable), så den första tonen spelades medan
 * förstärkaren ännu vaknade — hört på ägarens panel 2026-10-05 som en svag
 * första ton. 2560 ramar = 160 ms, hela DMA-bitar. */
#define TG_AUDIO_LEAD_FRAMES 2560u

typedef struct {
  bool built;     /* TK_TID_SOUND på och kortet är 2.16 */
  bool night;     /* nattschemat gäller nu */
  bool ota_busy;  /* underhållsfönstret står öppet */
  bool playing;   /* en signal spelas redan */
  bool disabled;  /* avstängt efter upprepade fel, till nästa boot */
} tg_audio_state;

typedef enum {
  TG_AUDIO_OK = 0,
  TG_AUDIO_NOT_BUILT,
  TG_AUDIO_NIGHT,
  TG_AUDIO_OTA,
  TG_AUDIO_BUSY,
  TG_AUDIO_DISABLED,
  TG_AUDIO_BAD_CUE,
} tg_audio_verdict;

tg_audio_verdict tg_audio_allowed(const tg_audio_state *s, int cue);
const char *tg_audio_verdict_name(tg_audio_verdict v);

/* Största interna DMA-blocket räcker för flush + ljud + marginal. */
bool tg_audio_dma_ok(size_t largest_block);

/* Efter ett försök: nollställer vid lyckat, räknar upp vid fel. Svarar true
 * när ljudet ska stängas av till nästa boot (TG_AUDIO_MAX_FAILURES i rad). */
bool tg_audio_after_attempt(uint8_t *consecutive_failures, bool ok);

/* Hur ett försök slutade. Bara ett STARTFEL räknas mot tregångersregeln:
 * nekat för minne (DMA-marginalen) är en vägran, inte ett fel, och ska inte
 * kunna stänga av ljudet till nästa boot (slutgranskningen 2026-10-04). */
typedef enum {
  TG_AUDIO_OUTCOME_PLAYED = 0,
  TG_AUDIO_OUTCOME_INIT_FAILED,
  TG_AUDIO_OUTCOME_NO_MEMORY,
} tg_audio_outcome;
bool tg_audio_after_outcome(uint8_t *consecutive_failures, tg_audio_outcome o);

/* Antal ramar en signal har; 0 för en okänd. */
uint32_t tg_audio_cue_frames(int cue);

/* Fyller buf med upp till `frames` ramar från `offset`. Svarar med antal
 * skrivna ramar; 0 när signalen är slut. level_percent 0..100. Varje ton
 * börjar och slutar på noll (8 ms linjär upp- och nedtoning). */
uint32_t tg_audio_render(int cue, uint32_t offset, int16_t *buf,
                         uint32_t frames, uint8_t level_percent);

#endif
