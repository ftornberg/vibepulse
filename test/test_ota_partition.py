#!/usr/bin/env python3
"""Partition-budget gate: two equal OTA slots, no factory, 4 MiB image cap."""

import csv
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
rows = []
for raw in (root / "partitions.csv").read_text().splitlines():
    raw = raw.strip()
    if raw and not raw.startswith("#"):
        rows.append(next(csv.reader([raw], skipinitialspace=True)))

parts = {row[0].strip(): row for row in rows}
assert "factory" not in parts, "normal updates must not target a factory slot"
assert {"nvs", "otadata", "phy_init", "ota_0", "ota_1"} <= set(parts)

def size(text):
    match = re.fullmatch(r"(\d+)([KkMm]?)", text.strip())
    assert match, text
    value = int(match.group(1))
    return value * {"": 1, "k": 1024, "m": 1024 * 1024}[match.group(2).lower()]

slot0 = size(parts["ota_0"][4])
slot1 = size(parts["ota_1"][4])
assert slot0 == slot1 == 5 * 1024 * 1024
assert parts["ota_0"][2].strip() == "ota_0"
assert parts["ota_1"][2].strip() == "ota_1"
assert size(parts["otadata"][4]) == 8 * 1024
maximum_permitted = 4 * 1024 * 1024
assert maximum_permitted <= slot0
binary = root / "build/torget.bin"
if binary.exists():
    assert binary.stat().st_size <= maximum_permitted
print("OK: two 5 MiB OTA slots; 4 MiB maximum image gate")

# --- Task 5: boot-health adapter wiring (source-level, no ESP-IDF needed) ---

main_c = (root / "main/main.c").read_text(encoding="utf-8")
adapter_path = root / "components/torget_ota/boot_health.c"
assert adapter_path.exists(), "boot_health.c must exist in torget_ota"
adapter = adapter_path.read_text(encoding="utf-8")

# The health gate only means something if the target actually starts it and
# feeds it the local evidence bits at the right places in the boot order.
assert "torget_boot_health_start();" in main_c, (
    "app_main must start the boot-health gate after NVS init"
)
assert "torget_boot_health_mark(TG_HEALTH_DISPLAY);" in main_c, (
    "display evidence must be marked after display_start()"
)
assert "torget_boot_health_mark(TG_HEALTH_UI);" in main_c, (
    "UI evidence must be marked after torget_ui_create()"
)
assert "torget_boot_health_mark(TG_HEALTH_SCHEDULER);" in main_c, (
    "scheduler evidence must be marked from the first tick_cb"
)

# Accept and rollback must both go through the official rollback API; anything
# else leaves otadata in a state the bootloader does not understand.
assert "esp_ota_mark_app_valid_cancel_rollback" in adapter, (
    "the adapter must accept only through esp_ota_mark_app_valid_cancel_rollback"
)
assert "esp_ota_mark_app_invalid_rollback_and_reboot" in adapter, (
    "the adapter must roll back through esp_ota_mark_app_invalid_rollback_and_reboot"
)
assert "ESP_OTA_IMG_PENDING_VERIFY" in adapter, (
    "policy polling must be enabled only for a pending-verify image"
)
assert "ESP_OTA_IMG_ABORTED" in adapter, (
    "a stable boot must record bootloader crash-rollback evidence from the other slot"
)
assert '"rollback_from"' in adapter and '"torget_health"' in adapter, (
    "rollback evidence must land in the bounded NVS key rollback_from"
)

# Component wiring: the adapter must be compiled into the target build and
# main must be allowed to include boot_health.h.
ota_cmake = (root / "components/torget_ota/CMakeLists.txt").read_text(encoding="utf-8")
main_cmake = (root / "main/CMakeLists.txt").read_text(encoding="utf-8")
assert '"boot_health.c"' in ota_cmake, "torget_ota must compile boot_health.c"
assert "torget_ota" in main_cmake, "main must require torget_ota"

# The deliberate failure injection stays a build-time development switch,
# mapped to forced_failure only for a pending image.
kconfig = (root / "main/Kconfig.projbuild").read_text(encoding="utf-8")
assert "TORGET_BOOT_HEALTH_FORCE_FAIL" in kconfig
assert "CONFIG_TORGET_BOOT_HEALTH_FORCE_FAIL" in adapter

print("OK: boot-health adapter is wired into the target boot order")

# --- Task 6: maintenance overlay safety (must not alter app layouts) -------

ota_ui_path = root / "components/torget_ota/ota_ui.c"
assert ota_ui_path.exists(), "ota_ui.c must exist in torget_ota"
ota_ui = ota_ui_path.read_text(encoding="utf-8")

# The overlay lives on LVGL's top layer, never inside the app tree: that is
# the structural guarantee that no existing app layout can change.
assert "lv_layer_top()" in ota_ui, "overlay must be created on lv_layer_top()"
assert "lv_screen_active" not in ota_ui, (
    "the overlay must never touch the screen the apps live on"
)
# Frysläxan 2026-08-14: overlayn tar låset TIDSBEGRÄNSAT och hoppar hellre
# över en bildruta än blockerar — ett evigt låsförsök från OTA-tasken var
# exakt så hela panelen frös. Oändlig väntan är förbjuden här.
assert "torget_ui_try_lock(200)" in ota_ui and "torget_ui_unlock()" in ota_ui, (
    "widget updates must take the shared UI lock with a bounded timeout"
)
assert "torget_ui_lock()" not in ota_ui, (
    "the OTA overlay must never wait forever on the UI lock"
)

# Native fonts only. plex_text_32 (kr/%/GWh glyphs) and plex_num_50 (no
# colon) cannot spell the state words or a countdown, so the overlay uses
# the native fonts that do cover them; scaling or transforms stay banned.
for font in ("plex_attention_52", "plex_num_50", "plex_ui_21"):
    assert f"extern const lv_font_t {font};" in ota_ui, (
        f"overlay must use the native font {font}"
    )
assert "lv_color_black()" in ota_ui, "overlay background must be true black"

# Same private-heap rules as the app screens: no transform layers, no canvas
# buffers, no opacity layers, no animation.
assert "lv_obj_set_style_transform" not in ota_ui
assert "lv_canvas" not in ota_ui
assert "lv_obj_set_style_opa" not in ota_ui
assert "lv_anim_start" not in ota_ui and "LV_ANIM_ON" not in ota_ui

# Wiring: created once, after the shared UI, while the lock is held.
assert '"ota_ui.c"' in ota_cmake, "torget_ota must compile ota_ui.c"
assert re.search(
    r"torget_ui_create\(\);.*?torget_ota_ui_create\(\);.*?torget_ui_unlock\(\);",
    main_c,
    re.DOTALL,
), "overlay must be created after torget_ui_create() under the held UI lock"

print("OK: OTA overlay stays on the top layer and off the app layouts")

# --- Task 7: authenticated inactive-slot streaming service ----------------

service_path = root / "components/torget_ota/ota_service.c"
assert service_path.exists(), "ota_service.c must exist in torget_ota"
service = service_path.read_text(encoding="utf-8")
service_h = (root / "components/torget_ota/ota_service.h").read_text(
    encoding="utf-8"
)

# The whole esp_ota lifecycle must be present: begin/write/end for the
# stream, abort for every failure path, and slot selection only at the end.
for symbol in (
    "esp_ota_get_next_update_partition",
    "esp_ota_begin",
    "esp_ota_write",
    "esp_ota_end",
    "esp_ota_set_boot_partition",
    "esp_ota_abort",
):
    assert symbol in service, f"ota_service.c must use {symbol}"

# The digest is computed incrementally while streaming — never by buffering
# the whole image (there is no RAM for that).
for symbol in (
    "mbedtls_sha256_starts",
    "mbedtls_sha256_update",
    "mbedtls_sha256_finish",
):
    assert symbol in service, f"ota_service.c must use incremental {symbol}"

# Token comparison must be constant-time, and the Authorization header must
# never reach a log line.
assert "constant_time_equal" in service, (
    "token comparison must go through the constant-time helper"
)
assert "strcmp(authorization" not in service and (
    "strncmp(authorization, \"Bearer \"" in service
), "the bearer prefix check is the only string scan allowed on the header"
for line in service.splitlines():
    if "ESP_LOG" in line:
        assert "authorization" not in line.lower(), (
            "the Authorization header must never be logged"
        )
        assert "TG_OTA_TOKEN" not in line, "the token must never be logged"

# The pure policy stays the gatekeeper: the HTTP layer maps its request and
# asks before esp_ota_begin may touch flash.
assert "tg_ota_request_check" in service
assert service.index("tg_ota_request_check") < service.index("esp_ota_begin"), (
    "the policy verdict must come before esp_ota_begin in the handler flow"
)
assert "TG_OTA_MAX_IMAGE_BYTES" in service, (
    "the status endpoint shares the policy's size cap"
)

# Metadata gate on the first chunk: image magic, chip id, app-desc magic and
# project name, straight from esp_app_format.h.
for symbol in (
    "ESP_IMAGE_HEADER_MAGIC",
    "ESP_CHIP_ID_ESP32S3",
    "ESP_APP_DESC_MAGIC_WORD",
    '"torget"',
):
    assert symbol in service, f"first-chunk validation must check {symbol}"

# HTTP boundary contract.
assert "#define TG_OTA_HTTP_PORT 80" in service_h
assert '"/api/ota/status"' in service and '"/api/ota/firmware"' in service
assert "httpd_req_recv" in service and "4096" in service

# secrets.h.example documents the development token contract.
example = (root / "secrets.h.example").read_text(encoding="utf-8")
assert "TG_OTA_TOKEN" in example, "secrets.h.example must describe TG_OTA_TOKEN"
assert 'sizeof(TG_OTA_TOKEN) == 65' in service, (
    "the 64-hex-character token length must be locked by _Static_assert"
)

# Wiring: compiled into the component, KEY3 routed through the pure button
# policy, and the listener started right after wifi_start().
assert '"ota_service.c"' in ota_cmake
assert "esp_http_server" in ota_cmake and "mbedtls" in ota_cmake
main_c = (root / "main/main.c").read_text(encoding="utf-8")
assert "tg_button_update" in main_c, "KEY3 must go through tg_button_update"
assert "key3_was_down" not in main_c, (
    "the raw KEY3 edge check must be replaced by the button policy"
)
assert re.search(r"tg_notice_glass\(&notice, torget_ota_ui_notice_visible\(\),\s*torget_ota_ui_visible\(\), busy\)", service), (
    "the takeover must be reconciled against the glass every poll, never "
    "driven by a one-shot SHOW/HIDE edge (dead pills 2026-10-07)"
)
assert "torget_ota_service_open_maintenance" in main_c
# Nödutgången: ett kort tryck medan fönstret är öppet stänger det. Tio
# minuter total svart låda utan flyktväg gjorde en frisk enhet omöjlig att
# skilja från en hängd (2026-08-14).
assert "torget_ota_service_close_maintenance" in main_c, (
    "a short KEY3 press while the window is open must close it"
)
# Bootordningen efter frysläxan: nättasken (apparnas kritiska bana) skapas
# FÖRE OTA-vakten, och vid boot startas ingen httpd alls — servern föds
# lat i vaktens task när fönstret öppnas och dör när det stängs.
assert re.search(
    r"wifi_start\(\);(.(?!torget_ota_service_start))*?torget-net(.(?!httpd_start))*?torget_ota_service_start\(\);",
    main_c,
    re.DOTALL,
), "boot order: wifi, then torget-net, then the lazy OTA watcher — no httpd"

# Den lata ytan i siffror: httpd startas och stoppas ENBART av vakten, och
# serverns sockelbudget kan aldrig svälja apparnas (lwip har 10 totalt).
watcher_start = service[service.index("static void httpd_surface_start"):]
watcher_start = watcher_start[:watcher_start.index("void torget_ota_service_start")]
assert "httpd_start" in watcher_start and "max_open_sockets = 2" in watcher_start
service_boot = service[service.index("void torget_ota_service_start"):]
assert "httpd_start" not in service_boot, (
    "boot must not start httpd — the surface is born when the window opens"
)
assert "httpd_surface_stop" in service, "closing the window must stop httpd"
assert "pdPASS" in service_boot, "the watcher xTaskCreate must be checked"

# Väpningen och anti-brick-vakten är kontrakt, inte detaljer.
button_h = (root / "components/torget_ota/button_policy.h").read_text(encoding="utf-8")
assert "bool armed;" in button_h, "the button policy must carry the arming flag"
health = (root / "components/torget_ota/boot_health.c").read_text(encoding="utf-8")
assert "esp_ota_check_rollback_is_possible" in health, (
    "rolling back into a blank slot bricks the unit — the verdict must be guarded"
)

print("OK: OTA service streams to the inactive slot behind token and policy")

# Frysläxan 2026-08-14: en task som gör LVGL-arbete under UI-låset och dör
# tar hela glaset med sig. ota-ui-tasken renderar text — den får aldrig
# krympas tillbaka till en stack som textmotorn kan äta upp.
service_src = (root / "components/torget_ota/ota_service.c").read_text(
    encoding="utf-8")
assert '"ota-ui", 8192' in service_src, \
    "ota-ui task needs 8 KiB: LVGL text layout under the UI lock overflowed 3 KiB on the panel"

print("OK: OTA UI task stack survives LVGL text layout under the lock")
