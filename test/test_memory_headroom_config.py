#!/usr/bin/env python3
"""Internal-RAM headroom (memory study 2026-10-08) must not erode silently.

EXT_RAM_BSS_ATTR is defined as NOTHING by esp_attr.h when
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY is off, so a stale sdkconfig would
put ~35 KB of buffers back in internal RAM and every test would still pass.
Same medicine as the LVGL pool guard: the value is a build invariant."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GUARD = ROOT / "cmake" / "torget_memory_guard.cmake"

# The buffers the study moved, and the declaration each must keep carrying
# the attribute on. Pure CPU data only: nothing here is touched while flash
# is written or from an ISR.
MOVED = {
    "components/app_tokens/net.c": [
        r"static EXT_RAM_BSS_ATTR char body\[BODY_MAX\]",
        r"static EXT_RAM_BSS_ATTR char body\[MT_BODY_MAX\]",
    ],
    "components/app_tokens/agent_monitor.c": [
        r"static EXT_RAM_BSS_ATTR struct \{[^}]*\} mon;",
    ],
    "components/app_tokens/usage_screen.c": [
        r"static EXT_RAM_BSS_ATTR struct \{[^}]*\} ui;",
    ],
    "components/app_tokens/merge_queue_net.c": [
        r"static EXT_RAM_BSS_ATTR char body\[MERGE_BODY_MAX\]",
        r"static EXT_RAM_BSS_ATTR tk_merge_queue queue;",
    ],
    "components/app_tokens/merge_queue_parse.c": [
        r"static EXT_RAM_BSS_ATTR tk_merge_queue parsed;",
    ],
    "components/app_tokens/agent_net.c": [
        r"static EXT_RAM_BSS_ATTR tk_agent_http_response response;",
    ],
    "components/app_tokens/github_net.c": [
        r"static EXT_RAM_BSS_ATTR char body\[GITHUB_BODY_MAX\]",
    ],
}
# Flash-adjacent code stays in internal RAM: the OTA chunk feeds
# esp_ota_write, the Wi-Fi setup flow writes credentials to NVS.
STAY_INTERNAL = [
    "components/torget_ota/ota_service.c",
    "components/torget_wifi/wifi_setup.c",
    "components/torget_wifi/wifi_creds.c",
]


def run_guard(bss: str, trace: str) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory() as tmp:
        script = Path(tmp) / "check.cmake"
        script.write_text(
            f'include("{GUARD.as_posix()}")\n'
            f'torget_require_memory_headroom("{bss}" "{trace}")\n',
            encoding="utf-8",
        )
        return subprocess.run(["cmake", "-P", str(script)],
                              capture_output=True, check=False, text=True)


class MemoryHeadroomConfigTests(unittest.TestCase):
    def test_intended_config_is_accepted(self) -> None:
        result = run_guard("y", "y")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_stale_config_without_psram_bss_is_rejected(self) -> None:
        result = run_guard("", "y")
        self.assertNotEqual(result.returncode, 0)
        diagnostic = result.stdout + result.stderr
        self.assertIn("CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y", diagnostic)
        self.assertIn("idf.py reconfigure && idf.py build", diagnostic)

    def test_stale_config_without_trace_facility_is_rejected(self) -> None:
        result = run_guard("y", "")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CONFIG_FREERTOS_USE_TRACE_FACILITY=y",
                      result.stdout + result.stderr)

    def test_root_build_invokes_the_guard(self) -> None:
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('cmake/torget_memory_guard.cmake")', root_cmake)
        self.assertIn("torget_require_memory_headroom(", root_cmake)
        self.assertIn('"${CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY}"', root_cmake)
        self.assertIn('"${CONFIG_FREERTOS_USE_TRACE_FACILITY}"', root_cmake)

    def test_checked_in_defaults_carry_both_values(self) -> None:
        defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8").splitlines()
        self.assertIn("CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y", defaults)
        self.assertIn("CONFIG_FREERTOS_USE_TRACE_FACILITY=y", defaults)

    def test_the_moved_buffers_keep_their_attribute(self) -> None:
        for rel, patterns in MOVED.items():
            source = (ROOT / rel).read_text(encoding="utf-8")
            for pattern in patterns:
                with self.subTest(file=rel, decl=pattern):
                    self.assertRegex(source, pattern)

    def test_flash_adjacent_code_stays_internal(self) -> None:
        for rel in STAY_INTERNAL:
            with self.subTest(file=rel):
                self.assertNotIn("EXT_RAM_BSS_ATTR",
                                 (ROOT / rel).read_text(encoding="utf-8"))

    def test_the_header_errors_on_target_and_is_empty_on_host(self) -> None:
        header = (ROOT / "platform" / "ext_ram.h").read_text(encoding="utf-8")
        self.assertIn("#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY", header)
        self.assertIn("#error", header)
        self.assertRegex(header, r"#else\s*\n#define EXT_RAM_BSS_ATTR\s*\n")

    def test_main_logs_the_stack_line(self) -> None:
        main_c = (ROOT / "main" / "main.c").read_text(encoding="utf-8")
        self.assertIn("uxTaskGetSystemState(", main_c)
        self.assertIn("stackar kvar (B, lägst först)", main_c)
        self.assertIn("LÅG STACK", main_c)


if __name__ == "__main__":
    unittest.main(verbosity=2)
