#include "github_status_parse.h"

#include <stdint.h>
#include <string.h>

#include "strict_json.h"

static bool repo_char(unsigned char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
}

static bool valid_repo(const char *value) {
  const char *slash = strchr(value, '/');
  if (!slash || slash == value || slash[1] == '\0' ||
      strchr(slash + 1, '/')) return false;
  for (const char *cursor = value; *cursor; cursor++) {
    if (*cursor != '/' && !repo_char((unsigned char)*cursor)) return false;
  }
  return true;
}

static bool valid_project(const char *value) {
  for (const char *cursor = value; *cursor; cursor++) {
    if (!repo_char((unsigned char)*cursor)) return false;
  }
  return true;
}

static bool valid_actor(const char *value) {
  size_t len = strlen(value);
  if (len == 0 || len > 39 || value[0] == '-' || value[len - 1] == '-') {
    return false;
  }
  for (const char *cursor = value; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '-')) return false;
  }
  return true;
}

static bool valid_event_id(const char *value) {
  for (const char *cursor = value; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (ch < 0x21 || ch > 0x7e) return false;
  }
  return true;
}

bool tk_github_status_parse(const char *json, size_t len,
                            tk_github_status *out) {
  if (!json || !out || len == 0 || !tk_json_lexical_guard(json, len)) return false;

  const char *parse_end = NULL;
  cJSON *root = cJSON_ParseWithLengthOpts(json, len, &parse_end, false);
  if (!root || !tk_json_trailing_whitespace(json, len, parse_end)) {
    cJSON_Delete(root);
    return false;
  }

  static const char *const allowed[] = {
      "v", "enabled", "repo", "project", "stars", "forks", "stale",
      "eventId", "actor", "eventStars", NULL,
  };
  tk_github_status parsed = {0};
  cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
  cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
  int32_t version_value = 0;
  bool ok = tk_json_exact_fields(root, allowed) &&
            tk_json_exact_int32(version, &version_value) && version_value == 1 &&
            cJSON_IsBool(enabled);

  if (ok && !cJSON_IsTrue(enabled)) {
    ok = root->child && root->child->next &&
         root->child->next->next == NULL;
  } else if (ok) {
    parsed.enabled = true;
    cJSON *repo = cJSON_GetObjectItemCaseSensitive(root, "repo");
    cJSON *project = cJSON_GetObjectItemCaseSensitive(root, "project");
    cJSON *stale = cJSON_GetObjectItemCaseSensitive(root, "stale");
    ok = tk_json_copy_string(repo, parsed.repo, sizeof parsed.repo) &&
         valid_repo(parsed.repo) &&
         tk_json_copy_string(project, parsed.project, sizeof parsed.project) &&
         valid_project(parsed.project) && cJSON_IsBool(stale);
    parsed.stale = cJSON_IsTrue(stale);

    cJSON *stars = cJSON_GetObjectItemCaseSensitive(root, "stars");
    cJSON *forks = cJSON_GetObjectItemCaseSensitive(root, "forks");
    if (ok && (stars || forks)) {
      ok = stars && forks &&
           tk_json_exact_int32(stars, &parsed.stars) &&
           tk_json_exact_int32(forks, &parsed.forks);
      parsed.has_data = ok;
    }

    cJSON *event_id = cJSON_GetObjectItemCaseSensitive(root, "eventId");
    cJSON *event_stars =
        cJSON_GetObjectItemCaseSensitive(root, "eventStars");
    cJSON *actor = cJSON_GetObjectItemCaseSensitive(root, "actor");
    if (ok && (event_id || event_stars || actor)) {
      ok = parsed.has_data && event_id && event_stars && actor &&
           tk_json_copy_string(event_id, parsed.event_id, sizeof parsed.event_id) &&
           valid_event_id(parsed.event_id) &&
           tk_json_exact_int32(event_stars, &parsed.event_stars) &&
           parsed.event_stars == parsed.stars &&
           (cJSON_IsNull(actor) ||
            (tk_json_copy_string(actor, parsed.actor, sizeof parsed.actor) &&
             valid_actor(parsed.actor)));
      parsed.has_actor = ok && cJSON_IsString(actor);
      parsed.has_event = ok;
    }
  }

  cJSON_Delete(root);
  if (!ok) return false;
  *out = parsed;
  return true;
}

