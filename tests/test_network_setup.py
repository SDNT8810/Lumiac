from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NetworkSetupTests(unittest.TestCase):
    def test_production_router_setup_for_both_controllers(self):
        with tempfile.TemporaryDirectory(prefix='lumiac-network-') as folder:
            executable = Path(folder) / 'network.exe'
            for family in ('ESP32', 'ESP8266'):
                subprocess.run([sys.executable, '-m', 'ziglang', 'c++', '-std=c++17', '-O1', '-UNDEBUG',
                                '-D' + family, str(ROOT / 'tests/network_setup_test.cpp'),
                                '-o', str(executable)], check=True)
                subprocess.run([str(executable)], check=True, timeout=15)


if __name__ == '__main__':
    unittest.main()
