#include "axp2101.h"

#include "esp_log.h"

static const char *TAG = "axp2101";

/* Registerkartan enligt AXP2101-databladet (och XPowersLib, som Waveshares
 * fabriksdemo använder). Verifiera mot databladet innan du litar på ett
 * fält: den första läsningen loggar råbyten just därför. */
#define REG_STATUS1      0x00 /* bit5 VBUS good, bit3 batteri anslutet */
#define REG_STATUS2      0x01 /* bit6:5 strömriktning 01=laddar 10=urladdar; bit4:2 laddstatus, 4=klar */
#define REG_IC_TYPE      0x03 /* (v & 0xCF) == 0x4A */
#define REG_ADC_ENABLE   0x30 /* bit0 VBAT-ADC */
#define REG_VBAT_H       0x34 /* 14 bitar, 1 mV/LSB, high i [5:0] */
#define REG_VBAT_L       0x35
#define REG_BAT_PERCENT  0xA4 /* 0..100 */

#define ADDR 0x34
#define TIMEOUT_MS 50

static i2c_master_dev_handle_t s_dev;
static bool s_logged_raw;

static esp_err_t rd(uint8_t reg, uint8_t *out, size_t n) {
  return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, TIMEOUT_MS);
}

static esp_err_t wr(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = { reg, val };
  return i2c_master_transmit(s_dev, buf, sizeof buf, TIMEOUT_MS);
}

esp_err_t tg_axp2101_init(i2c_master_bus_handle_t bus) {
  if (!bus) return ESP_ERR_INVALID_ARG;
  i2c_device_config_t cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADDR,
    .scl_speed_hz = 400 * 1000,
  };
  esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
  if (err != ESP_OK) return err;
  uint8_t id = 0;
  err = rd(REG_IC_TYPE, &id, 1);
  if (err != ESP_OK) goto fail;
  if ((id & 0xCF) != 0x4A) {
    ESP_LOGW(TAG, "oväntat chip-id 0x%02x på 0x34, PMU:n lämnas orörd", id);
    err = ESP_ERR_NOT_FOUND;
    goto fail;
  }
  uint8_t adc = 0;
  if (rd(REG_ADC_ENABLE, &adc, 1) == ESP_OK && !(adc & 0x01)) {
    err = wr(REG_ADC_ENABLE, adc | 0x01);
    if (err != ESP_OK) ESP_LOGW(TAG, "kunde inte slå på VBAT-ADC: %s", esp_err_to_name(err));
  }
  ESP_LOGI(TAG, "AXP2101 hittad (id 0x%02x), läses var 5 s, konfigureras inte", id);
  return ESP_OK;

fail:
  i2c_master_bus_rm_device(s_dev);
  s_dev = NULL;
  return err;
}

tg_batt_sample tg_axp2101_read(void) {
  tg_batt_sample s = { .valid = false, .percent = -1, .mv = -1 };
  if (!s_dev) return s;
  uint8_t st[2], v[2], pct;
  if (rd(REG_STATUS1, st, 2) != ESP_OK) return s;
  if (rd(REG_VBAT_H, v, 2) != ESP_OK) return s;
  if (rd(REG_BAT_PERCENT, &pct, 1) != ESP_OK) return s;
  if (!s_logged_raw) {
    s_logged_raw = true;
    ESP_LOGI(TAG, "råbyten: status 0x%02x 0x%02x vbat 0x%02x 0x%02x pct %u",
             st[0], st[1], v[0], v[1], pct);
  }
  s.valid = true;
  s.present = (st[0] & 0x08) != 0;
  s.vbus = (st[0] & 0x20) != 0;
  unsigned dir = (st[1] >> 5) & 0x03;
  unsigned chg = (st[1] >> 2) & 0x07;
  s.charging = dir == 1;
  s.charge_done = chg == 4;
  s.mv = (int)(((v[0] & 0x3F) << 8) | v[1]);
  s.percent = pct <= 100 ? (int)pct : -1;
  return s;
}
