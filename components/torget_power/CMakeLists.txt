# Torgets kraftpaket. De rena policyfilerna (battery_policy, night_policy)
# värdtestas i test/run.sh och byggs i simulatorn; axp2101.c rör I2C och
# finns bara på targetet. PMU:n läses, aldrig konfigureras (spec 2026-09-24):
# enda skrivningen är att slå på VBAT-ADC:n.
idf_component_register(
  SRCS "battery_policy.c" "night_policy.c" "axp2101.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES esp_driver_i2c esp_timer)
