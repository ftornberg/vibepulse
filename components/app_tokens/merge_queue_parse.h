#ifndef MERGE_QUEUE_PARSE_H
#define MERGE_QUEUE_PARSE_H

#include <stdbool.h>
#include <stddef.h>

#include "merge_queue.h"

/* Strict: unknown keys, duplicate keys, a wrong version, a list longer than
 * TK_MQ_LIST_CAP or a count below the list's length all reject the payload,
 * and a rejected payload never touches *out. */
bool tk_merge_queue_parse(const char *json, size_t len, tk_merge_queue *out);

#endif
