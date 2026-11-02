#include "time_sound_store.h"

#ifdef ESP_PLATFORM
#include <stdint.h>

#include "nvs.h"

bool tg_time_sound_load(void) {
  nvs_handle_t h;
  if (nvs_open("tg_time", NVS_READONLY, &h) != ESP_OK) return true;
  uint8_t v = 1;
  esp_err_t err = nvs_get_u8(h, "sound", &v);
  nvs_close(h);
  return err == ESP_OK ? v != 0 : true;
}

bool tg_time_sound_save(bool on) {
  nvs_handle_t h;
  if (nvs_open("tg_time", NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_u8(h, "sound", on ? 1 : 0);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

void tg_time_meeting_load(int *people, int *rate) {
  nvs_handle_t h;
  if (nvs_open("tg_time", NVS_READONLY, &h) != ESP_OK) return;
  uint8_t p = 0;
  uint16_t r = 0;
  if (nvs_get_u8(h, "mt_people", &p) == ESP_OK &&
      nvs_get_u16(h, "mt_rate", &r) == ESP_OK) {
    *people = p;
    *rate = r;
  }
  nvs_close(h);
}

bool tg_time_meeting_save(int people, int rate) {
  if (people < 0 || people > 255 || rate < 0 || rate > 65535) return false;
  nvs_handle_t h;
  if (nvs_open("tg_time", NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_u8(h, "mt_people", (uint8_t)people);
  if (err == ESP_OK) err = nvs_set_u16(h, "mt_rate", (uint16_t)rate);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}
#else
static bool s_on = true;
static int s_people = -1, s_rate = -1;
bool tg_time_sound_load(void) { return s_on; }
bool tg_time_sound_save(bool on) { s_on = on; return true; }
void tg_time_meeting_load(int *people, int *rate) {
  if (s_people < 0) return;
  *people = s_people;
  *rate = s_rate;
}
bool tg_time_meeting_save(int people, int rate) {
  s_people = people;
  s_rate = rate;
  return true;
}
#endif
