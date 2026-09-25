#include "battery_badge.h"

#include <stdio.h>

#include "display_geometry.h"
#include "lvgl.h"

extern const lv_font_t plex_ui_12;

/* Sidfotens högra kant: samma marginal som "TO RESET" (22 px) och samma
 * baslinje som sidprickarna (PAGER_Y 456 på 2.16). Sidprickarna är
 * horisontellt centrerade och badgen sitter i högerkanten, så de delar
 * rad utan att krocka; kontrollerat mot bilderna i task-4-report.md. */
#define BADGE_RIGHT_MARGIN 22
#define BADGE_Y (456 - (TG_VIEWPORT_INSET_Y ? 4 : 0))
#define BODY_W 26
#define BODY_H 13
#define NUB_W 3
#define NUB_H 7
#define BOLT_W 8
#define GAP 4
#define PCT_W 40 /* "100%" i plex_ui_12 med letter-space 1 får plats */

#define COL_OUTLINE lv_color_hex(0xBBBBBB)
#define COL_OK      lv_color_hex(0xFFFFFF)
#define COL_CHARGE  lv_color_hex(0x8FBF6A)
#define COL_LOW     lv_color_hex(0xFFD45A)
#define COL_CRIT    lv_color_hex(0xE0533A)
#define COL_MUTED   lv_color_hex(0x8A8F98)

static struct {
  lv_obj_t *root, *body, *fill, *nub, *bolt_top, *bolt_bottom, *dash, *pct;
  lv_anim_t pulse;
  tg_batt_state state;
  int percent;
  bool covered;
  bool created;
} ui;

static void pulse_cb(void *obj, int32_t v) {
  lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, c, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

static void apply(void);

void torget_battery_badge_create(void) {
#ifdef TORGET_BOARD_241_V2
  /* Board scope (docs/superpowers/specs/2026-09-24-battery-badge-and-rtc-
   * night-dim-design.md): "Waveshare ESP32-S3 Touch-AMOLED-2.16 only. The
   * 2.41 V2 has a different PMU wiring and is out of scope until its
   * registry says otherwise." No badge exists to create on that board —
   * set()/set_covered() below already no-op on an uncreated badge. Also,
   * this geometry (BADGE_RIGHT_MARGIN/BADGE_Y against TG_DISPLAY_WIDTH)
   * assumes the 2.16 board, where the viewport and the physical display
   * are the same size; on 241 V2 the 480 px content tile is centred inside
   * a wider 600 px physical display, so this math would misplace it. */
  return;
#endif
  if (ui.created) return;
  ui.root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ui.root);
  int w = PCT_W + GAP + BOLT_W + GAP + BODY_W + NUB_W;
  lv_obj_set_size(ui.root, w, 16);
  lv_obj_set_pos(ui.root, TG_VIEWPORT_X + TG_DISPLAY_WIDTH - BADGE_RIGHT_MARGIN - w,
                 TG_VIEWPORT_Y + BADGE_Y - 2);
  lv_obj_clear_flag(ui.root, LV_OBJ_FLAG_CLICKABLE);

  ui.pct = lv_label_create(ui.root);
  lv_obj_set_style_text_font(ui.pct, &plex_ui_12, 0);
  lv_obj_set_style_text_color(ui.pct, COL_OUTLINE, 0);
  lv_obj_set_style_text_letter_space(ui.pct, 1, 0);
  lv_obj_set_style_text_align(ui.pct, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(ui.pct, 0, 1);
  lv_obj_set_width(ui.pct, PCT_W);
  /* Aldrig radbryta: en för smal etikett bröt "100%" och klippte "%". */
  lv_label_set_long_mode(ui.pct, LV_LABEL_LONG_CLIP);

  /* plex_ui_12 is a project glyph set (cmap 32..~8200) without the
   * FontAwesome range LV_SYMBOL_CHARGE lives in (U+F0E7 / 61671) — the
   * label drew nothing. Two small slabs stand in for the bolt instead. */
  int bx = PCT_W + GAP;
  ui.bolt_top = box(ui.root, bx, 2, 3, 5, COL_LOW);
  ui.bolt_bottom = box(ui.root, bx + 4, 6, 3, 5, COL_LOW);

  bx += BOLT_W + GAP;
  ui.body = box(ui.root, bx, 1, BODY_W, BODY_H, lv_color_black());
  lv_obj_set_style_border_color(ui.body, COL_OUTLINE, 0);
  lv_obj_set_style_border_width(ui.body, 2, 0);
  lv_obj_set_style_radius(ui.body, 3, 0);
  ui.fill = box(ui.root, bx + 3, 4, BODY_W - 6, BODY_H - 6, COL_OK);
  lv_obj_set_style_radius(ui.fill, 1, 0);
  ui.dash = box(ui.root, bx + 9, 7, BODY_W - 18, 2, COL_MUTED);
  ui.nub = box(ui.root, bx + BODY_W, 4, NUB_W, NUB_H, COL_OUTLINE);

  /* No placeholder can ever show, even for a frame: LVGL seeds a fresh
   * label with the literal text "Text", and torget_battery_badge_set()'s
   * dedupe guard would otherwise skip the very first apply() below (state
   * and percent already match the UNKNOWN/-1 it's about to be told). */
  lv_label_set_text(ui.pct, "");
  ui.state = TG_BATT_UNKNOWN;
  ui.percent = -1;
  ui.created = true;
  apply();
}

static void apply(void) {
  if (!ui.created) return;
  lv_anim_delete(ui.fill, pulse_cb);
  lv_obj_set_style_bg_opa(ui.fill, LV_OPA_COVER, 0);
  bool unknown = ui.state == TG_BATT_UNKNOWN;
  bool vbus = ui.state == TG_BATT_CHARGING || ui.state == TG_BATT_FULL;
  lv_color_t fill = COL_OK;
  if (vbus) fill = COL_CHARGE;
  else if (ui.state == TG_BATT_LOW) fill = COL_LOW;
  else if (ui.state == TG_BATT_CRITICAL) fill = COL_CRIT;
  lv_obj_set_style_bg_color(ui.fill, fill, 0);
  /* The percent number carries the state colour too: at low percentages the
   * fill sliver itself is only 1-2 px wide (proportional, correctly so —
   * it must not overstate the charge) and easy to miss, so LOW/CRITICAL
   * need the number itself to read as coloured, not just outline-grey. */
  lv_obj_set_style_text_color(ui.pct, unknown ? COL_OUTLINE : fill, 0);

  int pct = ui.percent;
  int inner = BODY_W - 6;
  int w = unknown || pct < 0 ? 0 : (inner * pct + 50) / 100;
  if (w < 1 && !unknown && pct >= 0) w = 1;
  lv_obj_set_width(ui.fill, w > 0 ? w : 1);
  if (unknown || pct < 0) lv_obj_add_flag(ui.fill, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(ui.fill, LV_OBJ_FLAG_HIDDEN);
  if (unknown) lv_obj_clear_flag(ui.dash, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(ui.dash, LV_OBJ_FLAG_HIDDEN);
  if (vbus) {
    lv_obj_clear_flag(ui.bolt_top, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui.bolt_bottom, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ui.bolt_top, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.bolt_bottom, LV_OBJ_FLAG_HIDDEN);
  }

  if (pct >= 0 && !unknown) {
    char text[12]; /* INT_MAX-siffror + "%" + NUL, annars format-truncation */
    snprintf(text, sizeof text, "%d%%", pct);
    lv_label_set_text(ui.pct, text);
    lv_obj_clear_flag(ui.pct, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ui.pct, LV_OBJ_FLAG_HIDDEN);
  }

  if (ui.state == TG_BATT_CRITICAL) {
    lv_anim_init(&ui.pulse);
    lv_anim_set_var(&ui.pulse, ui.fill);
    lv_anim_set_exec_cb(&ui.pulse, pulse_cb);
    lv_anim_set_values(&ui.pulse, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_duration(&ui.pulse, 700);
    lv_anim_set_playback_duration(&ui.pulse, 700);
    lv_anim_set_repeat_count(&ui.pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&ui.pulse);
  }

  if (ui.covered) lv_obj_add_flag(ui.root, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(ui.root, LV_OBJ_FLAG_HIDDEN);
}

void torget_battery_badge_set(tg_batt_state state, int percent) {
  if (!ui.created) return;
  if (state == ui.state && percent == ui.percent) return;
  ui.state = state;
  ui.percent = percent;
  apply();
}

void torget_battery_badge_set_covered(bool covered) {
  if (!ui.created || covered == ui.covered) return;
  ui.covered = covered;
  apply();
}
