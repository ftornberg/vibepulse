# Torgets kraftpaket. De rena policyfilerna (battery_policy, night_policy,
# clock_policy) värdtestas i test/run.sh och byggs i simulatorn; axp2101.c
# och pcf85063.c rör I2C och finns bara på targetet. PMU:n läses, aldrig
# konfigureras (spec 2026-09-24): enda skrivningen är att slå på VBAT-ADC:n.
# RTC:ns enda konfigurationsskrivning är att nolla 12/24-biten.
idf_component_register(
  SRCS "battery_policy.c" "night_policy.c" "clock_policy.c" "axp2101.c" "pcf85063.c"
  INCLUDE_DIRS "."
  REQUIRES esp_driver_i2c
  PRIV_REQUIRES esp_timer)
