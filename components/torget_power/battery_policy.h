#ifndef TORGET_POWER_BATTERY_POLICY_H
#define TORGET_POWER_BATTERY_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Batteripolicyn: ren, pollad var femte sekund med PMU:ns mätning.
 * Ingen I2C, ingen LVGL, ingen tid utom den som skickas in — så varje rad
 * i tillståndstabellen (spec 2026-09-24) låses i test/test_battery_policy.c.
 *
 *  UNKNOWN     inget batteri, eller tre ogiltiga mätningar i rad
 *  CHARGING    USB in, ström in i cellen
 *  FULL        USB in, laddning klar
 *  ON_BATTERY  USB borta, > 20 %
 *  LOW         USB borta, <= 20 %  (lämnas uppåt först vid >= 23 %)
 *  CRITICAL    USB borta, <= 5 % i 30 s utan avbrott (lämnas vid >= 8 %)
 *
 * Procenten är PMU:ns bränslemätare avrundad, aldrig något vi räknar fram;
 * -1 betyder "rita ingen siffra". shutdown sätts EN gång, bara i CRITICAL
 * efter hela fördröjningen och bara när värden armerat den.
 * Varje mätning över 5 %, varje USB-anslutning och varje ogiltig mätning
 * stoppar klockan; nästa mätning på 5 % eller lägre startar om den.
 * En giltig mätning utan procent (mätaren -1) utan USB håller LOW/CRITICAL
 * kvar: den stoppar klockan men friskförklarar inget.
 */

typedef enum {
  TG_BATT_UNKNOWN,
  TG_BATT_CHARGING,
  TG_BATT_FULL,
  TG_BATT_ON_BATTERY,
  TG_BATT_LOW,
  TG_BATT_CRITICAL,
} tg_batt_state;

typedef struct {
  bool valid;
  bool present;
  bool vbus;
  bool charging;
  bool charge_done;
  int percent; /* 0..100 eller -1 */
  int mv;      /* eller -1 */
} tg_batt_sample;

typedef struct {
  tg_batt_state state;
  int failures;
  int64_t critical_since_us;
  bool shutdown_armed;
  bool shutdown_sent;
  bool dwell_running;
  int last_percent;
} tg_batt_policy;

typedef struct {
  tg_batt_state state;
  int percent;
  int bright_cap;
  bool shutdown;
  bool changed;
} tg_batt_verdict;

#define TG_BATT_LOW_PCT 20
#define TG_BATT_LOW_EXIT_PCT 23
#define TG_BATT_CRITICAL_PCT 5
#define TG_BATT_CRITICAL_EXIT_PCT 8
#define TG_BATT_CRITICAL_DWELL_US (30LL * 1000000LL)
#define TG_BATT_FAILURES_TO_UNKNOWN 3
#define TG_BATT_NIGHT_CAP 20
/* En litiumcell som mäter minst så här är inte tom: en mätare som ändå
 * säger 0 % är okonfigurerad eller avstängd, inte en tom cell. */
#define TG_BATT_GAUGE_ZERO_IMPLAUSIBLE_MV 3500

/* Rimlighetskontroll av PMU:ns procentbyte (PR #5-granskningen, punkt 2):
 * ett värde över 100 är skräp, och 0 % med en cell på >= 3,5 V är en
 * mätare som inte mäter — utan den regeln gick en okonfigurerad mätare
 * till LOW och sedan CRITICAL på 30 s, och med del C till avstängning på
 * fullt batteri. Utan spänningsavläsning (mv < 0) tas bytet som det är.
 * -1 betyder "rita ingen siffra", som resten av policyn redan hanterar. */
int tg_batt_gauge_percent(int raw_percent, int mv);

tg_batt_verdict tg_batt_update(tg_batt_policy *p, const tg_batt_sample *s,
                               int64_t now_us);
const char *tg_batt_state_name(tg_batt_state s);

/* ABOUT-radens POWER-text ur samma mätning och beslut som ikonen. Tom text
 * (menyn ritar streck) när PMU:n inte går att läsa; NO BATTERY bara när den
 * faktiskt svarat att inget batteri sitter i. Spänningen visas bara på
 * cellen, med två decimaler avhuggna (4005 mV -> 4.00 V). */
void tg_batt_power_text(const tg_batt_sample *s, const tg_batt_verdict *v,
                        char *out, size_t cap);

#endif
