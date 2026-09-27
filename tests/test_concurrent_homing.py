"""Compile homing-latch assertions with the installed Marlin ARM toolchain."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ConcurrentHomingTests(unittest.TestCase):
    def test_independent_motors_for_all_720_endstop_orders(self):
        core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
        compiler = shutil.which("arm-none-eabi-g++")
        if not compiler:
            name = "arm-none-eabi-g++.exe" if os.name == "nt" else "arm-none-eabi-g++"
            path = core / "packages" / "toolchain-gccarmnoneeabi" / "bin" / name
            if not path.exists():
                self.skipTest("Build Marlin once to install its ARM compiler")
            compiler = str(path)
        with tempfile.TemporaryDirectory(prefix="lumiac-homing-test-") as folder:
            result = subprocess.run(
                [compiler, "-std=c++14", "-fconstexpr-ops-limit=100000000", "-c",
                 str(Path(__file__).with_name("concurrent_homing_contract.cpp")),
                 "-o", str(Path(folder) / "homing.o")],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
