#include "time_views.h"

#include <string.h>

#include "speaker_assets.h"
#include "torget.h"

extern const lv_font_t plex_num_118;
extern const lv_font_t plex_num_84;
extern const lv_font_t plex_stat_35;
extern const lv_font_t plex_num_50;
extern const lv_font_t plex_headline_48;
extern const lv_font_t plex_attention_52;
extern const lv_font_t plex_ui_21;

/* Paletten är VibePulses (vibepulse_layout.generated.h och usage_screen.c),
 * vald av ägaren på glaset 2026-09-26: Claude-orange accent och VibePulses
 * mjukare off-white (COL_META) för de stora siffrorna — rent vitt blev skarpt
 * i så här stor yta på AMOLED. */
#define COL_BLACK  lv_color_hex(0x000000)
#define COL_WHITE  lv_color_hex(0xD9DCE2)
#define COL_MUTED  lv_color_hex(0x9298A2)
#define COL_TRACK  lv_color_hex(0x303238)
#define COL_ACCENT lv_color_hex(0xD97757)

#define DOT_COUNT 4

/* Läget innanför ringen. Mötets stegare kräver plats: rubriken och siffran
 * flyttar upp och uppmaningen hamnar mellan siffran och stegarna (skissen
 * 2026-11-02). */
#define CAPTION_Y             112
#define CAPTION_Y_STEPPERS    74
#define BIG_OFFSET_Y          (-8)
#define BIG_OFFSET_Y_STEPPERS (-62)
#define HINT_Y                300
#define HINT_Y_STEPPERS       232

/* Stegarna: en ram per värde, deltagare smal (högst två siffror) och
 * timkostnad bred (fyra). Ramarna ryms innanför glasets runda hörn. Varje
 * ram delas i två osynliga träffytor, − till vänster och + till höger, som
 * går 10 px utanför ramen uppåt och nedåt (84 px höga). */
#define STEP_Y        290
#define STEP_H        64
#define STEP_HIT_PAD  10
#define STEP_LABEL_Y  362
#define STEP_PEOPLE_X 38
#define STEP_PEOPLE_W 160
#define STEP_RATE_X   214
#define STEP_RATE_W   228

/* Startytan i inställningsläget: bandet med timkostnaden (siffrorna 150..252)
 * och raden "KR PER HOUR · TAP TO START" (232..258). Bakgrunden utanför
 * startar INTE mötet där: ett snabbt tryck strax utanför − eller + landade
 * på bakgrunden och drog igång räknaren (ägaren på glaset 2026-11-02).
 * Slutar ovanför stegarnas träffyta (STEP_Y - STEP_HIT_PAD = 280). */
#define START_Y       120
#define START_H       150

static struct {
  tg_time_view_actions actions;
  tg_time_view_model shown;
  bool has_shown;

  lv_obj_t *ring;
  lv_obj_t *caption;
  lv_obj_t *attention;
  const lv_image_dsc_t *attention_src;
  uint32_t attention_color;
  lv_obj_t *big;
  lv_obj_t *hint;
  lv_obj_t *dots[DOT_COUNT];
  lv_obj_t *steppers;
  lv_obj_t *step_value[2];
  lv_obj_t *start_area;
  lv_obj_t *reset;
  lv_obj_t *speaker_area;
  lv_obj_t *speaker;
  lv_obj_t *done_layer;
  lv_obj_t *done_caption;
  lv_obj_t *done_word;
} v;

/* ---- händelser ---------------------------------------------------------- */

static void on_long_press(lv_event_t *e) {
  (void)e;
  torget_launcher_open();
}

static void on_tap(lv_event_t *e) {
  (void)e;
  if (v.actions.tap) v.actions.tap();
}

/* Bakgrunden: ett tryck gäller överallt UTOM i mötets inställningsläge, där
 * bara startytan (och stegarna) tar emot tryck. Långtrycket för launchern
 * sitter kvar på bakgrunden oavsett läge. */
static void on_background_tap(lv_event_t *e) {
  if (v.has_shown && v.shown.show_steppers) return;
  on_tap(e);
}

static void on_reset(lv_event_t *e) {
  (void)e;
  if (v.actions.reset) v.actions.reset();
}

static void on_speaker(lv_event_t *e) {
  (void)e;
  if (v.actions.speaker) v.actions.speaker();
}

/* user_data: which * 2 + (dir > 0), så en pekare bär båda. */
static void on_step(lv_event_t *e) {
  int code = (int)(intptr_t)lv_event_get_user_data(e);
  if (v.actions.step) v.actions.step(code / 2, (code % 2) ? 1 : -1);
}

/* Tryck = SHORT_CLICKED, aldrig CLICKED: LVGL skickar även CLICKED efter ett
 * långt tryck, och då skulle launcherhållet också växla en timer. Långtryck
 * öppnar launchern var man än trycker (appkontraktet). */
static void touchable(lv_obj_t *o, lv_event_cb_t short_cb, void *user) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(o, short_cb, LV_EVENT_SHORT_CLICKED, user);
  lv_obj_add_event_cb(o, on_long_press, LV_EVENT_LONG_PRESSED, NULL);
}

/* ---- byggstenar --------------------------------------------------------- */

static void set_shown(lv_obj_t *o, bool shown) {
  if (shown) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Fast 480-bred, centrerad etikett: läget är då oberoende av textens bredd. */
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                       int y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(l, 480);
  lv_obj_set_pos(l, 0, y);
  lv_label_set_text(l, "");
  lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}

static lv_obj_t *plain(lv_obj_t *parent, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

/* En stegare i v.steppers: ram, −, värde, + och etikett under. Två osynliga
 * träffytor (vänster halva −, höger halva +) ligger ovanpå ramen. */
static void stepper(int which, int x, int w, const char *what) {
  int top = STEP_HIT_PAD; /* ramens y inne i behållaren */
  lv_obj_t *box = plain(v.steppers, w, STEP_H);
  lv_obj_set_pos(box, x, top);
  lv_obj_set_style_radius(box, 20, 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_border_color(box, COL_TRACK, 0);

  static const char *const glyph[2] = {"\xE2\x80\x93", "+"}; /* U+2013, + */
  for (int side = 0; side < 2; side++) {
    lv_obj_t *g = lv_label_create(box);
    lv_obj_set_style_text_font(g, &plex_stat_35, 0);
    lv_obj_set_style_text_color(g, COL_MUTED, 0);
    lv_label_set_text(g, glyph[side]);
    lv_obj_align(g, side ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
                 side ? -18 : 18, -2);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
  }

  lv_obj_t *num = lv_label_create(box);
  lv_obj_set_style_text_font(num, &plex_num_50, 0);
  lv_obj_set_style_text_color(num, COL_WHITE, 0);
  lv_label_set_text(num, "");
  lv_obj_center(num);
  lv_obj_remove_flag(num, LV_OBJ_FLAG_CLICKABLE);
  v.step_value[which] = num;

  lv_obj_t *cap = lv_label_create(v.steppers);
  lv_obj_set_style_text_font(cap, &plex_ui_21, 0);
  lv_obj_set_style_text_color(cap, COL_MUTED, 0);
  lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(cap, w);
  lv_obj_set_pos(cap, x, STEP_LABEL_Y - (STEP_Y - STEP_HIT_PAD));
  lv_label_set_text(cap, what);
  lv_obj_remove_flag(cap, LV_OBJ_FLAG_CLICKABLE);

  for (int side = 0; side < 2; side++) {
    lv_obj_t *hit = plain(v.steppers, w / 2, STEP_H + 2 * STEP_HIT_PAD);
    lv_obj_set_pos(hit, x + side * (w / 2), 0);
    touchable(hit, on_step, (void *)(intptr_t)(which * 2 + side));
  }
}

void time_views_create(lv_obj_t *root, const tg_time_view_actions *actions) {
  memset(&v, 0, sizeof v);
  if (actions) v.actions = *actions;

  lv_obj_set_style_bg_color(root, COL_BLACK, 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  touchable(root, on_background_tap, NULL);

  /* Ringen först, så att all text ritas ovanpå. Den fångar aldrig touch. */
  v.ring = lv_arc_create(root);
  lv_obj_set_size(v.ring, 448, 448);
  lv_obj_set_pos(v.ring, 16, 16);
  lv_arc_set_rotation(v.ring, 270);           /* börjar klockan 12 */
  lv_arc_set_bg_angles(v.ring, 0, 360);
  lv_arc_set_range(v.ring, 0, 1000);
  lv_arc_set_value(v.ring, 0);
  lv_obj_remove_style(v.ring, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(v.ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(v.ring, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(v.ring, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(v.ring, 0, LV_PART_MAIN);
  lv_obj_set_style_arc_width(v.ring, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(v.ring, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(v.ring, COL_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_color(v.ring, COL_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(v.ring, false, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(v.ring, false, LV_PART_INDICATOR);
  set_shown(v.ring, false);

  /* Allt annat ryms innanför ringens innerkant (radie ~214). */
  v.caption = label(root, &plex_headline_48, COL_MUTED, CAPTION_Y);
  /* Uppmärksamhetsikonen (Claude väntar på dig): 32 px, centrerad ovanför
   * klockans siffror och innanför ringen. Källa och färg kommer från
   * plattformen; synligheten från vymodellen. */
  v.attention = lv_image_create(root);
  lv_obj_set_size(v.attention, 32, 32);
  lv_obj_set_pos(v.attention, 224, 128);
  lv_obj_remove_flag(v.attention, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_image_recolor_opa(v.attention, LV_OPA_COVER, 0);
  lv_obj_add_flag(v.attention, LV_OBJ_FLAG_HIDDEN);
  v.big = label(root, &plex_num_118, COL_WHITE, 0);
  lv_obj_align(v.big, LV_ALIGN_CENTER, 0, BIG_OFFSET_Y);
  v.hint = label(root, &plex_ui_21, COL_MUTED, HINT_Y);

  /* Fokusblocken: fyra prickar, fyllda för klara block. */
  for (int i = 0; i < DOT_COUNT; i++) {
    lv_obj_t *dot = plain(root, 16, 16);
    lv_obj_set_pos(dot, 178 + i * 36, 350);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, COL_TRACK, 0);
    v.dots[i] = dot;
  }

  /* Mötets stegare: deltagare och timkostnad per person. Behållaren täcker
   * bara stegarnas band och tar själv aldrig emot tryck. */
  v.steppers = plain(root, 480, STEP_LABEL_Y + 30 - (STEP_Y - STEP_HIT_PAD));
  lv_obj_set_pos(v.steppers, 0, STEP_Y - STEP_HIT_PAD);
  stepper(0, STEP_PEOPLE_X, STEP_PEOPLE_W, "PEOPLE");
  stepper(1, STEP_RATE_X, STEP_RATE_W, "KR/H EACH");

  /* Startytan: osynlig, bara i inställningsläget, över timkostnaden. */
  v.start_area = plain(root, 480, START_H);
  lv_obj_set_pos(v.start_area, 0, START_Y);
  touchable(v.start_area, on_tap, NULL);

  /* RESET: en stor träffyta, en liten text. */
  /* 260 x 84 (ägaren 2026-09-26: 200 x 56 var för litet för ett finger på
   * glaset). Texten ligger kvar där den låg; ytan börjar under prickarna
   * (y 366) och får gå in under ringen, som aldrig tar emot tryck. */
  v.reset = plain(root, 260, 84);
  lv_obj_set_pos(v.reset, 110, 380);
  lv_obj_t *reset_text = lv_label_create(v.reset);
  lv_obj_set_style_text_font(reset_text, &plex_ui_21, 0);
  lv_obj_set_style_text_color(reset_text, COL_MUTED, 0);
  lv_label_set_text(reset_text, "RESET");
  lv_obj_center(reset_text);
  lv_obj_remove_flag(reset_text, LV_OBJ_FLAG_CLICKABLE);
  touchable(v.reset, on_reset, NULL);

  /* Högtalaren: 24 px nere i ringen, en 96 x 64-yta att träffa (spec
   * 2026-10-02). Bara på klockan. */
  v.speaker_area = plain(root, 96, 64);
  lv_obj_set_pos(v.speaker_area, 192, 392);
  touchable(v.speaker_area, on_speaker, NULL);
  v.speaker = lv_image_create(v.speaker_area);
  lv_image_set_src(v.speaker, &tg_img_speaker_on);
  lv_obj_set_style_image_recolor(v.speaker, COL_MUTED, 0);
  lv_obj_set_style_image_recolor_opa(v.speaker, LV_OPA_COVER, 0);
  lv_obj_center(v.speaker);
  lv_obj_remove_flag(v.speaker, LV_OBJ_FLAG_CLICKABLE);
  set_shown(v.speaker_area, false);

  /* KLAR: ett helskärmslager (fångar trycket) med en full cirkel i ringens
   * läge — en rund ram klipps inte av den rundade kåpan och krockar inte med
   * batteribrickan i hörnet, som en rektangulär ram gjorde. */
  v.done_layer = plain(root, 480, 480);
  lv_obj_set_pos(v.done_layer, 0, 0);
  lv_obj_set_style_bg_color(v.done_layer, COL_BLACK, 0);
  lv_obj_set_style_bg_opa(v.done_layer, LV_OPA_COVER, 0);
  lv_obj_t *frame = plain(v.done_layer, 448, 448);
  lv_obj_set_pos(frame, 16, 16);
  lv_obj_set_style_radius(frame, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(frame, 4, 0);
  lv_obj_set_style_border_color(frame, COL_ACCENT, 0);
  v.done_caption = label(v.done_layer, &plex_headline_48, COL_MUTED, 130);
  v.done_word = label(v.done_layer, &plex_attention_52, COL_WHITE, 200);
  lv_label_set_text(v.done_word, "DONE");
  lv_obj_t *done_hint = label(v.done_layer, &plex_ui_21, COL_MUTED, 300);
  lv_label_set_text(done_hint, "TAP TO CONTINUE");
  touchable(v.done_layer, on_tap, NULL);

  set_shown(v.steppers, false);
  set_shown(v.start_area, false);
  set_shown(v.reset, false);
  set_shown(v.done_layer, false);
}

void time_views_render(const tg_time_view_model *m) {
  if (v.has_shown && memcmp(&v.shown, m, sizeof *m) == 0) return;
  memcpy(&v.shown, m, sizeof *m);
  v.has_shown = true;

  lv_label_set_text(v.caption, m->caption ? m->caption : "");
  lv_label_set_text(v.big, m->big);
  lv_obj_set_style_text_color(v.big, m->muted ? COL_MUTED : COL_WHITE, 0);
  lv_label_set_text(v.hint, m->hint);

  for (int i = 0; i < DOT_COUNT; i++) {
    set_shown(v.dots[i], m->dots_done >= 0);
    lv_obj_set_style_bg_color(v.dots[i], i < m->dots_done ? COL_ACCENT : COL_TRACK, 0);
  }
  set_shown(v.attention, m->show_attention && v.attention_src != NULL);
  set_shown(v.speaker_area, m->show_speaker);
  lv_image_set_src(v.speaker, m->speaker_on ? &tg_img_speaker_on : &tg_img_speaker_off);
  set_shown(v.steppers, m->show_steppers);
  set_shown(v.start_area, m->show_steppers);
  if (m->show_steppers) {
    lv_label_set_text(v.step_value[0], m->people);
    lv_label_set_text(v.step_value[1], m->rate);
  }
  set_shown(v.reset, m->show_reset);
  lv_obj_set_style_text_font(v.big, m->big_compact ? &plex_num_84 : &plex_num_118, 0);
  lv_obj_set_y(v.caption, m->show_steppers ? CAPTION_Y_STEPPERS : CAPTION_Y);
  lv_obj_align(v.big, LV_ALIGN_CENTER, 0,
               m->show_steppers ? BIG_OFFSET_Y_STEPPERS : BIG_OFFSET_Y);
  lv_obj_set_y(v.hint, m->show_steppers ? HINT_Y_STEPPERS : HINT_Y);

  /* Bågen sätts med vinklar, inte med ett värde: klockans ring kan växa från
   * båda kanterna (alltid medurs), timrarnas krymper mot 12. 1 promille =
   * 0,36 grader; en sekund är exakt 6 grader. */
  set_shown(v.ring, m->ring_end >= 0);
  if (m->ring_end >= 0)
    lv_arc_set_angles(v.ring, (m->ring_start * 360 + 500) / 1000,
                      (m->ring_end * 360 + 500) / 1000);

  lv_label_set_text(v.done_caption, m->caption ? m->caption : "");
  set_shown(v.done_layer, m->done);
}

void time_views_set_attention(const lv_image_dsc_t *icon, uint32_t color_hex) {
  if (!v.attention || (icon == v.attention_src && color_hex == v.attention_color))
    return;
  v.attention_src = icon;
  v.attention_color = color_hex;
  if (icon) {
    lv_image_set_src(v.attention, icon);
    lv_obj_set_style_image_recolor(v.attention, lv_color_hex(color_hex), 0);
  }
  v.has_shown = false; /* rita om: synligheten beror också på källan */
}
