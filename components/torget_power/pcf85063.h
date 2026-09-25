#ifndef TORGET_POWER_PCF85063_H
#define TORGET_POWER_PCF85063_H

#include <stdbool.h>

#include "clock_policy.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * PCF85063ATL på I2C 0x51 (spec/hardware.md). Bara tidsregistren 0x04..0x0A
 * läses och skrivs, i UTC. Enda konfigurationsskrivningen är att nolla
 * 12/24-biten i Control_1 om den råkar vara satt: BCD-avkodningen nedan
 * förutsätter 24-timmarsläge. Larm, timer och avbrott rörs inte (del B
 * har inget schemalagt väckande, spec 2026-09-24).
 */
esp_err_t tg_pcf85063_init(i2c_master_bus_handle_t bus);
esp_err_t tg_pcf85063_read(tg_civil *out, bool *os_flag);
esp_err_t tg_pcf85063_write(const tg_civil *utc);

#endif
