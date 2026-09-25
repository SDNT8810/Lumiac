"""Verify the ESP flash image contains every selectable motion program."""

import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "ESP_RF_Octopus" / "scripts" / "embed_gcodes.py"
spec = importlib.util.spec_from_file_location("embed_gcodes", SCRIPT)
embed_gcodes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed_gcodes)


class EmbeddedGcodeTests(unittest.TestCase):
    def test_generated_header_matches_all_source_programs(self):
        self.assertTrue(embed_gcodes.generate(check=True))
        header = embed_gcodes.OUTPUT.read_text(encoding="utf-8")
        for filename in (*embed_gcodes.RANDOM_FILES, *embed_gcodes.PRESET_FILES):
            with self.subTest(filename=filename):
                self.assertIn(f'"/gcodes/{filename}"', header)

        # Seven identical random programs retain seven selections but use one flash copy.
        self.assertEqual(header.count("[] PROGMEM"), 4)
        self.assertEqual(header.count("@LOOP"), 1)

    def test_rejects_commands_the_esp_stream_cannot_send(self):
        with patch.object(embed_gcodes.Path, "read_text", return_value="M23 /gcodes/input.txt\n@LOOP\n"):
            with self.assertRaisesRegex(ValueError, "unsupported"):
                embed_gcodes.read_program("input.txt", loop=True)

    def test_random_paths_retrace_to_their_start_before_looping(self):
        for filename in embed_gcodes.RANDOM_FILES:
            with self.subTest(filename=filename):
                content = embed_gcodes.read_program(filename, loop=True)
                moves = [line.split(" F", 1)[0] for line in content.splitlines() if line.startswith("G1 ")]
                self.assertEqual(len(moves), 239)
                self.assertEqual(moves, moves[::-1])
                self.assertEqual(content.splitlines()[-1], "@LOOP")


if __name__ == "__main__":
    unittest.main()
