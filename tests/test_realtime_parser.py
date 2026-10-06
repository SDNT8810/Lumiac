"""Run the actual Marlin emergency parser with a minimal host hardware config.

uv run --with ziglang python tests/test_realtime_parser.py
"""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class RealtimeParserTests(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec("ziglang"), "Run with uv run --with ziglang")
    def test_lamp_and_transport_on_real_emergency_parser(self):
        root = Path(__file__).resolve().parents[1]
        feature = root / "Marlin/marlin-2.1.2.6/Marlin/src/feature"
        with tempfile.TemporaryDirectory(prefix="lumiac-parser-") as folder:
            path = Path(folder)
            (path / "feature").mkdir()
            (path / "inc").mkdir()
            for filename in ("e_parser.h", "lumiac_light_command.h", "lumiac_fan.h"):
                shutil.copy2(feature / filename, path / "feature" / filename)
            (path / "inc/MarlinConfigPre.h").write_text("""
#pragma once
#include <initializer_list>
#define ENABLED(x) x
#define ANY(a,b) ((a) || (b))
#define LUMIAC_REALTIME_LIGHT 1
#define LUMIAC_MOTION_FAN 1
#define REALTIME_REPORTING_COMMANDS 1
#define FORCE_INLINE inline
#define ISEOL(c) ((c) == '\\r' || (c) == '\\n')
""")
            executable = path / "parser.exe"
            result = subprocess.run([
                sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I", str(path), str(root / "tests/realtime_parser_test.cpp"), "-o", str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
