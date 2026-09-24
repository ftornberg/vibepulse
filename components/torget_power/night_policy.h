#ifndef TORGET_POWER_NIGHT_POLICY_H
#define TORGET_POWER_NIGHT_POLICY_H

#include <stdbool.h>

/*
 * Nattschemat: ren funktion. Lokal tid in, "natt gäller nu" ut. Fönstret
 * anges som HHMM (2300, 700); start > slut betyder över midnatt. Utan
 * giltig klocka gäller aldrig schemat — bara inaktivitetsregeln i main.c.
 */

typedef struct {
  int start_hhmm;
  int end_hhmm;
} tg_night_schedule;

bool tg_night_applies(const tg_night_schedule *s, bool enabled,
                      bool time_valid, int hour, int minute);

#endif
