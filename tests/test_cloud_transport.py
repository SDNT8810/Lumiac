from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import re

ROOT = Path(__file__).resolve().parents[1]

class CloudTransportTests(unittest.TestCase):
    def test_real_transport_keeps_hardware_loop_available_for_both_controllers(self):
        with tempfile.TemporaryDirectory(prefix='lumiac-cloud-test-') as folder:
            controller = (ROOT / 'ESP_RF_Octopus/src/main.cpp').read_text(encoding='utf-8')
            start = controller.index('void readOctopusSerial() {')
            end = controller.index('\nvoid handleRoot()', start)
            limit = re.search(r'constexpr size_t kSerialLineMax = (\d+);', controller).group(1)
            (Path(folder) / 'serial_reader_functions.h').write_text(
                'constexpr size_t kSerialLineMax = ' + limit + ';\n' + controller[start:end], encoding='utf-8')
            for family in ('ESP32', 'ESP8266'):
                executable = Path(folder) / 'transport.exe'
                subprocess.run([sys.executable, '-m', 'ziglang', 'c++', '-std=c++17', '-O1', '-UNDEBUG',
                                '-D' + family, '-I', folder, str(ROOT / 'tests/cloud_transport_test.cpp'), '-o', str(executable)], check=True)
                subprocess.run([str(executable)], check=True, timeout=30)

if __name__ == '__main__': unittest.main()
