#ifndef AGENT_ASSETS_H
#define AGENT_ASSETS_H

#include "lvgl.h"

extern const lv_image_dsc_t tk_img_claude;
extern const lv_image_dsc_t tk_img_codex;
extern const lv_image_dsc_t tk_img_codex_64;
extern const lv_image_dsc_t tk_img_claude_32;
extern const lv_image_dsc_t tk_img_codex_32;

/* Merge queue card: the pull request icon, pre-colored merge green. */
extern const lv_image_dsc_t tk_img_merge;
extern const lv_image_dsc_t tk_img_merge_32;

/* Needs You takeover: the Claude pet, one pose per emotive beat, pre-colored
 * at integer pixel scales. */
extern const lv_image_dsc_t tk_img_mascot_asking_4;
extern const lv_image_dsc_t tk_img_mascot_neutral_4;
extern const lv_image_dsc_t tk_img_mascot_alert_8;
extern const lv_image_dsc_t tk_img_mascot_neutral_7;
extern const lv_image_dsc_t tk_img_mascot_happy_8;

#endif
