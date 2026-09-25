#!/usr/bin/env python3
"""Guards for the TID app: opt-in, 2.16 only, and tap semantics.

The app must never appear unasked (a fresh clone builds exactly one app), must
not be built for the 2.41 V2 (its rotation is fixed landscape), and must use
SHORT_CLICKED for taps: LVGL also sends CLICKED after a long press, so a long
press that opens the launcher would otherwise also toggle a timer."""

from pathlib import Path


root = Path(__file__).resolve().parents[1]

sim = (root / "sim/CMakeLists.txt").read_text(encoding="utf-8")
assert 'option(TORGET_WITH_TIME' in sim and "OFF)" in sim.split(
    "option(TORGET_WITH_TIME", 1)[1].split("\n", 1)[0], (
    "TID must be an explicit simulator option, default OFF"
)
assert 'TORGET_BOARD STREQUAL "waveshare_216"' in sim, (
    "the simulator must not register TID on the 2.41 V2 profile"
)
assert "TORGET_HAVE_TIME" in sim

registry = (root / "main/registry.c").read_text(encoding="utf-8")
assert "#ifdef TORGET_HAVE_TIME" in registry and "&time_app" in registry, (
    "registry.c must gate the TID entry on TORGET_HAVE_TIME"
)

views = (root / "components/app_time/time_views.c").read_text(encoding="utf-8")
assert "LV_EVENT_SHORT_CLICKED" in views
assert "LV_EVENT_CLICKED" not in views.replace("LV_EVENT_SHORT_CLICKED", ""), (
    "taps must be SHORT_CLICKED, never CLICKED (a long press also clicks)"
)
assert "LV_EVENT_LONG_PRESSED" in views and "torget_launcher_open" in views
assert "lv_obj_set_style_transform" not in views
assert "lv_obj_set_style_opa" not in views
assert "lv_canvas" not in views

app = (root / "components/app_time/app_time.c").read_text(encoding="utf-8")
assert "torget_keep_awake" not in app, "TID never holds the panel awake"
assert "torget_net_wait" not in app, "TID has no network task"

core_h = (root / "components/app_time/time_core.h").read_text(encoding="utf-8")
no_value = [l for l in core_h.splitlines() if l.startswith("#define TG_TIME_NO_VALUE ")]
assert len(no_value) == 1, "time_core.h must define TG_TIME_NO_VALUE exactly once"
assert "-" not in no_value[0], (
    "plex_num_118 has no ASCII hyphen (0x2D); the no-value placeholder must use "
    "the en dash U+2013 the font carries"
)

root_cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
assert 'option(TORGET_WITH_TIME' in root_cmake and "OFF)" in root_cmake.split(
    "option(TORGET_WITH_TIME", 1)[1].split("\n", 1)[0], (
    "TID must be an explicit firmware option, default OFF"
)
assert 'TORGET_WITH_TIME AND TORGET_BOARD STREQUAL "waveshare_216"' in root_cmake, (
    "the firmware must not build TID for the 2.41 V2"
)
assert 'set(ENV{TORGET_APP_TIME} "")' in root_cmake, (
    "an unselected TID must mirror an EMPTY env value so main/ never sees a "
    "stale shell export (ESP-IDF expands main/CMakeLists.txt twice)"
)
main_cmake = (root / "main/CMakeLists.txt").read_text(encoding="utf-8")
assert '"$ENV{TORGET_APP_TIME}" STREQUAL "1"' in main_cmake
assert "app_time" in main_cmake and "TORGET_HAVE_TIME" in main_cmake

# A component manifest changes dependencies.lock's manifest_hash, so EVERY firmware
# build (also the default one) would re-solve the lock and bump unrelated drivers
# (final review: espressif/esp_lcd_co5300 2.1.0 -> 2.2.0), dirty the tree and make
# the OTA sender refuse the build. app_time gets LVGL from the graph instead.
assert not (root / "components/app_time/idf_component.yml").exists(), (
    "components/app_time must not carry an idf_component.yml (it re-solves "
    "dependencies.lock); require lvgl__lvgl in its CMakeLists instead"
)
app_cmake = (root / "components/app_time/CMakeLists.txt").read_text(encoding="utf-8")
assert "lvgl__lvgl" in app_cmake

# The rotation constants are plain #defines in time_core.h; nothing forwards a
# -D from idf.py/CMake to the compiler, so the docs must not promise it.
docs = (root / "docs/time-app.md").read_text(encoding="utf-8")
header = (root / "components/app_time/time_core.h").read_text(encoding="utf-8")
assert "pass `-D`" not in docs and "-D vid bygget" not in header, (
    "the TG_TIME_ROT_* override is a source edit, not a build flag"
)

print("OK: TID is opt-in, 2.16-only and taps are short clicks")
