#ifndef TORGET_EXT_RAM_H
#define TORGET_EXT_RAM_H

/*
 * EXT_RAM_BSS_ATTR: lägg en nollställd statisk variabel i PSRAM i stället
 * för i internminnet. Internminnet (256 KB heap) är panelens knappa resurs —
 * skärmflushen, WiFi och ljudet behöver det, PSRAM (8 MB) står nästan tomt
 * (minnesförstudien 2026-10-08, docs/observability-backlog.md OBS-47).
 *
 * Bara för ren CPU-data: HTTP-kroppar, tolkade nyttolaster, UI-tillstånd.
 * ALDRIG på något som läses eller skrivs medan flash skrivs (OTA:ns chunk,
 * WiFi-uppgifternas NVS-väg) eller från en ISR: under en flash-skrivning är
 * cachen av och PSRAM onåbar för tasken som skriver.
 *
 * På målet kräver attributet CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY;
 * utan det blir esp_attr.h:s makro TOMT och variabeln hamnar tyst i
 * internminnet igen. Därför #error här och en CMake-vakt i roten. På värd
 * och i simulatorn är attributet tomt.
 */
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_attr.h"
#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#error "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY saknas: sdkconfig är gammal, kör idf.py reconfigure (se sdkconfig.defaults)"
#endif
#else
#define EXT_RAM_BSS_ATTR
#endif

#endif
