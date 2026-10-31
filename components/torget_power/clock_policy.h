#ifndef TORGET_POWER_CLOCK_POLICY_H
#define TORGET_POWER_CLOCK_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Klockpolicyn: ren. Avgör om en RTC-avläsning får bli systemtid, räknar
 * civil tid ↔ epok i UTC utan libc (så samma tal gäller på värd och target),
 * och ger ABOUT-radens CLOCK-text. RTC:n lagrar UTC; lokal tid är TZ:s sak.
 *
 * Förtroenderegeln (spec 2026-09-24, skärpt efter PR #7-granskningen):
 * oscillatorflaggan ren, året 2026..2099 OCH avläsningen UTC-märkt — dvs.
 * skriven av den här firmwaren (märket bor i kretsens RAM-byte). En färsk
 * krets säger år 2000 och avvisas, en krets vars batteri tagit slut har
 * flaggan satt, och en krets som fabriksdemon ställt i lokal tid saknar
 * märket: alla tre väntar på SNTP i stället för att dimma vid fel timme.
 */

typedef struct {
  int year, month, day, hour, minute, second;
} tg_civil;

typedef struct {
  tg_civil civil;   /* UTC om utc_marked, annars okänd bas */
  bool os;          /* oscillatorn har stannat sedan senaste skrivningen */
  bool utc_marked;  /* RAM-byten bär vårt märke: tiden skrevs av oss, i UTC */
} tg_rtc_reading;

bool tg_rtc_reading_trusted(const tg_rtc_reading *r);
int64_t tg_civil_to_epoch(const tg_civil *c);
bool tg_epoch_to_civil(int64_t epoch, tg_civil *out);
int tg_civil_weekday(const tg_civil *c);
const char *tg_clock_text(bool rtc_applied, bool ntp_synced);

#endif
