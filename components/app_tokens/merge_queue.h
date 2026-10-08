#ifndef MERGE_QUEUE_H
#define MERGE_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

/* One strict /api/merge-queue payload from the tokenserver: pull requests
 * that local agent-team orchestrators have reviewed and that wait for a
 * person to merge them. The tokenserver lists at most eight; `count` covers
 * them all. `incomplete` = at least one orchestrator did not answer, so the
 * list is a lower bound, never a made-up zero. */
#define TK_MQ_LIST_CAP 8
/* The tokenserver's configuration allows sixteen orchestrators. */
#define TK_MQ_SOURCE_CAP 16
#define TK_MQ_PROJECT_CAP 101
/* The card shows one title on one line with an ellipsis, so the panel keeps
 * 127 bytes of it: every title the tokenserver sends in ASCII (it bounds
 * them to 80 characters), and a longer UTF-8 one cut at a character
 * boundary with "…" appended. Sized for RAM, not for the wire: the payload
 * is held three times (parse scratch, poller, monitor). */
#define TK_MQ_TITLE_CAP 128

typedef struct {
  char project[TK_MQ_PROJECT_CAP];
  int32_t number;
  bool has_title;
  char title[TK_MQ_TITLE_CAP];
} tk_mq_pr;

typedef struct {
  bool enabled;
  bool incomplete;
  int32_t count;
  uint8_t pr_count;
  tk_mq_pr prs[TK_MQ_LIST_CAP];
  /* tk_mq_project_key() of every source that answered, so the policy can
   * tell a pull request that left an answering source (merged, closed) from
   * one whose source is down. */
  uint8_t up_count;
  uint32_t up_projects[TK_MQ_SOURCE_CAP];
} tk_merge_queue;

/* FNV-1a of a project name; shared by the parser and the policy. */
uint32_t tk_mq_project_key(const char *project);

#endif
