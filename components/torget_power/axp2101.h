#ifndef TORGET_POWER_AXP2101_H
#define TORGET_POWER_AXP2101_H

#include "battery_policy.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * AXP2101 på I2C 0x34: LÄSES, konfigureras inte. Laddström, slutspänning
 * och termik är fabriksvärden tills en verifierad ändring finns
 * (spec/hardware-capabilities.yaml, power.axp2101). Enda skrivningen är
 * ADC-kanalen för batterispänning, en mätinställning.
 */
esp_err_t tg_axp2101_init(i2c_master_bus_handle_t bus);
tg_batt_sample tg_axp2101_read(void);

#endif
