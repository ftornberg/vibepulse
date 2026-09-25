#include "app_time.h"

#include <string.h>
#include <time.h>

#include "lvgl.h"

#include "time_core.h"
#include "time_present.h"
#include "time_views.h"
#include "torget.h"

extern const lv_font_t plex_icon_64;

#define TICK_EVERY_MS 200

static struct {
  tg_time_mode mode;
  tg_pomo pomo;
  tg_countdown count;
  lv_timer_t *tick;
#ifndef ESP_PLATFORM
  int64_t skew_us;
  bool time_unset;
#endif
} app;

static int64_t now_us(void) {
  int64_t now = torget_now_us();
#ifndef ESP_PLATFORM
  now += app.skew_us;
#endif
  return now;
}

/* Tickar timrarna, läser orienteringen och ritar om vid ändring. Kallas under
 * UI-låset (lv_timer och touch-callbacks gör det redan). */
static void refresh(void) {
  int64_t now = now_us();
  tg_timer_tick(&app.pomo.timer, now);
  tg_timer_tick(&app.count.timer, now);
  app.mode = tg_time_mode_for(torget_orientation(), app.mode);

  time_t wall = time(NULL);
  bool valid = tg_time_clock_valid((int64_t)wall);
#ifndef ESP_PLATFORM
  if (app.time_unset) valid = false;
#endif
  int hour = 0, minute = 0, second = 0;
  struct tm local;
  if (valid && localtime_r(&wall, &local)) {
    hour = local.tm_hour;
    minute = local.tm_min;
    second = local.tm_sec;
  } else {
    valid = false;
  }

  tg_time_view_model model;
  tg_time_present(&model, app.mode, valid, hour, minute, second, &app.pomo, &app.count, now);
  time_views_render(&model);
}

static void tick_cb(lv_timer_t *timer) {
  (void)timer;
  refresh();
}

/* Ett tryck kvitterar först en färdig timer (pomodoron före timern, i vilket
 * läge som helst); annars gäller det läget man står i. */
static void on_tap(void) {
  /* Medvetet INGEN tick före beslutet: ett tryck som landar efter deadline men
   * före nästa 200 ms-tick ska visa KLAR (tg_*_tap tickar själv och stannar
   * där), inte kvittera den osedd. */
  int64_t now = now_us();
  switch (tg_time_done_source_of(&app.pomo, &app.count)) {
    case TG_TIME_DONE_POMODORO: tg_pomo_tap(&app.pomo, now); break;
    case TG_TIME_DONE_TIMER: tg_countdown_tap(&app.count, now); break;
    case TG_TIME_DONE_NONE:
      if (app.mode == TG_TIME_MODE_POMODORO) tg_pomo_tap(&app.pomo, now);
      else if (app.mode == TG_TIME_MODE_TIMER) tg_countdown_tap(&app.count, now);
      break;
  }
  refresh();
}

static void on_reset(void) {
  /* Samma regel som on_tap: en löpning som gick ut inom senaste tick visar
   * KLAR först i stället för att försvinna osedd i ett avbryt. */
  if (app.mode == TG_TIME_MODE_POMODORO) {
    if (!tg_timer_tick_expired(&app.pomo.timer, now_us())) tg_pomo_reset(&app.pomo);
  } else if (app.mode == TG_TIME_MODE_TIMER) {
    if (!tg_timer_tick_expired(&app.count.timer, now_us())) tg_countdown_reset(&app.count);
  }
  refresh();
}

static void on_preset(int idx) {
  if (app.mode != TG_TIME_MODE_TIMER) return;
  tg_countdown_start(&app.count, idx, now_us());
  refresh();
}

static void create(lv_obj_t *root) {
  memset(&app, 0, sizeof app);
  app.mode = TG_TIME_MODE_CLOCK;
  tg_pomo_init(&app.pomo);
  tg_countdown_init(&app.count);

  static const tg_time_view_actions actions = {
    .tap = on_tap, .reset = on_reset, .preset = on_preset,
  };
  time_views_create(root, &actions);
  refresh();

  /* Appen är dold vid boot; tickern går bara medan den syns. */
  app.tick = lv_timer_create(tick_cb, TICK_EVERY_MS, NULL);
  lv_timer_pause(app.tick);
}

static void enter(void) {
  if (!app.tick) return;
  lv_timer_resume(app.tick);
  lv_timer_ready(app.tick); /* rita rätt läge direkt, inte efter 200 ms */
}

static void leave(void) {
  if (app.tick) lv_timer_pause(app.tick);
}

const torget_app_t time_app = {
  .api_version = TORGET_APP_API_VERSION,
  .name = "TID",
  .icon = {
    .font = &plex_icon_64,
    .glyph = "T",
    .plate_hex = 0x12302A,
    .glyph_hex = 0xFFFFFF,
    .dot_hex = 0x5FD0A5,
  },
  .create = create,
  .enter = enter,
  .leave = leave,
};

#ifndef ESP_PLATFORM
void time_app_qa_refresh(void) { refresh(); }
void time_app_qa_advance(int64_t us) { app.skew_us += us; }
void time_app_qa_tap(void) { on_tap(); }
void time_app_qa_preset(int idx) { on_preset(idx); }
void time_app_qa_reset(void) { on_reset(); }
void time_app_qa_time_unset(bool unset) { app.time_unset = unset; }
#endif
