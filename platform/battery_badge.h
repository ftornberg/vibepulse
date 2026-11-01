#ifndef TORGET_BATTERY_BADGE_H
#define TORGET_BATTERY_BADGE_H

#include <stdbool.h>

#include "../components/torget_power/battery_policy.h"

/*
 * Batteriikonen: nere till höger i sidfoten, på topplagret så den syns på
 * alla appsidor; döljs under SETTINGS, Needs You, klarpulsen och
 * övertagningarna. Vet bara om ett tillstånd och en procent —
 * ingen I2C, ingen policy.
 * Alla funktioner kallas under torget_ui_lock().
 */
void torget_battery_badge_create(void);
void torget_battery_badge_set(tg_batt_state state, int percent);
void torget_battery_badge_set_covered(bool covered);

#endif
