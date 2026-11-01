#include <stdio.h>

#include "../components/torget_ota/notice_policy.h"

static int failures;

static void check(const char *what, int condition) {
  if (!condition) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

#define HOUR_US (60LL * 60LL * 1000000LL)

static void test_first_discovery_takes_over_immediately(void) {
  tg_notice_policy policy = {0};
  check("no announcement shows nothing",
        tg_notice_update(&policy, false, false, 1000) == TG_NOTICE_NONE);
  check("first discovery shows at once",
        tg_notice_update(&policy, true, false, 2000) == TG_NOTICE_SHOW);
  check("already showing stays quiet",
        tg_notice_update(&policy, true, false, 3000) == TG_NOTICE_NONE);
}

static void test_dismiss_snoozes_then_nags_again(void) {
  tg_notice_policy policy = {0};
  tg_notice_update(&policy, true, false, 0);
  tg_notice_dismiss(&policy, 1000);
  check("freshly dismissed stays hidden",
        tg_notice_update(&policy, true, false, 2000) == TG_NOTICE_NONE);
  check("still hidden just under the nag interval",
        tg_notice_update(&policy, true, false,
                         1000 + TG_NOTICE_NAG_US - 1) == TG_NOTICE_NONE);
  check("nags again after the interval",
        tg_notice_update(&policy, true, false,
                         1000 + TG_NOTICE_NAG_US) == TG_NOTICE_SHOW);
}

static void test_busy_device_is_never_taken_over(void) {
  tg_notice_policy policy = {0};
  check("busy suppresses even the first discovery",
        tg_notice_update(&policy, true, true, 1000) == TG_NOTICE_NONE);
  tg_notice_update(&policy, true, false, 2000);
  check("turning busy hides a visible takeover",
        tg_notice_update(&policy, true, true, 3000) == TG_NOTICE_HIDE);
  /* Ledig igen med annonsen kvar: första-gången-regeln är förbrukad men
   * dismissed_at är orörd (0), så tjatklockan har redan löpt ut — visa. */
  check("idle again resumes the takeover",
        tg_notice_update(&policy, true, false,
                         TG_NOTICE_NAG_US + 4000) == TG_NOTICE_SHOW);
}

static void test_installed_update_silences_the_notice(void) {
  tg_notice_policy policy = {0};
  tg_notice_update(&policy, true, false, 1000);
  check("announcement gone hides the takeover",
        tg_notice_update(&policy, false, false, 2000) == TG_NOTICE_HIDE);
  check("and stays silent after",
        tg_notice_update(&policy, false, false, 3000) == TG_NOTICE_NONE);
}

static void test_dismiss_without_takeover_is_a_no_op(void) {
  tg_notice_policy policy = {0};
  tg_notice_dismiss(&policy, 1000);
  check("stray dismiss does not start the snooze clock",
        tg_notice_update(&policy, true, false, 2000) == TG_NOTICE_SHOW);
}

/* Glaset följer policyn som NIVÅ, inte som en engångshändelse: ett tryck som
 * avfärdar ger ingen HIDE ur tg_notice_update, och en ritning som hoppades
 * över för ett upptaget UI-lås måste göras om vid nästa poll. */
static void test_the_glass_follows_the_policy(void) {
  tg_notice_policy policy = {0};
  check("nothing to show, nothing shown",
        tg_notice_glass(&policy, false, false, false) == TG_NOTICE_NONE);
  tg_notice_update(&policy, true, false, 0);
  check("a takeover that never reached the glass is drawn again",
        tg_notice_glass(&policy, false, false, false) == TG_NOTICE_SHOW);
  check("a takeover owed while another state lingers is still drawn",
        tg_notice_glass(&policy, false, true, false) == TG_NOTICE_SHOW);
  check("glass and policy agree: leave the pixels alone",
        tg_notice_glass(&policy, true, true, false) == TG_NOTICE_NONE);
  tg_notice_dismiss(&policy, 1000);
  check("a dismissing tap changes nothing in the update itself",
        tg_notice_update(&policy, true, false, 2000) == TG_NOTICE_NONE);
  check("...so the glass must be told to hide (the dead-pill bug)",
        tg_notice_glass(&policy, true, true, false) == TG_NOTICE_HIDE);
  check("a hide that was skipped is asked for again",
        tg_notice_glass(&policy, true, true, false) == TG_NOTICE_HIDE);
  check("hidden and dismissed: quiet",
        tg_notice_glass(&policy, false, false, false) == TG_NOTICE_NONE);
  check("window closed but its ring is still drawn: hide it",
        tg_notice_glass(&policy, false, true, false) == TG_NOTICE_HIDE);
  check("an open window owns the overlay, whatever the glass shows",
        tg_notice_glass(&policy, true, true, true) == TG_NOTICE_NONE &&
        tg_notice_glass(&policy, false, true, true) == TG_NOTICE_NONE);
  check("NULL policy is quiet",
        tg_notice_glass(NULL, true, true, false) == TG_NOTICE_NONE);
}

static void test_only_a_demonstrably_newer_build_is_available(void) {
  check("same build is not an update",
        !tg_notice_version_is_newer("v0.6.0-97-g1b6ba3a",
                                    "v0.6.0-97-g1b6ba3a"));
  check("older commit count is not an update",
        !tg_notice_version_is_newer("v0.6.0-18-g3d88bb3-dirty",
                                    "v0.6.0-97-g1b6ba3a"));
  check("newer commit count is an update",
        tg_notice_version_is_newer("v0.6.0-98-g1234567",
                                   "v0.6.0-97-g1b6ba3a"));
  check("newer release is an update",
        tg_notice_version_is_newer("v0.7.0", "v0.6.0-97-g1b6ba3a"));
  check("older release is not an update",
        !tg_notice_version_is_newer("v0.5.0-999-g1234567", "v0.6.0"));
  check("commit after an exact tag is newer",
        tg_notice_version_is_newer("v0.7.0-1-g1234567", "v0.7.0"));
  check("exact tag is older than a commit after that tag",
        !tg_notice_version_is_newer("v0.7.0", "v0.7.0-1-g1234567"));
  check("malformed advertisement fails closed",
        !tg_notice_version_is_newer("newest", "v0.6.0-97-g1b6ba3a"));
  check("trailing junk fails closed",
        !tg_notice_version_is_newer("v0.7.0-surprise", "v0.6.0"));
  check("overflow fails closed",
        !tg_notice_version_is_newer("v999999999999999999999.0.0",
                                    "v0.6.0"));
}

int main(void) {
  test_first_discovery_takes_over_immediately();
  test_dismiss_snoozes_then_nags_again();
  test_busy_device_is_never_taken_over();
  test_installed_update_silences_the_notice();
  test_dismiss_without_takeover_is_a_no_op();
  test_the_glass_follows_the_policy();
  test_only_a_demonstrably_newer_build_is_available();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("OK: UPDATE READY-notisen tjatar lagom och stör aldrig en pågående uppdatering\n");
  return 0;
}
