#include "merge_queue_parse.h"

#include <stdint.h>
#include <string.h>

#include "strict_json.h"

/* A title longer than the panel keeps: cut at a UTF-8 character boundary
 * and end it with "…" (U+2026), so the card says it was shortened. */
static bool copy_title(const cJSON *item, char *out, size_t cap) {
  if (!cJSON_IsString(item) || !item->valuestring) return false;
  const char *value = item->valuestring;
  size_t len = strlen(value);
  if (len == 0) return false;
  if (len < cap) {
    memcpy(out, value, len + 1);
    return true;
  }
  static const char ellipsis[] = "\xE2\x80\xA6";
  size_t keep = cap - sizeof ellipsis; /* room for the ellipsis and NUL */
  while (keep > 0 && ((unsigned char)value[keep] & 0xC0) == 0x80) keep--;
  memcpy(out, value, keep);
  memcpy(out + keep, ellipsis, sizeof ellipsis);
  return true;
}

uint32_t tk_mq_project_key(const char *project) {
  uint32_t hash = 2166136261u;
  for (const char *c = project; *c; c++) {
    hash = (hash ^ (uint8_t)*c) * 16777619u;
  }
  return hash;
}

/* A project is a repository basename, exactly as GitHub allows it. */
static bool valid_project(const char *value) {
  for (const char *cursor = value; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) {
      return false;
    }
  }
  return true;
}

static bool parse_pr(const cJSON *item, tk_mq_pr *out) {
  static const char *const allowed[] = {"project", "number", "title", NULL};
  if (!tk_json_exact_fields(item, allowed)) return false;
  const cJSON *project = cJSON_GetObjectItemCaseSensitive(item, "project");
  const cJSON *number = cJSON_GetObjectItemCaseSensitive(item, "number");
  const cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
  if (!tk_json_copy_string(project, out->project, sizeof out->project) ||
      !valid_project(out->project) ||
      !tk_json_exact_int32(number, &out->number) || out->number == 0 || !title) {
    return false;
  }
  if (cJSON_IsNull(title)) return true;
  out->has_title = copy_title(title, out->title, sizeof out->title);
  return out->has_title;
}

static bool parse_source(const cJSON *item, tk_merge_queue *out) {
  static const char *const allowed[] = {"project", "up", "paused", NULL};
  if (!tk_json_exact_fields(item, allowed)) return false;
  const cJSON *project = cJSON_GetObjectItemCaseSensitive(item, "project");
  const cJSON *up = cJSON_GetObjectItemCaseSensitive(item, "up");
  if (!(cJSON_IsNull(project) || cJSON_IsString(project)) ||
      !cJSON_IsBool(up) ||
      !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(item, "paused"))) {
    return false;
  }
  if (cJSON_IsTrue(up)) {
    char name[TK_MQ_PROJECT_CAP];
    /* An answering source always names its project. */
    if (!tk_json_copy_string(project, name, sizeof name) ||
        !valid_project(name)) {
      return false;
    }
    out->up_projects[out->up_count++] = tk_mq_project_key(name);
  }
  return true;
}

bool tk_merge_queue_parse(const char *json, size_t len, tk_merge_queue *out) {
  if (!json || !out || len == 0 || !tk_json_lexical_guard(json, len)) return false;

  const char *parse_end = NULL;
  cJSON *root = cJSON_ParseWithLengthOpts(json, len, &parse_end, false);
  if (!root || !tk_json_trailing_whitespace(json, len, parse_end)) {
    cJSON_Delete(root);
    return false;
  }

  static const char *const allowed[] = {
      "v", "enabled", "count", "incomplete", "sources", "prs", NULL,
  };
  /* Static, not on the stack: the payload is about 2 KB and the poller's
   * stack also carries cJSON's recursion and the HTTP client. One caller
   * (the merge-queue poller task), so no reentrancy to worry about. */
  static tk_merge_queue parsed;
  memset(&parsed, 0, sizeof parsed);
  const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
  const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
  int32_t version_value = 0;
  bool ok = tk_json_exact_fields(root, allowed) &&
            tk_json_exact_int32(version, &version_value) && version_value == 1 &&
            cJSON_IsBool(enabled);

  if (ok && !cJSON_IsTrue(enabled)) {
    /* Disabled is exactly {"v": 1, "enabled": false}. */
    ok = root->child && root->child->next &&
         root->child->next->next == NULL;
  } else if (ok) {
    parsed.enabled = true;
    const cJSON *count = cJSON_GetObjectItemCaseSensitive(root, "count");
    const cJSON *incomplete =
        cJSON_GetObjectItemCaseSensitive(root, "incomplete");
    const cJSON *sources = cJSON_GetObjectItemCaseSensitive(root, "sources");
    const cJSON *prs = cJSON_GetObjectItemCaseSensitive(root, "prs");
    ok = tk_json_exact_int32(count, &parsed.count) && cJSON_IsBool(incomplete) &&
         cJSON_IsArray(sources) && cJSON_IsArray(prs);
    parsed.incomplete = cJSON_IsTrue(incomplete);
    if (ok) {
      uint8_t listed = 0;
      for (const cJSON *source = sources->child; ok && source;
           source = source->next) {
        ok = listed++ < TK_MQ_SOURCE_CAP && parse_source(source, &parsed);
      }
    }
    for (const cJSON *pr = ok ? prs->child : NULL; ok && pr; pr = pr->next) {
      ok = parsed.pr_count < TK_MQ_LIST_CAP &&
           parse_pr(pr, &parsed.prs[parsed.pr_count]);
      if (ok) parsed.pr_count++;
    }
    ok = ok && parsed.count >= parsed.pr_count;
  }

  cJSON_Delete(root);
  if (!ok) return false;
  *out = parsed;
  return true;
}
