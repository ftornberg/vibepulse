#include "pcf85063.h"

#include "esp_log.h"

static const char *TAG = "pcf85063";

#define ADDR 0x51
#define TIMEOUT_MS 50
#define REG_CONTROL1 0x00 /* bit5 STOP, bit1 12_24 (0 = 24 h) */
#define REG_RAM      0x03 /* fri byte, batteribackad som klockan: vårt UTC-märke */
#define REG_SECONDS  0x04 /* bit7 OS (oscillatorn har stannat), BCD 0..59 */
/* 0x05 minuter, 0x06 timmar, 0x07 dag, 0x08 veckodag, 0x09 månad, 0x0A år (00..99) */
#define CTL_STOP  0x20
#define CTL_12_24 0x02
#define UTC_MARK  0x54 /* 'T' */

static i2c_master_dev_handle_t s_dev;

static esp_err_t rd(uint8_t reg, uint8_t *out, size_t n) {
  return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, TIMEOUT_MS);
}

static esp_err_t wr(uint8_t reg, const uint8_t *data, size_t n) {
  uint8_t buf[1 + 8];
  if (n > 8) return ESP_ERR_INVALID_SIZE;
  buf[0] = reg;
  for (size_t i = 0; i < n; i++) buf[1 + i] = data[i];
  return i2c_master_transmit(s_dev, buf, 1 + n, TIMEOUT_MS);
}

static int bcd_to_int(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t int_to_bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t tg_pcf85063_init(i2c_master_bus_handle_t bus) {
  if (!bus) return ESP_ERR_INVALID_ARG;
  i2c_device_config_t cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADDR,
    .scl_speed_hz = 400 * 1000,
  };
  esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
  if (err != ESP_OK) return err;
  uint8_t ctl = 0;
  err = rd(REG_CONTROL1, &ctl, 1);
  if (err != ESP_OK) goto fail;
  if (ctl & (CTL_STOP | CTL_12_24)) {
    /* Kretsen är inte i det läge vi skriver den i: någon annan har ställt
     * den. Rätta läget och nolla märket, så bootavläsningen (som avkodats
     * fel eller stått stilla) avvisas och nästa SNTP-skrivning börjar om. */
    uint8_t fixed = (uint8_t)(ctl & ~(CTL_STOP | CTL_12_24));
    err = wr(REG_CONTROL1, &fixed, 1);
    if (err != ESP_OK) goto fail;
    uint8_t blank = 0;
    err = wr(REG_RAM, &blank, 1);
    if (err != ESP_OK) goto fail;
    ESP_LOGW(TAG, "Control_1 0x%02x rättat (%s%s), avläsningen avvisas tills SNTP skrivit",
             ctl, (ctl & CTL_STOP) ? "STOP" : "",
             (ctl & CTL_12_24) ? ((ctl & CTL_STOP) ? " + 12 h" : "12 h") : "");
  }
  ESP_LOGI(TAG, "PCF85063 hittad (Control_1 0x%02x)", ctl);
  return ESP_OK;
fail:
  i2c_master_bus_rm_device(s_dev);
  s_dev = NULL;
  return err;
}

esp_err_t tg_pcf85063_read(tg_rtc_reading *out) {
  if (!s_dev) return ESP_ERR_INVALID_STATE;
  if (!out) return ESP_ERR_INVALID_ARG;
  uint8_t r[8]; /* RAM-byte + sju tidsregister, en transaktion */
  esp_err_t err = rd(REG_RAM, r, sizeof r);
  if (err != ESP_OK) return err;
  out->utc_marked = r[0] == UTC_MARK;
  out->os = (r[1] & 0x80) != 0;
  out->civil.second = bcd_to_int(r[1] & 0x7F);
  out->civil.minute = bcd_to_int(r[2] & 0x7F);
  out->civil.hour = bcd_to_int(r[3] & 0x3F);
  out->civil.day = bcd_to_int(r[4] & 0x3F);
  /* r[5] veckodag, härleds ur datumet i stället */
  out->civil.month = bcd_to_int(r[6] & 0x1F);
  out->civil.year = 2000 + bcd_to_int(r[7]);
  return ESP_OK;
}

esp_err_t tg_pcf85063_write(const tg_civil *utc) {
  if (!s_dev) return ESP_ERR_INVALID_STATE;
  if (!utc || utc->year < 2000 || utc->year > 2099) return ESP_ERR_INVALID_ARG;
  int wd = tg_civil_weekday(utc);
  if (wd < 0) return ESP_ERR_INVALID_ARG;
  uint8_t w[8] = {
    UTC_MARK /* RAM-byten: tiden nedan är vår och i UTC */,
    int_to_bcd(utc->second) /* OS-biten (bit7) skrivs 0 = klockan gäller */,
    int_to_bcd(utc->minute),
    int_to_bcd(utc->hour),
    int_to_bcd(utc->day),
    (uint8_t)wd,
    int_to_bcd(utc->month),
    int_to_bcd(utc->year - 2000),
  };
  return wr(REG_RAM, w, sizeof w);
}
