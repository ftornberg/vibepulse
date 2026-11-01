#ifndef TORGET_GLASS_CLAIM_H
#define TORGET_GLASS_CLAIM_H

#include <stdbool.h>

/*
 * Glasets anspråk: ren regel, hosttestad (test/test_glass_claim.c). En app
 * vars larm måste synas även när en annan app står framme (Needs You) gör
 * anspråk på glaset; plattformen tar fram den och minns vad som visades.
 * När larmet är besvarat släpps anspråket och glaset går tillbaka dit det
 * var — utom när personen själv navigerat under tiden: då stannar det där
 * personen valde. Plattformen (torget_ui.c) utför växlingarna; den här filen
 * bestämmer bara vart.
 *
 * Index är appregistrets ordning; TG_GLASS_LAUNCHER är launchern.
 */

#define TG_GLASS_LAUNCHER (-1)
#define TG_GLASS_STAY     (-2) /* ingen växling */

typedef struct {
  bool held;
  int owner;      /* appen som gjort anspråk */
  int return_to;  /* det som visades innan: ett appindex eller launchern */
  bool moved;     /* personen navigerade själv medan anspråket gällde */
} tg_glass_claim;

void tg_glass_claim_init(tg_glass_claim *c);

/* Anspråk från `owner` medan `active` visas. Svarar med vad som ska visas:
 * owner, eller TG_GLASS_STAY (redan framme, redan hållet, eller ogiltigt).
 * Upprepade anspråk behåller det första återgångsmålet. */
int tg_glass_claim_take(tg_glass_claim *c, int owner, int active);

/* Släpp från `owner` medan `active` visas. Svarar med återgångsmålet
 * (appindex eller TG_GLASS_LAUNCHER), eller TG_GLASS_STAY när det inte finns
 * något att återställa eller personen själv navigerat. */
int tg_glass_claim_release(tg_glass_claim *c, int owner, int active);

/* Kallas för VARJE växling på glaset (appvisning eller launchern). En växling
 * till något annat än ägaren medan anspråket gäller är personens egen
 * navigering. */
void tg_glass_claim_note_show(tg_glass_claim *c, int shown);

#endif
