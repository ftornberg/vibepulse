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
#else
static bool s_on = true;
bool tg_time_sound_load(void) { return s_on; }
bool tg_time_sound_save(bool on) { s_on = on; return true; }
#endif
