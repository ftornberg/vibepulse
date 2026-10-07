#include "app_time.h"

#include <string.h>
#include <time.h>

#include "lvgl.h"

#include "time_core.h"
#include "time_present.h"
#include "time_sound_store.h"
#include "time_views.h"
#include "torget.h"

extern const lv_font_t plex_icon_64;

#define TICK_EVERY_MS 200
#define WATCH_EVERY_MS 1000

static struct {
  tg_time_mode mode;
  tg_pomo pomo;
  tg_countdown count;
  lv_timer_t *tick;
  lv_timer_t *watch;
  bool sound_on;
#ifndef ESP_PLATFORM
  int64_t skew_us;
  bool time_unset;
  int fixed_hour, fixed_minute, fixed_second; /* fixed_hour < 0: av */
#endif
} app;

static int64_t now_us(void) {
  int64_t now = torget_now_us();
#ifndef ESP_PLATFORM
  now += app.skew_us;
#endif
  return now;
}

/* En timer som just gick ut låter, om ljudet är på (spec 2026-10-02). En paus
 * som är slut har sin egen signal; två utgångar på samma tick ger en. Kallas
 * före kvitteringen, medan pomodoron står kvar på fasen som tog slut. */
static void chime_on(int expired) {
  int cue = tg_time_cue_for(expired, &app.pomo);
  if (cue >= 0 && app.sound_on) torget_audio_play((tg_audio_cue)cue);
}

/* Tickar timrarna, läser orienteringen och ritar om vid ändring. Kallas under
 * UI-låset (lv_timer och touch-callbacks gör det redan). All tickning går
 * genom tg_time_advance, så att varje utgång hörs exakt en gång. */
static void refresh(void) {
  int64_t now = now_us();
  chime_on(tg_time_advance(&app.pomo, &app.count, now));
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
#ifndef ESP_PLATFORM
  if (app.fixed_hour >= 0 && !app.time_unset) {
    valid = true;
    hour = app.fixed_hour;
    minute = app.fixed_minute;
    second = app.fixed_second;
  }
#endif

  tg_time_view_model model;
  uint32_t attention_color = 0;
  const lv_image_dsc_t *attention = torget_attention_icon(&attention_color);
  time_views_set_attention(attention, attention_color);
  tg_time_present(&model, app.mode, valid, hour, minute, second, &app.pomo,
                  &app.count, now, attention != NULL, app.sound_on);
  time_views_render(&model);
}

static void tick_cb(lv_timer_t *timer) {
  (void)timer;
  refresh();
}

/* Bevakningen går ALLTID, även när TID är dold: en timer som går ut medan
 * VibePulse visas ska höras. Den ritar ingenting. */
static void watch_cb(lv_timer_t *timer) {
  (void)timer;
  chime_on(tg_time_advance(&app.pomo, &app.count, now_us()));
}

/* Ett tryck kvitterar först en färdig timer (pomodoron före timern, i vilket
 * läge som helst); annars gäller det läget man står i. */
static void on_tap(void) {
  /* Medvetet INGEN tick före beslutet: ett tryck som landar efter deadline men
   * före nästa 200 ms-tick ska visa KLAR (tg_*_tap tickar själv och stannar
   * där), inte kvittera den osedd. */
  int64_t now = now_us();
  /* Gick en löpning ut just nu: låt den höras och visa KLAR, kvittera inte. */
  int expired = tg_time_advance(&app.pomo, &app.count, now);
  if (expired) {
    chime_on(expired);
    refresh();
    return;
  }
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
  int expired = tg_time_advance(&app.pomo, &app.count, now_us());
  if (expired) {
    chime_on(expired);
  } else if (app.mode == TG_TIME_MODE_POMODORO) {
    tg_pomo_reset(&app.pomo);
  } else if (app.mode == TG_TIME_MODE_TIMER) {
    tg_countdown_reset(&app.count);
  }
  refresh();
}

static void on_preset(int idx) {
  if (app.mode != TG_TIME_MODE_TIMER) return;
  tg_countdown_start(&app.count, idx, now_us());
  refresh();
}

/* Högtalarsymbolen: av/på, sparas. Påslaget spelar signalen en gång — en
 * bekräftelse och samtidigt provet för den fysiska grinden. */
static void on_speaker(void) {
  app.sound_on = !app.sound_on;
  tg_time_sound_save(app.sound_on);
  if (app.sound_on) torget_audio_play(TG_AUDIO_CUE_DONE);
  refresh();
}

static void create(lv_obj_t *root) {
  memset(&app, 0, sizeof app);
  app.mode = TG_TIME_MODE_CLOCK;
#ifndef ESP_PLATFORM
  app.fixed_hour = -1;
#endif
  tg_pomo_init(&app.pomo);
  tg_countdown_init(&app.count);
  app.sound_on = tg_time_sound_load();

  static const tg_time_view_actions actions = {
    .tap = on_tap, .reset = on_reset, .preset = on_preset, .speaker = on_speaker,
  };
  time_views_create(root, &actions);
  refresh();

  /* Appen är dold vid boot; tickern går bara medan den syns. */
  app.tick = lv_timer_create(tick_cb, TICK_EVERY_MS, NULL);
  lv_timer_pause(app.tick);
  app.watch = lv_timer_create(watch_cb, WATCH_EVERY_MS, NULL);
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
    .plate_hex = 0x2A1812,
    .glyph_hex = 0xFFFFFF,
    .dot_hex = 0xD97757,
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
void time_app_qa_speaker(void) { on_speaker(); }
void time_app_qa_clock(int hour, int minute, int second) {
  app.fixed_hour = hour;
  app.fixed_minute = minute;
  app.fixed_second = second;
}
#endif
