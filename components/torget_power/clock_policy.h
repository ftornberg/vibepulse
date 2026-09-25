#ifndef TORGET_POWER_CLOCK_POLICY_H
#define TORGET_POWER_CLOCK_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Klockpolicyn: ren. Avgör om en RTC-avläsning får bli systemtid, räknar
 * civil tid ↔ epok i UTC utan libc (så samma tal gäller på värd och target),
 * och ger ABOUT-radens CLOCK-text. RTC:n lagrar UTC; lokal tid är TZ:s sak.
 *
 * Förtroenderegeln (spec 2026-09-24): oscillatorflaggan ren och året
 * 2026..2099. En färsk krets säger år 2000 och avvisas därmed, precis som
 * en krets vars batteri tagit slut (flaggan satt).
 */

typedef struct {
  int year, month, day, hour, minute, second;
} tg_civil;

bool tg_rtc_reading_trusted(bool os_flag, int year);
int64_t tg_civil_to_epoch(const tg_civil *c);
bool tg_epoch_to_civil(int64_t epoch, tg_civil *out);
int tg_civil_weekday(const tg_civil *c);
const char *tg_clock_text(bool rtc_applied, bool ntp_synced);

#endif
