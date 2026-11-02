#ifndef TORGET_TIME_SOUND_STORE_H
#define TORGET_TIME_SOUND_STORE_H

#include <stdbool.h>

/* TID:s sparade val: högtalaren och möteskostnadens inställning. Eget NVS-namnområde ("tg_time"), så att inget annat
 * (Wi-Fi, nycklar, LABS) någonsin rörs. Inget sparat = på. Simulatorn håller
 * valet i minnet. */
bool tg_time_sound_load(void);
bool tg_time_sound_save(bool on);

/* Möteskostnadens senaste antal och timkostnad, i samma namnområde. Inget
 * sparat (eller ett läsfel) lämnar *people och *rate orörda, så anroparen
 * förfyller standardvärdena; tg_meeting_init klipper det som läses. Sparas
 * bara när ett möte startar, inte vid varje tryck på en stegare. */
void tg_time_meeting_load(int *people, int *rate);
bool tg_time_meeting_save(int people, int rate);

#endif
