#include <stdio.h>
#include <string.h>

#include "../components/app_tokens/merge_queue_parse.h"
#include "../components/app_tokens/merge_queue_policy.h"

static int failures;

static void check(const char *what, int condition) {
  if (!condition) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

static void rejected_unchanged(const char *what, const char *json) {
  tk_merge_queue value;
  memset(&value, 0xa5, sizeof value);
  tk_merge_queue before = value;
  if (tk_merge_queue_parse(json, strlen(json), &value)) {
    printf("FAIL %s accepted\n", what);
    failures++;
  }
  check(what, memcmp(&value, &before, sizeof value) == 0);
}

#define SOURCES \
  "\"sources\":[{\"project\":\"kvitt\",\"up\":true,\"paused\":false}," \
  "{\"project\":null,\"up\":false,\"paused\":false}]"

#define VALID \
  "{\"v\":1,\"enabled\":true,\"count\":3,\"incomplete\":false," SOURCES "," \
  "\"prs\":[{\"project\":\"kvitt\",\"number\":134,\"title\":\"feat: x\"}," \
  "{\"project\":\"gaffel\",\"number\":7,\"title\":null}]}"

static tk_merge_queue queue_of(int count, const char *const *projects,
                               const int *numbers, int listed,
                               int incomplete) {
  tk_merge_queue q;
  memset(&q, 0, sizeof q);
  q.enabled = true;
  q.incomplete = incomplete;
  q.count = count;
  q.pr_count = (uint8_t)listed;
  for (int i = 0; i < listed; i++) {
    snprintf(q.prs[i].project, sizeof q.prs[i].project, "%s", projects[i]);
    q.prs[i].number = numbers[i];
    /* Every listed project answered (it listed something). */
    q.up_projects[q.up_count++] = tk_mq_project_key(projects[i]);
  }
  return q;
}

static void source_up(tk_merge_queue *q, const char *project) {
  q->up_projects[q->up_count++] = tk_mq_project_key(project);
}

/* tk_mq_apply may extend the queue in place: always hand it a fresh copy. */
static bool apply(tk_mq_state *s, const tk_merge_queue *q, uint64_t now) {
  tk_merge_queue copy = *q;
  return tk_mq_apply(s, &copy, now);
}

/* Show the card at `now` the way the renderer does when it is visible. */
static void shown(tk_mq_state *s, uint64_t now) { tk_mq_shown(s, now); }

static void test_parse(void) {
  tk_merge_queue value;
  memset(&value, 0, sizeof value);
  check("valid parses", tk_merge_queue_parse(VALID, strlen(VALID), &value));
  check("valid values",
        value.enabled && !value.incomplete && value.count == 3 &&
        value.pr_count == 2 &&
        strcmp(value.prs[0].project, "kvitt") == 0 &&
        value.prs[0].number == 134 && value.prs[0].has_title &&
        strcmp(value.prs[0].title, "feat: x") == 0 &&
        strcmp(value.prs[1].project, "gaffel") == 0 &&
        !value.prs[1].has_title);

  const char *off = "{\"v\":1,\"enabled\":false}";
  memset(&value, 0xa5, sizeof value);
  check("disabled parses", tk_merge_queue_parse(off, strlen(off), &value));
  check("disabled is empty", !value.enabled && value.pr_count == 0);

  const char *empty =
      "{\"v\":1,\"enabled\":true,\"count\":0,\"incomplete\":true,"
      "\"sources\":[],\"prs\":[]}";
  check("empty incomplete parses",
        tk_merge_queue_parse(empty, strlen(empty), &value) &&
        value.enabled && value.incomplete && value.pr_count == 0);

  rejected_unchanged("wrong version",
      "{\"v\":2,\"enabled\":false}");
  rejected_unchanged("disabled with extra key",
      "{\"v\":1,\"enabled\":false,\"count\":0}");
  rejected_unchanged("unknown key",
      "{\"v\":1,\"enabled\":true,\"count\":0,\"incomplete\":false,"
      "\"sources\":[],\"prs\":[],\"extra\":1}");
  rejected_unchanged("duplicate key",
      "{\"v\":1,\"v\":1,\"enabled\":false}");
  rejected_unchanged("count below the list",
      "{\"v\":1,\"enabled\":true,\"count\":0,\"incomplete\":false,"
      "\"sources\":[],\"prs\":[{\"project\":\"k\",\"number\":1,"
      "\"title\":null}]}");
  rejected_unchanged("number zero",
      "{\"v\":1,\"enabled\":true,\"count\":1,\"incomplete\":false,"
      "\"sources\":[],\"prs\":[{\"project\":\"k\",\"number\":0,"
      "\"title\":null}]}");
  rejected_unchanged("number as string",
      "{\"v\":1,\"enabled\":true,\"count\":1,\"incomplete\":false,"
      "\"sources\":[],\"prs\":[{\"project\":\"k\",\"number\":\"1\","
      "\"title\":null}]}");
  rejected_unchanged("project with a slash",
      "{\"v\":1,\"enabled\":true,\"count\":1,\"incomplete\":false,"
      "\"sources\":[],\"prs\":[{\"project\":\"o/k\",\"number\":1,"
      "\"title\":null}]}");
  rejected_unchanged("missing title key",
      "{\"v\":1,\"enabled\":true,\"count\":1,\"incomplete\":false,"
      "\"sources\":[],\"prs\":[{\"project\":\"k\",\"number\":1}]}");
  rejected_unchanged("empty title string",
      "{\"v\":1,\"enabled\":true,\"count\":1,\"incomplete\":false,"
      "\"sources\":[],\"prs\":[{\"project\":\"k\",\"number\":1,"
      "\"title\":\"\"}]}");
  rejected_unchanged("source with unknown key",
      "{\"v\":1,\"enabled\":true,\"count\":0,\"incomplete\":false,"
      "\"sources\":[{\"project\":\"k\",\"up\":true,\"paused\":false,"
      "\"x\":1}],\"prs\":[]}");
  rejected_unchanged("trailing garbage", VALID "x");
  rejected_unchanged("decoded NUL",
      "{\"v\":1,\"enabled\":true,\"count\":1,\"incomplete\":false,"
      "\"sources\":[],\"prs\":[{\"project\":\"k\",\"number\":1,"
      "\"title\":\"a\\u0000b\"}]}");

  char nine[2048];
  int at = snprintf(nine, sizeof nine,
      "{\"v\":1,\"enabled\":true,\"count\":9,\"incomplete\":false,"
      "\"sources\":[],\"prs\":[");
  for (int i = 1; i <= 9; i++) {
    at += snprintf(nine + at, sizeof nine - (size_t)at,
                   "%s{\"project\":\"k\",\"number\":%d,\"title\":null}",
                   i > 1 ? "," : "", i);
  }
  snprintf(nine + at, sizeof nine - (size_t)at, "]}");
  rejected_unchanged("list over capacity", nine);
}

static void test_policy(void) {
  static const char *const p2[] = {"kvitt", "gaffel"};
  static const int n2[] = {134, 7};
  tk_mq_state s;
  tk_mq_init(&s);
  check("starts hidden", tk_mq_phase_at(&s, 0) == TK_MQ_HIDDEN &&
                         tk_mq_lead(&s) == NULL);

  tk_merge_queue q = queue_of(1, p2, n2, 1, 0);
  check("first PR is news", apply(&s, &q, 1000));
  check("pulse owed before it is shown",
        tk_mq_phase_at(&s, 999999) == TK_MQ_PULSE);
  uint32_t epoch = s.pulse_epoch;
  shown(&s, 2000);
  check("shown starts one pulse", s.pulse_epoch == epoch + 1);
  shown(&s, 3000);
  check("showing again starts no second pulse", s.pulse_epoch == epoch + 1);
  check("pulse lasts the lease from when shown",
        tk_mq_phase_at(&s, 2000 + TK_MQ_PULSE_MS - 1) == TK_MQ_PULSE &&
        tk_mq_phase_at(&s, 2000 + TK_MQ_PULSE_MS) == TK_MQ_STATIC);
  check("lead is the PR", tk_mq_lead(&s) && tk_mq_lead(&s)->number == 134);

  check("same list again is no news",
        !apply(&s, &q, 50000) &&
        tk_mq_phase_at(&s, 50000) == TK_MQ_STATIC);

  tk_merge_queue two = queue_of(2, p2, n2, 2, 0);
  check("a new PR is news", apply(&s, &two, 60000) &&
                            tk_mq_phase_at(&s, 60000) == TK_MQ_PULSE);
  shown(&s, 60000);

  tk_mq_dismiss(&s);
  check("dismiss hides", tk_mq_phase_at(&s, 60001) == TK_MQ_HIDDEN &&
                         tk_mq_lead(&s) == NULL);
  check("dismissed list stays down", !apply(&s, &two, 70000) &&
                                     tk_mq_phase_at(&s, 70000) ==
                                         TK_MQ_HIDDEN);

  /* A complete list without gaffel #7: merged. Reopened later = news. */
  tk_merge_queue merged = queue_of(1, p2, n2, 1, 0);
  check("merged PR forgotten", !apply(&s, &merged, 100000));
  check("reopened PR is news", apply(&s, &two, 110000));
  check("lead is the unseen one",
        tk_mq_lead(&s) && tk_mq_lead(&s)->number == 7);

  tk_merge_queue none = queue_of(0, p2, n2, 0, 0);
  check("empty queue hides", !apply(&s, &none, 120000) &&
                             tk_mq_phase_at(&s, 120000) == TK_MQ_HIDDEN);

  tk_merge_queue off;
  memset(&off, 0, sizeof off);
  tk_mq_init(&s);
  check("disabled never shows", !apply(&s, &off, 1) &&
                                tk_mq_phase_at(&s, 1) == TK_MQ_HIDDEN);

  tk_mq_pr a = {.number = 1}, b = {.number = 1};
  snprintf(a.project, sizeof a.project, "kvitt");
  snprintf(b.project, sizeof b.project, "gaffel");
  check("key separates projects", tk_mq_key(&a) != tk_mq_key(&b));
}

/* Review finding 2: dismiss #134, #17 arrives and merges untapped, the list
 * is back to only #134. The card must not come back for #134. */
static void test_dismissed_does_not_return(void) {
  static const char *const p[] = {"kvitt", "kvitt"};
  static const int n[] = {134, 17};
  tk_mq_state s;
  tk_mq_init(&s);
  tk_merge_queue first = queue_of(1, p, n, 1, 0);
  apply(&s, &first, 0);
  shown(&s, 0);
  tk_mq_dismiss(&s);
  tk_merge_queue both = queue_of(2, p, n, 2, 0);
  check("#17 is news", apply(&s, &both, 10));
  check("leads with #17", tk_mq_lead(&s) && tk_mq_lead(&s)->number == 17);
  shown(&s, 10);
  check("#17 merged: card goes",
        !apply(&s, &first, 20) &&
        tk_mq_phase_at(&s, 20) == TK_MQ_HIDDEN && tk_mq_lead(&s) == NULL);
}

/* Review finding 4: an orchestrator restart (incomplete, empty list) must
 * neither hide an undismissed card nor replay it as news on return. */
static void test_incomplete_keeps_and_does_not_replay(void) {
  static const char *const p[] = {"gaffel", "kvitt"};
  static const int n[] = {7, 134};
  tk_mq_state s;
  tk_mq_init(&s);
  tk_merge_queue one = queue_of(1, p, n, 1, 0);
  apply(&s, &one, 0);
  shown(&s, 0);

  tk_merge_queue down = queue_of(0, p, n, 0, 1); /* gaffel did not answer */
  check("source down is no news", !apply(&s, &down, 60000));
  check("card stays with the kept PR",
        tk_mq_phase_at(&s, 60000) == TK_MQ_STATIC && tk_mq_lead(&s) &&
        tk_mq_lead(&s)->number == 7);
  check("kept PR still counted", s.queue.count == 1);
  check("source back is no news",
        !apply(&s, &one, 120000) &&
        tk_mq_phase_at(&s, 120000) == TK_MQ_STATIC);

  /* A source that answered without the PR has dropped it: not kept. */
  tk_merge_queue answered = queue_of(0, p, n, 0, 1);
  source_up(&answered, "gaffel");
  check("answering source drops it", !apply(&s, &answered, 130000) &&
                                     tk_mq_phase_at(&s, 130000) ==
                                         TK_MQ_HIDDEN);

  /* A dismissal made while another source is down survives its return. */
  tk_mq_init(&s);
  apply(&s, &one, 0);
  shown(&s, 0);
  tk_mq_dismiss(&s);
  apply(&s, &down, 10);
  check("dismissed stays down after the source returns",
        !apply(&s, &one, 20) &&
        tk_mq_phase_at(&s, 20) == TK_MQ_HIDDEN);
}

/* Review finding 5: a list cut at eight is not complete. */
static void test_truncated_list(void) {
  static const char *const p[] = {"k", "k", "k", "k", "k", "k", "k", "k"};
  static const int first8[] = {1, 2, 3, 4, 5, 6, 7, 8};
  static const int shifted[] = {1, 2, 3, 4, 5, 6, 7, 9};
  tk_mq_state s;
  tk_mq_init(&s);
  tk_merge_queue eight = queue_of(8, p, first8, 8, 0);
  apply(&s, &eight, 0);
  shown(&s, 0);
  tk_mq_dismiss(&s);

  /* A ninth PR exists only past the cut: the count alone is news. */
  tk_merge_queue nine = queue_of(9, p, first8, 8, 0);
  check("rising count past the cut is news", apply(&s, &nine, 10));
  check("card up with the first listed",
        tk_mq_phase_at(&s, 10) == TK_MQ_PULSE && tk_mq_lead(&s) &&
        tk_mq_lead(&s)->number == 1);
  shown(&s, 10);
  tk_mq_dismiss(&s);

  /* #8 pushed past the cut by #9 sorting in: its dismissal survives, and #9
   * (never listed, never dismissed) is the news when it slides in. */
  tk_merge_queue cut = queue_of(9, p, shifted, 8, 0);
  check("#9 sliding in is news", apply(&s, &cut, 20) &&
                                 tk_mq_lead(&s)->number == 9);
  shown(&s, 20);
  tk_mq_dismiss(&s);
  check("#8 back from past the cut is no news",
        !apply(&s, &nine, 30) &&
        tk_mq_phase_at(&s, 30) == TK_MQ_HIDDEN);
}

/* Review finding 6: news behind Needs You keeps its pulse until shown. */
static void test_pulse_waits_for_the_glass(void) {
  static const char *const p[] = {"kvitt"};
  static const int n[] = {134};
  tk_mq_state s;
  tk_mq_init(&s);
  tk_merge_queue q = queue_of(1, p, n, 1, 0);
  apply(&s, &q, 0);
  /* Not shown for two minutes (the surface was busy). */
  check("still owed a minute later",
        tk_mq_phase_at(&s, 120000) == TK_MQ_PULSE);
  shown(&s, 120000);
  check("full pulse once shown",
        tk_mq_phase_at(&s, 120000 + TK_MQ_PULSE_MS - 1) == TK_MQ_PULSE &&
        tk_mq_phase_at(&s, 120000 + TK_MQ_PULSE_MS) == TK_MQ_STATIC);
}

static void test_long_title_is_cut_on_a_boundary(void) {
  char json[1024];
  char title[400];
  size_t at = 0;
  for (int i = 0; i < 60; i++) { /* 120 bytes of 'ö' */
    title[at++] = (char)0xc3;
    title[at++] = (char)0xb6;
  }
  for (int i = 0; i < 10; i++) title[at++] = 'x';
  title[at] = '\0';
  snprintf(json, sizeof json,
           "{\"v\":1,\"enabled\":true,\"count\":1,\"incomplete\":false,"
           "\"sources\":[{\"project\":\"k\",\"up\":true,\"paused\":false}],"
           "\"prs\":[{\"project\":\"k\",\"number\":1,\"title\":\"%s\"}]}",
           title);
  tk_merge_queue value;
  check("long title parses", tk_merge_queue_parse(json, strlen(json), &value));
  size_t len = strlen(value.prs[0].title);
  check("fits the cap", len < TK_MQ_TITLE_CAP);
  check("ends in an ellipsis",
        len >= 3 && strcmp(value.prs[0].title + len - 3, "\xe2\x80\xa6") == 0);
  check("cut on a character boundary",
        ((unsigned char)value.prs[0].title[len - 4] & 0xC0) != 0xC0);
  check("up source recorded",
        value.up_count == 1 && value.up_projects[0] == tk_mq_project_key("k"));

  char many[4096];
  int w = snprintf(many, sizeof many,
      "{\"v\":1,\"enabled\":true,\"count\":0,\"incomplete\":false,"
      "\"sources\":[");
  for (int i = 0; i <= TK_MQ_SOURCE_CAP; i++) {
    w += snprintf(many + w, sizeof many - (size_t)w,
                  "%s{\"project\":null,\"up\":false,\"paused\":false}",
                  i ? "," : "");
  }
  snprintf(many + w, sizeof many - (size_t)w, "],\"prs\":[]}");
  rejected_unchanged("more sources than the configuration allows", many);
  rejected_unchanged("an up source without a project",
      "{\"v\":1,\"enabled\":true,\"count\":0,\"incomplete\":false,"
      "\"sources\":[{\"project\":null,\"up\":true,\"paused\":false}],"
      "\"prs\":[]}");
}

static void test_title_label(void) {
  char out[64];
  tk_mq_title_label("feat: visa medlemmar", out, sizeof out);
  check("ascii upper", strcmp(out, "FEAT: VISA MEDLEMMAR") == 0);
  tk_mq_title_label("fix: \xc3\xa5\xc3\xa4\xc3\xb6 \xc3\x96", out, sizeof out);
  check("swedish upper", strcmp(out, "FIX: \xc3\x85\xc3\x84\xc3\x96 \xc3\x96") == 0);
  tk_mq_title_label("emoji \xf0\x9f\x9a\x80 \xc3\xa9 x", out, sizeof out);
  check("undrawable becomes ?", strcmp(out, "EMOJI ? ? X") == 0);
  tk_mq_title_label("a\tb", out, sizeof out);
  check("control dropped", strcmp(out, "AB") == 0);
  tk_mq_title_label("a \xe2\x80\x94 b \xe2\x80\x9cq\xe2\x80\x9d it\xe2\x80\x99s\xe2\x80\xa6 \xc2\xb7",
                    out, sizeof out);
  check("typography mapped",
        strcmp(out, "A \xe2\x80\x93 B \"Q\" IT'S... \xc2\xb7") == 0);
  char tiny[4];
  tk_mq_title_label("\xc3\xa5\xc3\xa5", tiny, sizeof tiny);
  check("no split multibyte at cap", strcmp(tiny, "\xc3\x85") == 0);
}

int main(void) {
  test_title_label();
  test_parse();
  test_policy();
  test_dismissed_does_not_return();
  test_incomplete_keeps_and_does_not_replay();
  test_truncated_list();
  test_pulse_waits_for_the_glass();
  test_long_title_is_cut_on_a_boundary();
  if (failures) {
    printf("%d merge-queue failures\n", failures);
    return 1;
  }
  printf("OK: merge-queue parse and policy\n");
  return 0;
}
