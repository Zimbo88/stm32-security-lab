import unittest
from pathlib import Path

ROOT = Path(__file__).parents[1]
PLATFORM_C = ROOT / "firmware" / "exp066_research_platform_core" / "src" / "platform.c"

ALLOWED_TEST_COMMANDS = {
    "test run gpio",
    "test run button",
    "test run clock",
    "test run ram",
}

UNAVAILABLE_REGISTER_GROUPS = {
    "registers nvic",
    "registers systick",
    "registers mpu",
    "registers flash",
    "registers pwr",
    "registers syscfg",
}


def tokenize(line, limit=4):
    words = line.split()
    return words if len(words) <= limit else None

def is_allowed_test_command(command):
    return command in ALLOWED_TEST_COMMANDS

def crc32(data):
    c = 0xffffffff
    for x in data:
        c ^= x
        for _ in range(8):
            c = (c >> 1) ^ (0xedb88320 if c & 1 else 0)
    return c ^ 0xffffffff

class Exp066Tests(unittest.TestCase):
    def test_tokenizer_bounds(self):
        self.assertEqual(tokenize("test run ram"), ["test", "run", "ram"])
        self.assertIsNone(tokenize("a b c d e"))
    def test_command_rejection(self):
        self.assertNotEqual(tokenize("memory peek 0x20000000"), ["memory", "peek"])
    def test_test_command_allowlist_is_exact(self):
        for command in ALLOWED_TEST_COMMANDS:
            self.assertTrue(is_allowed_test_command(command))
        for command in ("test", "test run", "test run flash", "test anything"):
            self.assertFalse(is_allowed_test_command(command))
    def test_crc(self):
        self.assertEqual(crc32(b"123456789"), 0xcbf43926)
    def test_ring_wrap_model(self):
        slots = [None] * 4
        for i in range(6): slots[i % 4] = i
        self.assertEqual(slots, [4, 5, 2, 3])
    def test_firmware_source_uses_exact_test_allowlist(self):
        source = PLATFORM_C.read_text()
        self.assertNotIn("s[0]=='t'", source)
        self.assertNotIn('s[0] == \'t\'', source)
        for command in ALLOWED_TEST_COMMANDS:
            self.assertIn(f'eq(command, "{command}")', source)
    def test_documented_unavailable_register_groups_are_handled(self):
        source = PLATFORM_C.read_text()
        for command in UNAVAILABLE_REGISTER_GROUPS:
            self.assertIn(f'eq(command, "{command}")', source)

if __name__ == "__main__": unittest.main()
