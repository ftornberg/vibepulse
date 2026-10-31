#include "audio.h"

#include "secrets.h"

#ifndef TK_TID_SOUND
#define TK_TID_SOUND 0
#endif

#if TK_TID_SOUND && !defined(TORGET_BOARD_241_V2)

#include <stdatomic.h>

#include "bsp/esp32_s3_touch_amoled_2_16.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_policy.h"

#ifndef TG_AUDIO_VOLUME
#define TG_AUDIO_VOLUME 45 /* lågt startvärde; den fysiska grinden sätter det */
#endif
#define CHUNK_FRAMES 256u

static const char *TAG = "audio";
static atomic_bool s_playing;
static atomic_bool s_disabled;
static uint8_t s_failures;

static size_t dma_largest(void) {
  return heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
}

typedef struct {
  i2s_chan_handle_t tx;
  const audio_codec_data_if_t *data_if;
  const audio_codec_ctrl_if_t *ctrl_if;
  const audio_codec_gpio_if_t *gpio_if;
  const audio_codec_if_t *codec_if;
  esp_codec_dev_handle_t dev;
  bool opened;
} chain;

/* Riv i omvänd ordning, oavsett hur långt starten kom. */
static void teardown(chain *c) {
  if (c->opened) esp_codec_dev_close(c->dev);
  if (c->dev) esp_codec_dev_delete(c->dev);
  if (c->codec_if) audio_codec_delete_codec_if(c->codec_if);
  if (c->ctrl_if) audio_codec_delete_ctrl_if(c->ctrl_if);
  if (c->gpio_if) audio_codec_delete_gpio_if(c->gpio_if);
  if (c->data_if) audio_codec_delete_data_if(c->data_if);
  if (c->tx) {
    i2s_channel_disable(c->tx);
    i2s_del_channel(c->tx);
  }
}

static bool bring_up(chain *c) {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 3;
  chan_cfg.dma_frame_num = CHUNK_FRAMES;
  chan_cfg.auto_clear = true;
  if (i2s_new_channel(&chan_cfg, &c->tx, NULL) != ESP_OK) return false;
  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(TG_AUDIO_RATE),
    .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                  I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_SCLK, .ws = BSP_I2S_LCLK,
      .dout = BSP_I2S_DOUT, .din = I2S_GPIO_UNUSED,
    },
  };
  if (i2s_channel_init_std_mode(c->tx, &std_cfg) != ESP_OK) return false;
  if (i2s_channel_enable(c->tx) != ESP_OK) return false;

  audio_codec_i2s_cfg_t i2s_cfg = { .port = CONFIG_BSP_I2S_NUM, .tx_handle = c->tx };
  c->data_if = audio_codec_new_i2s_data(&i2s_cfg);
  audio_codec_i2c_cfg_t i2c_cfg = {
    .port = BSP_I2C_NUM, .addr = ES8311_CODEC_DEFAULT_ADDR,
    .bus_handle = bsp_i2c_get_handle(),
  };
  c->ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
  c->gpio_if = audio_codec_new_gpio();
  if (!c->data_if || !c->ctrl_if || !c->gpio_if) return false;

  es8311_codec_cfg_t es_cfg = {
    .ctrl_if = c->ctrl_if, .gpio_if = c->gpio_if,
    .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, .pa_pin = BSP_POWER_AMP_IO,
    .pa_reverted = false, .master_mode = false, .use_mclk = true,
    .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
  };
  c->codec_if = es8311_codec_new(&es_cfg);
  if (!c->codec_if) return false;
  esp_codec_dev_cfg_t dev_cfg = {
    .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = c->codec_if, .data_if = c->data_if,
  };
  c->dev = esp_codec_dev_new(&dev_cfg);
  if (!c->dev) return false;
  if (esp_codec_dev_set_out_vol(c->dev, TG_AUDIO_VOLUME) != ESP_CODEC_DEV_OK) return false;
  esp_codec_dev_sample_info_t fs = {
    .bits_per_sample = 16, .channel = 1, .sample_rate = TG_AUDIO_RATE,
  };
  if (esp_codec_dev_open(c->dev, &fs) != ESP_CODEC_DEV_OK) return false;
  c->opened = true;
  return true;
}

static void chime_task(void *arg) {
  tg_audio_cue cue = (tg_audio_cue)(intptr_t)arg;
  size_t before = dma_largest();
  tg_audio_outcome outcome = TG_AUDIO_OUTCOME_INIT_FAILED;
  if (!tg_audio_dma_ok(before)) {
    outcome = TG_AUDIO_OUTCOME_NO_MEMORY; /* en vägran, inte ett fel */
    ESP_LOGW(TAG, "ljud nekat: DMA-block %u B < %u B", (unsigned)before,
             (unsigned)TG_AUDIO_DMA_NEEDED);
  } else {
    chain c = {0};
    if (bring_up(&c)) {
      size_t during = dma_largest();
      int16_t buf[CHUNK_FRAMES];
      uint32_t off = 0, n;
      /* Låt förstärkaren vakna innan första tonen. */
      for (uint32_t i = 0; i < CHUNK_FRAMES; i++) buf[i] = 0;
      for (uint32_t s = 0; s < TG_AUDIO_LEAD_FRAMES; s += CHUNK_FRAMES)
        esp_codec_dev_write(c.dev, buf, (int)(CHUNK_FRAMES * sizeof buf[0]));
      while ((n = tg_audio_render(cue, off, buf, CHUNK_FRAMES, 100)) > 0) {
        esp_codec_dev_write(c.dev, buf, (int)(n * sizeof buf[0]));
        off += n;
      }
      /* Tystnad som täcker alla DMA-buffertar som ännu inte spelats, så att
       * nedrivningen inte klipper sista tonens nedtoning. */
      for (uint32_t i = 0; i < CHUNK_FRAMES; i++) buf[i] = 0;
      for (uint32_t s = 0; s < TG_AUDIO_TAIL_FRAMES; s += CHUNK_FRAMES)
        esp_codec_dev_write(c.dev, buf, (int)(CHUNK_FRAMES * sizeof buf[0]));
      ESP_LOGI(TAG, "spelade %s: DMA-block före %u, under %u B, stack kvar %u B",
               "DONE", (unsigned)before, (unsigned)during,
               (unsigned)uxTaskGetStackHighWaterMark(NULL));
      outcome = TG_AUDIO_OUTCOME_PLAYED;
    } else {
      ESP_LOGW(TAG, "ljudstart misslyckades; river ned");
    }
    teardown(&c);
    size_t after = dma_largest();
    if (after + 256u < before)
      ESP_LOGW(TAG, "möjlig ljudläcka: DMA-block %u -> %u B", (unsigned)before, (unsigned)after);
  }
  if (tg_audio_after_outcome(&s_failures, outcome)) {
    atomic_store(&s_disabled, true);
    ESP_LOGE(TAG, "ljudet avstängt till nästa boot efter %u fel i rad",
             (unsigned)TG_AUDIO_MAX_FAILURES);
  }
  atomic_store(&s_playing, false);
  vTaskDelete(NULL);
}

bool tg_audio_engine_start_cue(tg_audio_cue cue) {
  if (atomic_load(&s_disabled)) return false;
  bool expected = false;
  if (!atomic_compare_exchange_strong(&s_playing, &expected, true)) return false;
  if (xTaskCreate(chime_task, "chime", 4096, (void *)(intptr_t)cue, 4, NULL) != pdPASS) {
    atomic_store(&s_playing, false);
    return false;
  }
  return true;
}

bool tg_audio_engine_playing(void) { return atomic_load(&s_playing); }
bool tg_audio_engine_disabled(void) { return atomic_load(&s_disabled); }

#else /* ljudet är inte inbyggt */

bool tg_audio_engine_start_cue(tg_audio_cue cue) { (void)cue; return false; }
bool tg_audio_engine_playing(void) { return false; }
bool tg_audio_engine_disabled(void) { return false; }

#endif
