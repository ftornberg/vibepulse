#include "notice_policy.h"

#include <stdint.h>
#include <string.h>

typedef struct {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
  uint32_t distance;
} tg_git_version;

static bool parse_u32(const char **cursor, uint32_t *out) {
  const char *p = *cursor;
  if (*p < '0' || *p > '9') return false;
  uint32_t value = 0;
  do {
    uint32_t digit = (uint32_t)(*p - '0');
    if (value > (UINT32_MAX - digit) / 10U) return false;
    value = value * 10U + digit;
    p++;
  } while (*p >= '0' && *p <= '9');
  *cursor = p;
  *out = value;
  return true;
}

static bool parse_git_version(const char *text, tg_git_version *out) {
  if (!text || !out || *text++ != 'v') return false;
  tg_git_version parsed = {0};
  if (!parse_u32(&text, &parsed.major) || *text++ != '.' ||
      !parse_u32(&text, &parsed.minor) || *text++ != '.' ||
      !parse_u32(&text, &parsed.patch))
    return false;

  if (*text == '\0') {
    *out = parsed;
    return true;
  }
  if (strcmp(text, "-dirty") == 0) {
    *out = parsed;
    return true;
  }
  if (*text++ != '-' || !parse_u32(&text, &parsed.distance) ||
      text[0] != '-' || text[1] != 'g')
    return false;
  text += 2;
  size_t hex_digits = 0;
  while ((*text >= '0' && *text <= '9') ||
         (*text >= 'a' && *text <= 'f') ||
         (*text >= 'A' && *text <= 'F')) {
    text++;
    hex_digits++;
  }
  if (hex_digits < 7 || hex_digits > 40) return false;
  if (*text != '\0' && strcmp(text, "-dirty") != 0) return false;
  *out = parsed;
  return true;
}

bool tg_notice_version_is_newer(const char *advertised, const char *running) {
  tg_git_version incoming;
  tg_git_version current;
  if (!parse_git_version(advertised, &incoming) ||
      !parse_git_version(running, &current))
    return false;

  if (incoming.major != current.major)
    return incoming.major > current.major;
  if (incoming.minor != current.minor)
    return incoming.minor > current.minor;
  if (incoming.patch != current.patch)
    return incoming.patch > current.patch;
  return incoming.distance > current.distance;
}

/* Tidsreglerna bor här och ingen annanstans — se headern för kontraktet. */

tg_notice_action tg_notice_update(tg_notice_policy *policy,
                                  bool available, bool busy,
                                  int64_t now_us) {
  if (!policy) return TG_NOTICE_NONE;

  if (!available || busy) {
    /* Slocknad annons eller upptagen enhet: en synlig takeover göms.
     * Avfärdandeklockan lämnas orörd — blir enheten ledig igen med
     * annonsen kvar gäller samma tjatrytm som innan. */
    if (policy->showing) {
      policy->showing = false;
      return TG_NOTICE_HIDE;
    }
    return TG_NOTICE_NONE;
  }

  if (policy->showing) return TG_NOTICE_NONE;

  /* Första upptäckten tar över direkt; därefter styr tjatklockan. */
  if (!policy->ever_shown ||
      now_us - policy->dismissed_at_us >= TG_NOTICE_NAG_US) {
    policy->showing = true;
    policy->ever_shown = true;
    return TG_NOTICE_SHOW;
  }
  return TG_NOTICE_NONE;
}

tg_notice_action tg_notice_glass(const tg_notice_policy *policy,
                                 bool glass_shows_notice,
                                 bool glass_shows_anything, bool busy) {
  if (!policy || busy) return TG_NOTICE_NONE;
  if (policy->showing)
    return glass_shows_notice ? TG_NOTICE_NONE : TG_NOTICE_SHOW;
  return glass_shows_notice || glass_shows_anything ? TG_NOTICE_HIDE
                                                    : TG_NOTICE_NONE;
}

void tg_notice_dismiss(tg_notice_policy *policy, int64_t now_us) {
  if (!policy || !policy->showing) return;
  policy->showing = false;
  policy->dismissed_at_us = now_us;
}
