#include "torget_app.h"

#include "app_tokens.h"

/* Det HÄR repot är VibePulse: en färsk klon utifrån bygger exakt en app, och
 * skärmen startar i den. Solelkollen och Vibbe/Buddy är egna produkter i egna
 * repon och delas in som companion-inputs när de finns utcheckade —
 * byggena sätter TORGET_HAVE_* och grindar registerposterna här. Utomstående
 * ska aldrig få en app de inte bett om.
 * TID (components/app_time) bor i det här repot men är opt-in på samma sätt
 * (TORGET_WITH_TIME, bara 2.16): en färsk klon bygger fortfarande exakt en app. */
#ifdef TORGET_HAVE_SOLELKOLLEN
#include "app_solelkollen.h"
#endif
#ifdef TORGET_HAVE_BUDDY
#include "app_buddy.h"
#endif
#ifdef TORGET_HAVE_TIME
#include "app_time.h"
#endif

/*
 * Appregistret: DEN här byggens appar, i launcherordning. Registret är
 * byggets konfiguration — vill du ha en annan skärm bygger du en annan binär
 * med ett annat register (en skärm = en binär, P25). Filen delas
 * byte-identiskt med simulatorn så bänken alltid visar samma appuppsättning
 * som hyllan.
 *
 * Lägga till en app: skriv en komponent som exporterar en torget_app_t,
 * inkludera dess header här och lägg in pekaren. Launchern gör resten.
 */

const torget_app_t *const torget_apps[] = {
  &tokens_app,
#ifdef TORGET_HAVE_SOLELKOLLEN
  &solelkollen_app,
#endif
#ifdef TORGET_HAVE_BUDDY
  &vibbe_app,
#endif
#ifdef TORGET_HAVE_TIME
  &time_app,
#endif
};

const int torget_app_count =
  (int)(sizeof torget_apps / sizeof torget_apps[0]);
