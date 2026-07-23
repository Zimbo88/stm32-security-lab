from __future__ import annotations

import subprocess
import textwrap
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).parents[1]
SRC = ROOT / "firmware" / "exp066_research_platform_core" / "src"


def compile_and_run(source: str, *extra_sources: Path) -> None:
    with TemporaryDirectory() as tmpdir_name:
        tmpdir = Path(tmpdir_name)
        harness = tmpdir / "harness.c"
        binary = tmpdir / "harness"
        harness.write_text(textwrap.dedent(source))
        subprocess.run(
            [
                "gcc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(SRC),
                *(str(path) for path in extra_sources),
                str(harness),
                "-o",
                str(binary),
            ],
            check=True,
            text=True,
            capture_output=True,
        )
        subprocess.run([str(binary)], check=True, text=True, capture_output=True)


class Exp071HealthTests(unittest.TestCase):
    def test_health_state_priority_patterns_and_temporary_restore(self):
        compile_and_run(
            r"""
            #include <stdint.h>
            #include "platform_health.h"

            static uint8_t writes[256];
            static uint32_t write_count;

            void platform_led_init(void) { write_count = 0U; }
            void platform_led_write(uint8_t mask)
            {
                if (write_count < (uint32_t)(sizeof(writes) / sizeof(writes[0]))) {
                    writes[write_count] = mask;
                }
                ++write_count;
            }

            static int expect(uint32_t condition, int code)
            {
                return condition != 0U ? 0 : code;
            }

            static void reset_writes(void)
            {
                for (uint32_t i = 0U; i < (uint32_t)(sizeof(writes) / sizeof(writes[0])); ++i) {
                    writes[i] = 0U;
                }
                write_count = 0U;
            }

            int main(void)
            {
                uint8_t degraded[8];
                uint8_t fault[8];
                uint8_t security[8];
                int different = 0;

                platform_health_init();
                platform_health_apply_boot_policy(0U, 0U, 1U, 0U);
                if (expect(platform_health_get_automatic_state() == PLATFORM_HEALTHY, 1) != 0) return 1;

                reset_writes();
                for (uint32_t i = 0U; i < 6U; ++i) {
                    platform_health_service_tick();
                }
                if (expect(writes[0] == 0x08U, 2) != 0) return 2;
                for (uint32_t i = 1U; i < 6U; ++i) {
                    if (expect(writes[i] == 0U, 3) != 0) return 3;
                }
                if (expect((platform_health_get_led_mask() & 0x07U) == 0U, 4) != 0) return 4;

                platform_health_init();
                platform_health_apply_boot_policy(0U, 0U, 1U, 0U);
                reset_writes();
                for (uint32_t i = 0U; i < 11U; ++i) {
                    platform_health_service_tick();
                }
                if (expect(writes[0] == 0x08U, 5) != 0) return 5;
                for (uint32_t i = 1U; i < 10U; ++i) {
                    if (expect(writes[i] == 0U, 6) != 0) return 6;
                }
                if (expect(writes[10] == 0x08U, 7) != 0) return 7;

                platform_health_set_state(PLATFORM_DEGRADED);
                reset_writes();
                for (uint32_t i = 0U; i < 8U; ++i) {
                    platform_health_service_tick();
                    degraded[i] = platform_health_get_led_mask();
                }

                platform_health_init();
                platform_health_apply_boot_policy(0U, 0U, 1U, 0U);
                platform_health_report_fault();
                platform_health_set_state(PLATFORM_HEALTHY);
                if (expect(platform_health_get_state() == PLATFORM_FAULT, 8) != 0) return 8;
                reset_writes();
                for (uint32_t i = 0U; i < 8U; ++i) {
                    platform_health_service_tick();
                    fault[i] = platform_health_get_led_mask();
                }

                platform_health_init();
                platform_health_apply_boot_policy(0U, 0U, 1U, 0U);
                platform_health_report_security_failure();
                platform_health_set_state(PLATFORM_HEALTHY);
                if (expect(platform_health_get_state() == PLATFORM_SECURITY_FAILURE, 9) != 0) return 9;
                reset_writes();
                for (uint32_t i = 0U; i < 8U; ++i) {
                    platform_health_service_tick();
                    security[i] = platform_health_get_led_mask();
                }

                for (uint32_t i = 0U; i < 8U; ++i) {
                    if (degraded[i] != fault[i] || fault[i] != security[i]) {
                        different = 1;
                    }
                }
                if (expect((uint32_t)different, 10) != 0) return 10;

                platform_health_init();
                platform_health_apply_boot_policy(0U, 0U, 1U, 0U);
                platform_health_start_led_test(PLATFORM_FAULT);
                if (expect(platform_health_temporary_active() != 0U, 11) != 0) return 11;
                for (uint32_t i = 0U; i < PLATFORM_HEALTH_LED_TEST_TICKS; ++i) {
                    platform_health_service_tick();
                }
                if (expect(platform_health_temporary_active() == 0U, 12) != 0) return 12;
                if (expect(platform_health_get_state() == PLATFORM_HEALTHY, 13) != 0) return 13;

                platform_health_start_easteregg_knightrider();
                for (uint32_t i = 0U; i < PLATFORM_HEALTH_EASTER_EGG_TICKS; ++i) {
                    platform_health_service_tick();
                }
                if (expect(platform_health_temporary_active() == 0U, 14) != 0) return 14;
                if (expect(platform_health_get_state() == PLATFORM_HEALTHY, 15) != 0) return 15;
                return 0;
            }
            """,
            SRC / "platform_health.c",
        )

    def test_reset_fault_policy_and_audio_disabled_boundary(self):
        compile_and_run(
            r"""
            #include <stdint.h>
            #include "platform_audio.h"
            #include "platform_health.h"

            void platform_led_init(void) {}
            void platform_led_write(uint8_t mask) { (void)mask; }

            int main(void)
            {
                if (platform_health_evaluate_boot_policy(0U, 0U, 1U, 0U) != PLATFORM_HEALTHY) return 1;
                if (
                    platform_health_evaluate_boot_policy(PLATFORM_RESET_IWDG_FLAG, 0U, 1U, 0U) !=
                    PLATFORM_DEGRADED
                ) return 2;
                if (platform_health_evaluate_boot_policy(0U, 1U, 1U, 0U) != PLATFORM_DEGRADED) return 3;
                if (platform_health_evaluate_boot_policy(0U, 0U, 0U, 0U) != PLATFORM_FAULT) return 4;
                if (
                    platform_health_evaluate_boot_policy(0U, 0U, 1U, 1U) !=
                    PLATFORM_SECURITY_FAILURE
                ) return 5;
                if (platform_audio_available() != 0U) return 6;
                if (platform_audio_start_retro() != 0U) return 7;
                platform_audio_stop();
                return 0;
            }
            """,
            SRC / "platform_health.c",
            SRC / "platform_audio.c",
        )


if __name__ == "__main__":
    unittest.main()
