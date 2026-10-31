/* See strict_json.h. */
#include "strict_json.h"

#include <math.h>
#include <string.h>

static bool whitespace(unsigned char byte) {
  return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
}

bool tk_json_lexical_guard(const char *json, size_t len) {
  bool in_string = false;
  bool escaped = false;
  for (size_t i = 0; i < len; i++) {
    unsigned char byte = (unsigned char)json[i];
    if (!in_string) {
      if (byte == '"') in_string = true;
      else if (byte < 0x20 && !whitespace(byte)) return false;
      continue;
    }
    if (escaped) {
      escaped = false;
      if (byte == 'u' && i + 4 < len &&
          json[i + 1] == '0' && json[i + 2] == '0' &&
          json[i + 3] == '0' && json[i + 4] == '0') {
        return false;
      }
    } else if (byte == '\\') {
      escaped = true;
    } else if (byte == '"') {
      in_string = false;
    } else if (byte < 0x20) {
      return false;
    }
  }
  return !in_string && !escaped;
}

bool tk_json_trailing_whitespace(const char *json, size_t len,
                                 const char *parse_end) {
  if (!parse_end || parse_end < json || parse_end > json + len) return false;
  for (const char *cursor = parse_end; cursor < json + len; cursor++) {
    if (!whitespace((unsigned char)*cursor)) return false;
  }
  return true;
}

static bool allowed_name(const char *name, const char *const *allowed) {
  if (!name) return false;
  for (size_t i = 0; allowed[i]; i++) {
    if (strcmp(name, allowed[i]) == 0) return true;
  }
  return false;
}

bool tk_json_exact_fields(const cJSON *object, const char *const *allowed) {
  if (!cJSON_IsObject(object)) return false;
  for (const cJSON *item = object->child; item; item = item->next) {
    if (!allowed_name(item->string, allowed)) return false;
    for (const cJSON *other = item->next; other; other = other->next) {
      if (other->string && strcmp(item->string, other->string) == 0) {
        return false;
      }
    }
  }
  return true;
}

bool tk_json_exact_int32(const cJSON *item, int32_t *out) {
  if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
      item->valuedouble < 0 || item->valuedouble > INT32_MAX ||
      floor(item->valuedouble) != item->valuedouble) {
    return false;
  }
  *out = (int32_t)item->valuedouble;
  return true;
}

bool tk_json_copy_string(const cJSON *item, char *out, size_t cap) {
  if (!cJSON_IsString(item) || !item->valuestring) return false;
  size_t len = strlen(item->valuestring);
  if (len == 0 || len >= cap) return false;
  memcpy(out, item->valuestring, len + 1);
  return true;
}
