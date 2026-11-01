#ifndef TORGET_OTA_NOTICE_POLICY_H
#define TORGET_OTA_NOTICE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * UPDATE READY-notisen: en full-screen-takeover (NEEDS YOU-mönstret) när
 * ett nyare bygge annonseras än det som kör. Policyn är ren och pollas
 * med (annons finns, upptagen, nu) — ingen LVGL, inget nät — så tjatets
 * tidsregler kan låsas i värdtester.
 *
 * Kontraktet (beslut 2026-08-14):
 *  - Första upptäckten tar över glaset direkt.
 *  - Ett tryck avfärdar, men notisen ÅTERKOMMER efter TG_NOTICE_NAG_US
 *    så länge uppdateringen inte är installerad — lagom tjat, omöjlig
 *    att glömma. En timme är utvecklingstakten (beslut 2026-08-14:
 *    "fixar man massor" ska påminnelsen hinna med); skruva upp den när
 *    plattformen går in i lugnare förvaltning.
 *  - Aldrig takeover medan enheten är upptagen (öppet underhållsfönster
 *    eller pågående överföring) — den som redan uppdaterar ska inte
 *    störas av påminnelsen om samma sak.
 *  - Försvinner själv när annonsen slocknar (versionerna matchar).
 */

#define TG_NOTICE_NAG_US (60LL * 60LL * 1000000LL)

typedef enum {
  TG_NOTICE_NONE,  /* ingen förändring — rör inte overlayn  */
  TG_NOTICE_SHOW,  /* ta över glaset                        */
  TG_NOTICE_HIDE,  /* göm takeovern                         */
} tg_notice_action;

typedef struct {
  bool showing;
  bool ever_shown;
  int64_t dismissed_at_us;
} tg_notice_policy;

/* Git describe-versionerna ar vMAJOR.MINOR.PATCH eller
 * vMAJOR.MINOR.PATCH-N-gHEX, med valfritt -dirty. Returnerar true bara nar
 * advertised ar strikt nyare; okanda/trasiga former faller sakert till false. */
bool tg_notice_version_is_newer(const char *advertised, const char *running);

/* Pollas periodiskt. available = annonserad version är nyare än den
 * körande; busy = fönster öppet eller överföring igång. */
tg_notice_action tg_notice_update(tg_notice_policy *policy,
                                  bool available, bool busy,
                                  int64_t now_us);

/* Användarens tryck på takeovern: göm och starta tjatklockan. */
void tg_notice_dismiss(tg_notice_policy *policy, int64_t now_us);

/* Vad GLASET ska göra för att stämma med policyn, frågat varje poll. Nivå,
 * inte händelse: tg_notice_dismiss ger ingen HIDE ur tg_notice_update, och
 * overlayn hoppar över en ritning när UI-låset är upptaget, så en engångs-
 * händelse kan gå förlorad. Då låg UPDATE READY kvar med döda pillar och
 * avstängt KEY3-håll tills panelen startades om (2026-10-07).
 *  - busy: det öppna fönstret äger overlayn och ritar sitt eget läge.
 *  - glass_shows_anything: overlayn är uppe i NÅGOT läge. Utan notis och
 *    utan fönster ska den bort, också en nedräkningsring vars gömning
 *    hoppades över när fönstret stängdes. */
tg_notice_action tg_notice_glass(const tg_notice_policy *policy,
                                 bool glass_shows_notice,
                                 bool glass_shows_anything, bool busy);

#endif
