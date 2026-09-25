#ifndef TORGET_TIME_VIEWS_H
#define TORGET_TIME_VIEWS_H

#include "lvgl.h"

#include "time_present.h"

/*
 * TIDs LVGL-lager. Bara objekt och rendering: vilken text som står var
 * bestäms av time_present (hosttestad), vilket läge som gäller av
 * app_time.c. Alla objekt skapas EN gång i create(); render() sätter bara
 * text, färg och synlighet — och hoppar helt över en omritning när
 * vymodellen är byte-identisk med den senast visade.
 */

typedef struct {
  void (*tap)(void);        /* tryck på bakgrunden eller KLAR-lagret */
  void (*reset)(void);      /* RESET-knappen */
  void (*preset)(int idx);  /* ett av 20/40/50 */
} tg_time_view_actions;

/* Kallas under UI-låset med appens 480 x 480-root. */
void time_views_create(lv_obj_t *root, const tg_time_view_actions *actions);
/* Kallas under UI-låset. */
void time_views_render(const tg_time_view_model *m);

#endif
