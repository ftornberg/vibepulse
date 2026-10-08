# Internal-RAM headroom (memory study 2026-10-08, OBS-47) rests on two
# Kconfig values that sdkconfig.defaults seeds but never migrates into an
# existing generated sdkconfig (docs/lessons.md, 2026-08-19):
#  - SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY: without it esp_attr.h defines
#    EXT_RAM_BSS_ATTR as nothing, and ~35 KB of buffers land back in internal
#    RAM with no warning; platform/ext_ram.h also #errors, this guard says why.
#  - FREERTOS_USE_TRACE_FACILITY: uxTaskGetSystemState, the once-a-minute
#    stack line that every stack decision after the study is based on.
#
# Each argument is the effective CONFIG_* value as CMake sees it after the
# IDF project() call ("y" when set, "" when unset or =n).
function(torget_require_memory_headroom bss_in_psram trace_facility)
  set(_missing "")
  if(NOT "${bss_in_psram}" STREQUAL "y")
    list(APPEND _missing "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y")
  endif()
  if(NOT "${trace_facility}" STREQUAL "y")
    list(APPEND _missing "CONFIG_FREERTOS_USE_TRACE_FACILITY=y")
  endif()
  if(_missing)
    string(REPLACE ";" " " _missing_text "${_missing}")
    message(FATAL_ERROR
      "Memory headroom config is missing: ${_missing_text}. "
      "The generated sdkconfig is stale: add the line(s) to sdkconfig "
      "(matching sdkconfig.defaults), then run: "
      "idf.py reconfigure && idf.py build")
  endif()
endfunction()
