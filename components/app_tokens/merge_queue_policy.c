/* See merge_queue_policy.h. Pure decisions, no LVGL, no clock, no network. */
#include "merge_queue_policy.h"

#include <string.h>

/* FNV-1a over "project#number". A collision would only merge two cards into
 * one dismissal; nothing is ever sent or acted on from this key. */
uint32_t tk_mq_key(const tk_mq_pr *pr) {
  uint32_t hash = tk_mq_project_key(pr->project);
  hash = (hash ^ '#') * 16777619u;
  uint32_t number = (uint32_t)pr->number;
  for (int i = 0; i < 4; i++) {
    hash = (hash ^ (uint8_t)(number >> (8 * i))) * 16777619u;
  }
  return hash;
}

static bool contains(const uint32_t *keys, uint8_t count, uint32_t key) {
  for (uint8_t i = 0; i < count; i++) {
    if (keys[i] == key) return true;
  }
  return false;
}

static void ack(tk_mq_state *state, uint32_t key) {
  if (contains(state->acked, state->acked_count, key)) return;
  if (state->acked_count == TK_MQ_ACK_CAP) {
    /* Oldest out: the list holds at most TK_MQ_LIST_CAP live entries. */
    memmove(state->acked, state->acked + 1,
            (TK_MQ_ACK_CAP - 1) * sizeof state->acked[0]);
    state->acked_count--;
  }
  state->acked[state->acked_count++] = key;
}

void tk_mq_init(tk_mq_state *state) {
  if (!state) return;
  memset(state, 0, sizeof *state);
  state->lead = -1;
}

static void hide(tk_mq_state *state) {
  state->showing = false;
  state->pulse_owed = false;
  state->pulse_until_ms = 0;
  state->lead = -1;
}

static bool source_up(const tk_merge_queue *queue, const char *project) {
  uint32_t key = tk_mq_project_key(project);
  for (uint8_t i = 0; i < queue->up_count; i++) {
    if (queue->up_projects[i] == key) return true;
  }
  return false;
}

bool tk_mq_apply(tk_mq_state *state, tk_merge_queue *queue, uint64_t now_ms) {
  (void)now_ms; /* a pulse is timed from when it is shown, not from here */
  if (!state || !queue) return false;
  if (!queue->enabled) {
    state->queue = *queue;
    state->present_count = 0;
    state->unseen_beyond = false;
    hide(state);
    return false;
  }

  /* Entries from sources that did not answer are kept: an incomplete list
   * says nothing about them. A source that answered without one has
   * dropped it (merged, closed), so that one goes. */
  tk_merge_queue *next = queue;
  const tk_merge_queue *previous = &state->queue;
  if (queue->incomplete && previous->enabled) {
    for (uint8_t i = 0; i < previous->pr_count &&
                        next->pr_count < TK_MQ_LIST_CAP; i++) {
      const tk_mq_pr *old = &previous->prs[i];
      uint32_t key = tk_mq_key(old);
      bool listed = false;
      for (uint8_t j = 0; j < next->pr_count && !listed; j++) {
        listed = tk_mq_key(&next->prs[j]) == key;
      }
      if (listed || source_up(queue, old->project)) continue;
      next->prs[next->pr_count++] = *old;
      next->count++;
    }
  }

  uint32_t keys[TK_MQ_LIST_CAP];
  for (uint8_t i = 0; i < next->pr_count; i++) {
    keys[i] = tk_mq_key(&next->prs[i]);
  }

  /* Only a complete, uncut list is the truth: forget dismissals for pull
   * requests that are gone, so a reopened one counts as new. One pushed past
   * the cut of eight is still there. */
  if (!next->incomplete && next->count == next->pr_count) {
    uint8_t kept = 0;
    for (uint8_t i = 0; i < state->acked_count; i++) {
      if (contains(keys, next->pr_count, state->acked[i])) {
        state->acked[kept++] = state->acked[i];
      }
    }
    state->acked_count = kept;
  }

  bool news = false;
  int8_t lead = -1;
  for (uint8_t i = 0; i < next->pr_count; i++) {
    bool unseen = !contains(state->acked, state->acked_count, keys[i]);
    bool arrived = !contains(state->present, state->present_count, keys[i]);
    if (unseen && lead < 0) lead = (int8_t)i;
    if (unseen && arrived) news = true;
  }

  /* Past the cut only the count speaks: more hidden than before is news the
   * list cannot name. */
  int32_t hidden = next->count - next->pr_count;
  int32_t hidden_before = previous->enabled
                              ? previous->count - previous->pr_count : 0;
  if (hidden > hidden_before) {
    state->unseen_beyond = true;
    news = true;
  } else if (hidden == 0) {
    state->unseen_beyond = false;
  }

  state->queue = *next;
  memcpy(state->present, keys, next->pr_count * sizeof keys[0]);
  state->present_count = next->pr_count;

  if (lead < 0 && !(state->unseen_beyond && next->pr_count > 0)) {
    /* Nothing left the person has not seen: the card goes, also when
     * dismissed pull requests remain. */
    hide(state);
    return false;
  }
  state->showing = true;
  state->lead = lead >= 0 ? lead : 0;
  if (news) state->pulse_owed = true;
  return news;
}

void tk_mq_shown(tk_mq_state *state, uint64_t now_ms) {
  if (!state || !state->showing || !state->pulse_owed) return;
  state->pulse_owed = false;
  state->pulse_until_ms = now_ms + TK_MQ_PULSE_MS;
  state->pulse_epoch++;
}

void tk_mq_dismiss(tk_mq_state *state) {
  if (!state) return;
  for (uint8_t i = 0; i < state->present_count; i++) {
    ack(state, state->present[i]);
  }
  state->unseen_beyond = false;
  hide(state);
}

tk_mq_phase tk_mq_phase_at(const tk_mq_state *state, uint64_t now_ms) {
  if (!state || !state->showing || state->present_count == 0) {
    return TK_MQ_HIDDEN;
  }
  return state->pulse_owed || now_ms < state->pulse_until_ms ? TK_MQ_PULSE
                                                              : TK_MQ_STATIC;
}

const tk_mq_pr *tk_mq_lead(const tk_mq_state *state) {
  if (!state || !state->showing || state->lead < 0 ||
      state->lead >= state->queue.pr_count) {
    return NULL;
  }
  return &state->queue.prs[state->lead];
}

static size_t utf8_length(const unsigned char *s) {
  if (s[0] < 0x80) return 1;
  if ((s[0] & 0xe0) == 0xc0) return 2;
  if ((s[0] & 0xf0) == 0xe0) return 3;
  if ((s[0] & 0xf8) == 0xf0) return 4;
  return 1;
}

void tk_mq_title_label(const char *source, char *destination, size_t cap) {
  if (!destination || cap == 0) return;
  destination[0] = '\0';
  if (!source) return;
  size_t out = 0;
  const unsigned char *c = (const unsigned char *)source;
  while (*c) {
    size_t len = utf8_length(c);
    for (size_t i = 1; i < len; i++) {
      if (c[i] == 0) { len = i; break; } /* truncated sequence: stop here */
    }
    unsigned char glyph[2] = {'?', 0};
    size_t glyph_len = 1;
    if (len == 1) {
      unsigned char b = c[0];
      if (b < 0x20 || b == 0x7f) { c++; continue; }
      if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 32);
      glyph[0] = b < 0x80 ? b : '?';
    } else if (len == 2 && c[0] == 0xC2 && (c[1] == 0xB7 || c[1] == 0xA0)) {
      /* The face has the middle dot; a no-break space is just a space. */
      if (c[1] == 0xB7) { glyph[0] = 0xC2; glyph[1] = 0xB7; glyph_len = 2; }
      else glyph[0] = ' ';
    } else if (len == 3 && c[0] == 0xE2 && c[1] == 0x80 &&
               (c[2] == 0x93 || c[2] == 0x94)) {
      /* En and em dash both as the en dash the face carries (U+2013). */
      if (out + 3 >= cap) break;
      memcpy(destination + out, "\xE2\x80\x93", 3);
      out += 3;
      c += len;
      continue;
    } else if (len == 3 && c[0] == 0xE2 && c[1] == 0x80 &&
               (c[2] == 0x98 || c[2] == 0x99)) {
      glyph[0] = '\'';
    } else if (len == 3 && c[0] == 0xE2 && c[1] == 0x80 &&
               (c[2] == 0x9C || c[2] == 0x9D)) {
      glyph[0] = '"';
    } else if (len == 3 && c[0] == 0xE2 && c[1] == 0x80 && c[2] == 0xA6) {
      if (out + 3 >= cap) break;
      memcpy(destination + out, "...", 3);
      out += 3;
      c += len;
      continue;
    } else if (len == 2 && c[0] == 0xC3 &&
               (c[1] == 0xA4 || c[1] == 0xA5 || c[1] == 0xB6 ||
                c[1] == 0x84 || c[1] == 0x85 || c[1] == 0x96)) {
      glyph[0] = 0xC3;
      glyph[1] = c[1] == 0xA4 ? 0x84 : c[1] == 0xA5 ? 0x85 :
                 c[1] == 0xB6 ? 0x96 : c[1];
      glyph_len = 2;
    }
    if (out + glyph_len >= cap) break;
    memcpy(destination + out, glyph, glyph_len);
    out += glyph_len;
    c += len;
  }
  destination[out] = '\0';
}
