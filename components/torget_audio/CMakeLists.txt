# Ljudmotorn: lånar I2S och ES8311 per signal (spec 2026-10-02). INGEN egen
# idf_component.yml — ett manifest löser om dependencies.lock vid varje bygge.
# Kodek och I2S kommer ur grafen som BSP:n redan drar in.
idf_component_register(
  SRCS "audio_policy.c" "audio.c"
  INCLUDE_DIRS "."
  REQUIRES torget_app
  PRIV_INCLUDE_DIRS "../.."
  PRIV_REQUIRES esp_driver_i2s espressif__esp_codec_dev
                waveshare__esp32_s3_touch_amoled_2_16)
