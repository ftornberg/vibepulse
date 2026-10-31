#include "glass_claim.h"

#include <stddef.h>

void tg_glass_claim_init(tg_glass_claim *c) {
  if (!c) return;
  c->held = false;
  c->owner = TG_GLASS_STAY;
  c->return_to = TG_GLASS_STAY;
  c->moved = false;
}

int tg_glass_claim_take(tg_glass_claim *c, int owner, int active) {
  if (!c || owner < 0 || c->held) return TG_GLASS_STAY;
  c->held = true;
  c->owner = owner;
  c->return_to = active;
  c->moved = false;
  return active == owner ? TG_GLASS_STAY : owner;
}

int tg_glass_claim_release(tg_glass_claim *c, int owner, int active) {
  if (!c || !c->held || owner != c->owner) return TG_GLASS_STAY;
  int target = c->return_to;
  bool moved = c->moved;
  tg_glass_claim_init(c);
  /* Personen valde själv, eller ägaren står inte ens framme längre: rör
   * ingenting. Stod ägaren redan framme från början finns inget att återställa. */
  if (moved || active != owner || target == owner) return TG_GLASS_STAY;
  return target;
}

void tg_glass_claim_note_show(tg_glass_claim *c, int shown) {
  if (!c || !c->held) return;
  if (shown != c->owner) c->moved = true;
}
