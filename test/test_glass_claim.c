#include <stdio.h>

#include "../platform/glass_claim.h"

static int failures;

static void check(const char *what, int condition) {
  if (!condition) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

#define VP 0      /* the claiming app (VibePulse) */
#define TID 2     /* another app */
#define LAUNCHER TG_GLASS_LAUNCHER

int main(void) {
  tg_glass_claim c;

  /* The core case: TID is showing, an alert claims the glass, the answer
   * releases it and TID comes back. */
  tg_glass_claim_init(&c);
  check("claim from another app switches to the owner",
        tg_glass_claim_take(&c, VP, TID) == VP);
  check("claim is held", c.held);
  check("release returns to the app that was showing",
        tg_glass_claim_release(&c, VP, VP) == TID);
  check("released", !c.held);

  /* From the launcher: the answer returns to the launcher. */
  tg_glass_claim_init(&c);
  check("claim from the launcher switches to the owner",
        tg_glass_claim_take(&c, VP, LAUNCHER) == VP);
  check("release returns to the launcher",
        tg_glass_claim_release(&c, VP, VP) == LAUNCHER);

  /* Owner already on the glass: nothing to switch, nothing to restore. */
  tg_glass_claim_init(&c);
  check("claim while the owner shows stays",
        tg_glass_claim_take(&c, VP, VP) == TG_GLASS_STAY);
  check("release after that stays",
        tg_glass_claim_release(&c, VP, VP) == TG_GLASS_STAY);

  /* The person navigated during the alert: never drag them back. */
  tg_glass_claim_init(&c);
  tg_glass_claim_take(&c, VP, TID);
  tg_glass_claim_note_show(&c, LAUNCHER);
  tg_glass_claim_note_show(&c, TID);
  check("release after the person moved away stays",
        tg_glass_claim_release(&c, VP, TID) == TG_GLASS_STAY);

  /* Even a trip back to the owner counts as their own navigation. */
  tg_glass_claim_init(&c);
  tg_glass_claim_take(&c, VP, TID);
  tg_glass_claim_note_show(&c, LAUNCHER);
  tg_glass_claim_note_show(&c, VP);
  check("release after launcher-and-back stays",
        tg_glass_claim_release(&c, VP, VP) == TG_GLASS_STAY);

  /* The claim's own switch to the owner is not the person navigating. */
  tg_glass_claim_init(&c);
  tg_glass_claim_take(&c, VP, TID);
  tg_glass_claim_note_show(&c, VP);
  check("the claim's own show does not count as navigation",
        tg_glass_claim_release(&c, VP, VP) == TID);

  /* Repeated claims (every render tick) keep the first return target. */
  tg_glass_claim_init(&c);
  tg_glass_claim_take(&c, VP, TID);
  check("a second claim while held stays",
        tg_glass_claim_take(&c, VP, VP) == TG_GLASS_STAY);
  check("...and keeps the original return target",
        tg_glass_claim_release(&c, VP, VP) == TID);

  /* Release without a claim, or twice, does nothing. */
  tg_glass_claim_init(&c);
  check("release without a claim stays",
        tg_glass_claim_release(&c, VP, TID) == TG_GLASS_STAY);
  tg_glass_claim_take(&c, VP, TID);
  tg_glass_claim_release(&c, VP, VP);
  check("a second release stays",
        tg_glass_claim_release(&c, VP, VP) == TG_GLASS_STAY);

  /* Another app cannot release or steal a held claim. */
  tg_glass_claim_init(&c);
  tg_glass_claim_take(&c, VP, TID);
  check("a foreign claim while held is ignored",
        tg_glass_claim_take(&c, TID, VP) == TG_GLASS_STAY);
  check("a foreign release is ignored",
        tg_glass_claim_release(&c, TID, VP) == TG_GLASS_STAY);
  check("still held by the owner", c.held && c.owner == VP);

  /* Navigation notes outside a claim are harmless. */
  tg_glass_claim_init(&c);
  tg_glass_claim_note_show(&c, TID);
  check("a note without a claim changes nothing", !c.held);

  /* Invalid owners never take the glass. */
  tg_glass_claim_init(&c);
  check("negative owner is refused", tg_glass_claim_take(&c, -1, TID) == TG_GLASS_STAY);
  check("refused claim is not held", !c.held);

  /* NULL safety. */
  check("NULL take", tg_glass_claim_take(NULL, VP, TID) == TG_GLASS_STAY);
  check("NULL release", tg_glass_claim_release(NULL, VP, VP) == TG_GLASS_STAY);
  tg_glass_claim_note_show(NULL, VP);

  if (failures) { printf("%d failure(s)\n", failures); return 1; }
  printf("glass claim: ok\n");
  return 0;
}
