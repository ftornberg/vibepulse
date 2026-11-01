#ifndef TORGET_TIME_SOUND_STORE_H
#define TORGET_TIME_SOUND_STORE_H

#include <stdbool.h>

/* TID:s högtalarval. Eget NVS-namnområde ("tg_time"), så att inget annat
 * (Wi-Fi, nycklar, LABS) någonsin rörs. Inget sparat = på. Simulatorn håller
 * valet i minnet. */
bool tg_time_sound_load(void);
bool tg_time_sound_save(bool on);

#endif
