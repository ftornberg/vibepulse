#include "corner_probe.h"

#include <stdbool.h>
#include <stdio.h>

#include "display_geometry.h"
#include "lvgl.h"

extern const lv_font_t plex_ui_12;

/* Radierna som mäts. Bezeln på 2.16 misstänks ligga runt 90 px (brickan
 * vid 22 px marginal och Needs You-ramen vid 14 px + radie 40 klipptes
 * båda, sett 2026-09-25), så skalan går förbi det åt båda hållen. */
static const int RADII[] = { 40, 60, 80, 100, 120 };
#define N_RADII ((int)(sizeof RADII / sizeof RADII[0]))

/* En färg per radie så siffrorna går att para ihop med bågarna även när
 * de ligger tätt. */
static const uint32_t COLOURS[] = { 0xFFFFFF, 0xFFD45A, 0x8FBF6A, 0xD97757, 0xE0533A };

static lv_obj_t *s_root;

static void arc(int corner, int r) {
  /* corner: 0 = övre vänstra, 1 = övre högra, 2 = nedre högra, 3 = nedre
   * vänstra. Bågen tangerar båda kanterna: cirkeln har centrum r in från
   * hörnet, och bara kvartsvarvet mot hörnet ritas. LVGL:s vinklar går
   * medurs från klockan tre. */
  const int W = TG_DISPLAY_WIDTH, H = TG_DISPLAY_HEIGHT;
  int x = (corner == 1 || corner == 2) ? W - 2 * r : 0;
  int y = (corner == 2 || corner == 3) ? H - 2 * r : 0;
  static const int START[4] = { 180, 270, 0, 90 };
  lv_obj_t *a = lv_arc_create(s_root);
  lv_obj_remove_style_all(a);
  lv_obj_set_pos(a, x, y);
  lv_obj_set_size(a, 2 * r, 2 * r);
  lv_arc_set_rotation(a, 0);
  lv_arc_set_bg_angles(a, START[corner], START[corner] + 90);
  lv_arc_set_value(a, 0);
  lv_obj_set_style_arc_width(a, 2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, lv_color_hex(COLOURS[0]), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < N_RADII; i++)
    if (RADII[i] == r)
      lv_obj_set_style_arc_color(a, lv_color_hex(COLOURS[i]), LV_PART_MAIN);
}

static void tag(int corner, int r, int index) {
  /* Siffran vid bågens tangentpunkt på sidokanten (y = r från hörnet),
   * strax innanför kanten: där ligger bågarna 20 px isär och siffrorna
   * går att läsa, och färgen binder siffran till sin båge. */
  const int W = TG_DISPLAY_WIDTH, H = TG_DISPLAY_HEIGHT;
  char text[8];
  snprintf(text, sizeof text, "%d", r);
  lv_obj_t *l = lv_label_create(s_root);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &plex_ui_12, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COLOURS[index]), 0);
  lv_obj_set_width(l, 24);
  bool right = corner == 1 || corner == 2;
  bool bottom = corner == 2 || corner == 3;
  lv_obj_set_style_text_align(l, right ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
  int x = right ? W - 6 - 24 : 6;
  int y = bottom ? H - r - 7 : r - 7;
  lv_obj_set_pos(l, x, y);
}

void torget_corner_probe_show(void) {
  if (s_root) return;
  s_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_root);
  lv_obj_set_pos(s_root, TG_VIEWPORT_X, TG_VIEWPORT_Y);
  lv_obj_set_size(s_root, TG_DISPLAY_WIDTH, TG_DISPLAY_HEIGHT);
  lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  for (int c = 0; c < 4; c++)
    for (int i = 0; i < N_RADII; i++) {
      arc(c, RADII[i]);
      tag(c, RADII[i], i);
    }
  /* Kantlinjer 1 px runt hela glaset: syns de inte alls är också de raka
   * kanterna dolda under ramen, vilket är ett mått i sig. */
  lv_obj_t *edge = lv_obj_create(s_root);
  lv_obj_remove_style_all(edge);
  lv_obj_set_pos(edge, 0, 0);
  lv_obj_set_size(edge, TG_DISPLAY_WIDTH, TG_DISPLAY_HEIGHT);
  lv_obj_set_style_border_width(edge, 1, 0);
  lv_obj_set_style_border_color(edge, lv_color_hex(0x8A8F98), 0);
  lv_obj_set_style_border_opa(edge, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(edge, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(edge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(s_root);
  lv_label_set_text(title, "CORNER PROBE\nsmallest complete arc per corner = glass radius\n"
                           "white 40 - yellow 60 - green 80 - orange 100 - red 120");
  lv_obj_set_style_text_font(title, &plex_ui_12, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xBBBBBB), 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(title, TG_DISPLAY_WIDTH - 2 * 130);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
}

void torget_corner_probe_hide(void) {
  if (!s_root) return;
  lv_obj_del(s_root);
  s_root = NULL;
}
