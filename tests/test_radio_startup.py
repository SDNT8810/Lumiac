"""Exercise production AP/Bluetooth startup against the SDK coexistence contract."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RadioStartupTests(unittest.TestCase):
    def test_hotspot_and_bluetooth_start_with_compatible_radio_power_settings(self):
        source = (ROOT / 'ESP_RF_Octopus/src/main.cpp').read_text(encoding='utf-8')
        setup = source[source.index('void setup() {'):]
        start = setup.index('#if defined(ESP8266)')
        end = setup.index('  beginCloud();', start)
        with tempfile.TemporaryDirectory(prefix='lumiac-radio-') as folder:
            root = Path(folder)
            (root / 'radio_startup.h').write_text('void startRadios() {\n' + setup[start:end] + '\n}\n', encoding='utf-8')
            for family in ('ESP32', 'ESP8266'):
                with self.subTest(family=family):
                    executable = root / (family + '.exe')
                    subprocess.run([sys.executable, '-m', 'ziglang', 'c++', '-std=c++17', '-O1', '-UNDEBUG',
                                    '-D' + family, '-I', folder, str(ROOT / 'tests/radio_startup_test.cpp'),
                                    '-o', str(executable)], check=True)
                    subprocess.run([str(executable)], check=True, timeout=15)


if __name__ == '__main__':
    unittest.main()
