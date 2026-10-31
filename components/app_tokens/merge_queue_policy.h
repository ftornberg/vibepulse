#ifndef MERGE_QUEUE_POLICY_H
#define MERGE_QUEUE_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "merge_queue.h"

/* When the "ready to merge" card is up, and when it pulses.
 *
 * Kept out of the LVGL layer like the completion and Needs You policies:
 * pure, host-tested, no clock of its own. A pull request is known by its
 * project and number.
 *
 * The card is up while the queue holds a pull request the person has not
 * dismissed — listed, or (when the list is cut at eight) shown only by a
 * rising count. News (a newly listed unseen pull request, or a rising count
 * past the cut) owes one pulse of TK_MQ_PULSE_MS. The pulse is owed, not
 * timed, until the card is actually on the glass: one that arrives behind
 * Needs You or an agent card pulses when the surface frees, so it can still
 * borrow the glass from TID. Each pulse has its own epoch, so the renderer
 * starts each one exactly once.
 *
 * An incomplete payload (an orchestrator did not answer) proves nothing about
 * what it left out: pull requests from sources that did not answer stay on
 * the card, and they neither vanish nor return as news when the source comes
 * back. Only a complete list that is not cut forgets dismissals. */
#define TK_MQ_PULSE_MS 45000u
/* Dismissed pull requests remembered: twice the list, so dismissals made
 * while sources were down survive their return. */
#define TK_MQ_ACK_CAP 16

typedef enum {
  TK_MQ_HIDDEN,
  TK_MQ_PULSE,
  TK_MQ_STATIC,
} tk_mq_phase;

typedef struct {
  tk_merge_queue queue;  /* what the card shows; kept entries included */
  uint32_t acked[TK_MQ_ACK_CAP];
  uint8_t acked_count;
  uint32_t present[TK_MQ_LIST_CAP]; /* keys of queue.prs */
  uint8_t present_count;
  bool unseen_beyond;    /* the count rose past the list since the last tap */
  bool showing;
  bool pulse_owed;       /* news not yet on the glass */
  uint64_t pulse_until_ms;
  uint32_t pulse_epoch;  /* bumped each time a pulse starts */
  int8_t lead; /* index in queue.prs of the pull request to show, or -1 */
} tk_mq_state;

void tk_mq_init(tk_mq_state *state);

/* Fold in the latest payload. Entries kept from sources that did not answer
 * are appended to *queue in place (it is the caller's scratch, so the panel
 * needs no third copy of a payload). Returns true when it brought news (a
 * pulse is now owed). */
bool tk_mq_apply(tk_mq_state *state, tk_merge_queue *queue, uint64_t now_ms);

/* The card is on the glass now: an owed pulse starts here. */
void tk_mq_shown(tk_mq_state *state, uint64_t now_ms);

/* The person tapped the card away: everything listed now counts as seen. */
void tk_mq_dismiss(tk_mq_state *state);

/* PULSE while a pulse is owed or running, STATIC after, HIDDEN when down. */
tk_mq_phase tk_mq_phase_at(const tk_mq_state *state, uint64_t now_ms);

/* The pull request the card leads with (the first unseen one, else — when
 * only the count says there is news — the first listed), or NULL while the
 * card is hidden. */
const tk_mq_pr *tk_mq_lead(const tk_mq_state *state);

uint32_t tk_mq_key(const tk_mq_pr *pr);

/* A pull request title in the card's detail face (plex_ui_14): ASCII in
 * upper case, å/ä/ö as Å/Ä/Ö, dashes, curly quotes and the ellipsis as what
 * the face carries, anything else as '?', control characters dropped. Never
 * a missing-glyph box on the glass. */
void tk_mq_title_label(const char *source, char *destination, size_t cap);

#endif
