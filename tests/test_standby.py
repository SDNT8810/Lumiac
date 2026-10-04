"""Exercise the real ESP Bluetooth handler with simulated serial/dashboard I/O.

uv run --with ziglang python tests/test_standby.py
"""
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


class StandbyTests(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec("ziglang"), "Run with uv run --with ziglang")
    def test_long_press_updates_lamps_with_or_without_octopus(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / "ESP_RF_Octopus/src/main.cpp").read_text(encoding="utf-8")
        functions = []
        for name in ("clampBrightness", "lampStateCommand", "setLightState", "setBrightness",
                     "syncFanModeToOctopus", "syncLampStateToOctopus", "handleBluetoothEvent",
                     "pollOctopusCapabilities"):
            match = re.search(r"^(?:void|String|uint8_t) " + name + r"\([^\n]*\) \{.*?^}",
                              source, re.MULTILINE | re.DOTALL)
            self.assertIsNotNone(match, name)
            functions.append(match.group())
        with tempfile.TemporaryDirectory(prefix="lumiac-standby-") as folder:
            path = Path(folder)
            (path / "standby_functions.h").write_text("\n".join(functions), encoding="utf-8")
            executable = path / "standby.exe"
            result = subprocess.run([
                sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I", str(path), str(root / "tests/standby_control_test.cpp"), "-o", str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
