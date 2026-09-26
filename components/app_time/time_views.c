#include "time_views.h"

#include <string.h>

#include "torget.h"

extern const lv_font_t plex_num_118;
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

/* Läget innanför ringen. Förvalen kräver plats: siffran flyttar upp och
 * uppmaningen hamnar mellan siffran och förvalen. */
#define BIG_OFFSET_Y          (-8)
#define BIG_OFFSET_Y_PRESETS  (-54)
#define HINT_Y                300
#define HINT_Y_PRESETS        232

static struct {
  tg_time_view_actions actions;
  tg_time_view_model shown;
  bool has_shown;

  lv_obj_t *ring;
  lv_obj_t *caption;
  lv_obj_t *big;
  lv_obj_t *hint;
  lv_obj_t *dots[DOT_COUNT];
  lv_obj_t *presets_row;
  lv_obj_t *chips[TG_COUNTDOWN_PRESETS];
  lv_obj_t *reset;
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

static void on_reset(lv_event_t *e) {
  (void)e;
  if (v.actions.reset) v.actions.reset();
}

static void on_preset(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (v.actions.preset) v.actions.preset(idx);
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

void time_views_create(lv_obj_t *root, const tg_time_view_actions *actions) {
  memset(&v, 0, sizeof v);
  if (actions) v.actions = *actions;

  lv_obj_set_style_bg_color(root, COL_BLACK, 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  touchable(root, on_tap, NULL);

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
  v.caption = label(root, &plex_headline_48, COL_MUTED, 112);
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

  /* Förvalen 20/40/50 för den vanliga timern. */
  v.presets_row = plain(root, 480, 72);
  lv_obj_set_pos(v.presets_row, 0, 296);
  lv_obj_set_flex_flow(v.presets_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(v.presets_row, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(v.presets_row, 12, 0);
  for (int i = 0; i < TG_COUNTDOWN_PRESETS; i++) {
    lv_obj_t *chip = plain(v.presets_row, 96, 64);
    lv_obj_set_style_radius(chip, 20, 0);
    lv_obj_set_style_border_width(chip, 2, 0);
    lv_obj_set_style_border_color(chip, COL_TRACK, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *num = lv_label_create(chip);
    lv_obj_set_style_text_font(num, &plex_num_50, 0);
    lv_obj_set_style_text_color(num, COL_WHITE, 0);
    char text[8];
    lv_snprintf(text, sizeof text, "%d", tg_countdown_preset_minutes(i));
    lv_label_set_text(num, text);
    lv_obj_remove_flag(num, LV_OBJ_FLAG_CLICKABLE);
    touchable(chip, on_preset, (void *)(intptr_t)i);
    v.chips[i] = chip;
  }

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

  set_shown(v.presets_row, false);
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
  lv_label_set_text(v.hint, m->hint ? m->hint : "");

  for (int i = 0; i < DOT_COUNT; i++) {
    set_shown(v.dots[i], m->dots_done >= 0);
    lv_obj_set_style_bg_color(v.dots[i], i < m->dots_done ? COL_ACCENT : COL_TRACK, 0);
  }
  set_shown(v.presets_row, m->show_presets);
  set_shown(v.reset, m->show_reset);
  lv_obj_align(v.big, LV_ALIGN_CENTER, 0,
               m->show_presets ? BIG_OFFSET_Y_PRESETS : BIG_OFFSET_Y);
  lv_obj_set_y(v.hint, m->show_presets ? HINT_Y_PRESETS : HINT_Y);

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
