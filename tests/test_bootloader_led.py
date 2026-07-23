from __future__ import annotations

import subprocess
import textwrap
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).parents[1]
BOOT_SRC = ROOT / "firmware" / "exp045_bootloader_v2" / "src"
APP_SRC = ROOT / "firmware" / "exp066_research_platform_core" / "src"


def compile_and_run(source: str, *extra_sources: Path) -> None:
    with TemporaryDirectory() as tmpdir_name:
        tmpdir = Path(tmpdir_name)
        harness = tmpdir / "harness.c"
        binary = tmpdir / "harness"
        harness.write_text(textwrap.dedent(source), encoding="ascii")
        subprocess.run(
            [
                "gcc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(BOOT_SRC),
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


class BootloaderLedTests(unittest.TestCase):
    def test_bootloader_recovery_and_fatal_patterns_are_distinct(self):
        compile_and_run(
            r"""
            #include <stdint.h>
            #include "led_show.h"

            void delay_cycles(uint32_t cycles) { (void)cycles; }

            static int expect(uint32_t condition, int code)
            {
                return condition != 0U ? 0 : code;
            }

            int main(void)
            {
                if (expect(led_show_state_mask(BOOT_LED_STATE_BOOTING, 0U) == LED_SHOW_LED4, 1) != 0) return 1;
                if (expect(led_show_state_mask(BOOT_LED_STATE_VERIFYING, 0U) == LED_SHOW_LED2, 2) != 0) return 2;

                if (expect(led_show_state_mask(BOOT_LED_STATE_RECOVERY, 0U) == LED_SHOW_LED1, 3) != 0) return 3;
                if (expect(led_show_state_mask(BOOT_LED_STATE_RECOVERY, 1U) == 0U, 4) != 0) return 4;
                if (expect(led_show_state_mask(BOOT_LED_STATE_RECOVERY, 2U) == LED_SHOW_LED1, 5) != 0) return 5;
                if (expect(led_show_state_mask(BOOT_LED_STATE_RECOVERY, 3U) == 0U, 6) != 0) return 6;
                if (expect(led_show_state_duration_units(BOOT_LED_STATE_RECOVERY, 0U) == 1U, 7) != 0) return 7;
                if (expect(led_show_state_duration_units(BOOT_LED_STATE_RECOVERY, 1U) == 2U, 8) != 0) return 8;
                if (expect(led_show_state_duration_units(BOOT_LED_STATE_RECOVERY, 2U) == 1U, 9) != 0) return 9;
                if (expect(led_show_state_duration_units(BOOT_LED_STATE_RECOVERY, 3U) == 10U, 10) != 0) return 10;

                for (uint32_t i = 0U; i < 4U; ++i) {
                    if (expect((led_show_state_mask(BOOT_LED_STATE_RECOVERY, i) & ~LED_SHOW_LED1) == 0U, 11) != 0) return 11;
                }

                if (expect(led_show_state_mask(BOOT_LED_STATE_FATAL, 0U) == LED_SHOW_ALL, 12) != 0) return 12;
                if (expect(led_show_state_mask(BOOT_LED_STATE_FATAL, 1U) == 0U, 13) != 0) return 13;
                if (expect(led_show_state_mask(BOOT_LED_STATE_FATAL, 0U) != led_show_state_mask(BOOT_LED_STATE_RECOVERY, 0U), 14) != 0) return 14;
                return 0;
            }
            """,
            BOOT_SRC / "led_show.c",
        )

    def test_boot_sequence_turns_bootloader_leds_off_before_jump(self):
        source = (BOOT_SRC / "boot_sequence.c").read_text(encoding="ascii")
        assert '#include "led_show.h"' in source
        off_index = source.index("led_show_all_off();")
        jump_index = source.index("signed_image_jump(&selection.jump_context)")
        assert off_index < jump_index

    def test_led_code_does_not_use_dynamic_allocation(self):
        source = (
            (BOOT_SRC / "led_show.c").read_text(encoding="ascii")
            + (APP_SRC / "platform_health.c").read_text(encoding="ascii")
        )
        for token in ("malloc(", "calloc(", "realloc(", "free("):
            assert token not in source


if __name__ == "__main__":
    unittest.main()
