#ifndef TK_STRICT_JSON_H
#define TK_STRICT_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../third_party/cjson/cJSON.h"

/* The guards every strict LAN-payload parser shares (GitHub status, merge
 * queue). cJSON on its own accepts a decoded NUL, trailing garbage and
 * duplicate keys; these refuse all three. One copy, so a fix lands in every
 * parser at once. */

/* Before parsing: no raw control byte outside whitespace, no control byte in
 * a string, no \u0000 escape, every string closed. cJSON stores a decoded NUL
 * inside a C string without exposing its length, so it would silently
 * truncate an identity. */
bool tk_json_lexical_guard(const char *json, size_t len);

/* After parsing: only whitespace between cJSON's parse end and len. */
bool tk_json_trailing_whitespace(const char *json, size_t len,
                                 const char *parse_end);

/* An object whose keys are all in the NULL-terminated `allowed` list, each at
 * most once. */
bool tk_json_exact_fields(const cJSON *object, const char *const *allowed);

/* A whole, finite number in [0, INT32_MAX]. */
bool tk_json_exact_int32(const cJSON *item, int32_t *out);

/* A non-empty string that fits `cap` including its NUL. */
bool tk_json_copy_string(const cJSON *item, char *out, size_t cap);

#endif
