#include <assert.h>
#include <stdio.h>
#include "../components/app_tokens/labs_features.h"
#include "../components/app_tokens/app_tokens_config.h"
static uint32_t saved;
static int writes;
static bool fail_write;
static tk_labs_store_result result;
tk_labs_store_result tk_labs_store_read(uint32_t *record) {
  *record = saved;
  return result;
}
bool tk_labs_store_write(uint32_t record) {
  writes++;
  if (fail_write) return false;
  saved = record;
  result = TK_LABS_STORE_FOUND;
  return true;
}
int main(void) {
  result = TK_LABS_STORE_EMPTY;
  tk_labs_init();
  /* The always-on base is the two Claude pages plus Codex Weekly when
   * TK_CODEX_PAGES is 1; analytics adds burn rate, one tracker per shown
   * provider, and value. */
  const int base = 2 + TK_CODEX_PAGES;
  assert(tk_labs_view_count() == (TK_LABS_ANALYTICS_DEFAULT ? base + 2 + 1 + TK_CODEX_PAGES : base) +
                                TK_GITHUB_SCREEN_ENABLED);
  assert(tk_labs_view_position(VIEW_CODEX_WEEKLY) == (TK_CODEX_PAGES ? 2 : -1));
  assert(tk_labs_active(TK_LABS_GITHUB) == !!TK_GITHUB_SCREEN_ENABLED);
  assert(tk_labs_active(TK_LABS_STAR_POPUP) == !!TK_GITHUB_NOTIFICATIONS_ENABLED);
  assert(writes == 1 && !tk_labs_pending());
  /* Every subset must produce contiguous columns, including Value without
   * GitHub and popup without a page. Boot and selected must never be confused. */
  for (unsigned mask = 0; mask <= TK_LABS_ALL; mask++) {
    saved = TK_LABS_RECORD_VERSION | mask;
    tk_labs_init();
    int expected_count = base + !!(mask & 1) + (1 + TK_CODEX_PAGES) * !!(mask & 2) +
                         !!(mask & 4) + !!(mask & 8);
    assert(tk_labs_view_position(VIEW_TRACKER_CODEX) == -1 ||
           (TK_CODEX_PAGES && (mask & 2)));
    assert(tk_labs_view_count() == expected_count);
    int pos = 0, previous = -1;
    for (int view = 0; view < TK_USAGE_SCREEN_VIEWS; view++) {
      int at = tk_labs_view_position(view);
      if (at < 0) continue;
      assert(at == pos++);
      if (previous >= 0) {
        assert(tk_labs_next_view(previous, 1) == view);
        assert(tk_labs_next_view(view, -1) == previous);
      }
      previous = view;
    }
    assert(tk_labs_next_view(previous, 1) == 0);
    assert(tk_labs_next_view(0, -1) == previous);
    assert(tk_labs_view_position(-1) == -1);
    assert(tk_labs_view_position(8) == -1);
    for (int feature = 0; feature < TK_LABS_COUNT; feature++) {
      bool before = !!(mask & (1u << feature));
      assert(tk_labs_active(feature) == before);
      assert(tk_labs_toggle(feature));
      assert(tk_labs_selected(feature) != before);
      assert(tk_labs_active(feature) == before);
      assert(tk_labs_pending());
      assert(tk_labs_view_count() == expected_count);
      assert(tk_labs_toggle(feature));
      assert(!tk_labs_pending());
    }
  }
  saved = TK_LABS_RECORD_VERSION;
  tk_labs_init();
  assert(tk_labs_toggle(TK_LABS_VALUE));
  tk_labs_init(); /* reboot: saved choice wins over any template default */
  assert(tk_labs_active(TK_LABS_VALUE) && !tk_labs_pending());
  assert(tk_labs_view_count() == base + 1);
  assert(tk_labs_view_position(VIEW_VALUE) == base);
  fail_write = true;
  assert(!tk_labs_toggle(TK_LABS_VALUE));
  assert(tk_labs_selected(TK_LABS_VALUE) && tk_labs_storage_error());
  assert(!tk_labs_pending());
  fail_write = false;
  assert(tk_labs_toggle(TK_LABS_VALUE));
  assert(!tk_labs_storage_error());
  for (int mode = 0; mode < 3; mode++) {
    result = mode == 0 ? TK_LABS_STORE_ERROR : TK_LABS_STORE_FOUND;
    saved = mode == 1 ? 0x300u : 0x120u; /* newer version / unknown bit */
    int before_writes = writes;
    tk_labs_init();
    assert(tk_labs_storage_error());
    assert(!tk_labs_toggle(TK_LABS_VALUE));
    assert(writes == before_writes); /* don't clobber unreadable/future state */
  }
  result = TK_LABS_STORE_EMPTY;
  fail_write = true;
  tk_labs_init();
  assert(tk_labs_storage_error());
  fail_write = false;
  assert(tk_labs_toggle(TK_LABS_VALUE)); /* initial seed failure can recover */
  int before_writes = writes;
  assert(!tk_labs_toggle(-1) && !tk_labs_toggle(TK_LABS_COUNT));
  assert(writes == before_writes);

  /* NIGHT DIM: sixth feature, defaults from TK_NIGHT_ENABLED_DEFAULT. */
  result = TK_LABS_STORE_EMPTY;
  saved = 0;
  tk_labs_init();
  assert(tk_labs_active(TK_LABS_NIGHT_DIM) == !!TK_NIGHT_ENABLED_DEFAULT);
  assert((saved & ~TK_LABS_ALL) == TK_LABS_RECORD_VERSION);

  /* A v1 record (five features) migrates: old bits kept, night from default. */
  result = TK_LABS_STORE_FOUND;
  saved = TK_LABS_RECORD_VERSION_V1 | 9u; /* BURN RATE + GITHUB PAGE */
  before_writes = writes;
  tk_labs_init();
  assert(writes == before_writes); /* migrating on read must not write */
  assert(tk_labs_active(TK_LABS_BURN_RATE));
  assert(tk_labs_active(TK_LABS_GITHUB));
  assert(!tk_labs_active(TK_LABS_TRACKER));
  assert(tk_labs_active(TK_LABS_NIGHT_DIM) == !!TK_NIGHT_ENABLED_DEFAULT);
  assert(!tk_labs_storage_error());
  assert(tk_labs_toggle(TK_LABS_NIGHT_DIM));
  assert((saved & ~TK_LABS_ALL) == TK_LABS_RECORD_VERSION);
  assert((saved & TK_LABS_ALL) ==
         ((9u | (TK_NIGHT_ENABLED_DEFAULT ? 32u : 0u)) ^ 32u));

  /* An unknown future version stays read-only. */
  result = TK_LABS_STORE_FOUND;
  saved = 0x400u | 1u;
  before_writes = writes;
  tk_labs_init();
  assert(writes == before_writes); /* don't clobber unreadable/future state */
  assert(tk_labs_storage_error());
  assert(!tk_labs_toggle(TK_LABS_NIGHT_DIM));

  assert(tk_labs_name(TK_LABS_NIGHT_DIM)[0] == 'N');

  puts("OK: LABS migration, 64 dense page combinations, restart and storage failures");
}
