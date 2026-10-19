"""Exercise the production image writer and Marlin SD receiver on the host."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import re

ROOT = Path(__file__).resolve().parents[1]

class CloudFirmwareTests(unittest.TestCase):
    def test_production_sd_sender_waits_for_exact_acknowledgements_and_never_replays_activation(self):
        source = (ROOT / 'ESP_RF_Octopus/src/cloud_runtime.h').read_text()
        functions = []
        for name in ('handleFirmwareLine', 'sdCommand', 'tickOctopusUpdate'):
            start = re.search(r'^(?:bool|void) ' + name + r'\(', source, re.M).start()
            end, depth = source.index('{', start), 1
            position = end + 1
            while depth:
                depth += (source[position] == '{') - (source[position] == '}')
                position += 1
            functions.append(source[start:position])
        with tempfile.TemporaryDirectory(prefix='lumiac-sender-test-') as folder:
            path = Path(folder)
            (path / 'cloud_transfer_functions.h').write_text('\n'.join(functions))
            executable = path / 'sender.exe'
            subprocess.run([sys.executable, '-m', 'ziglang', 'c++', '-std=c++11', '-O1', '-UNDEBUG',
                            '-I', folder, str(ROOT / 'tests/cloud_transfer_test.cpp'), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_production_receivers_reject_corruption_and_preserve_activation_guards(self):
        with tempfile.TemporaryDirectory(prefix='lumiac-update-test-') as folder:
            for source, flags in [('firmware_image_test', []), ('diagnostic_journal_test', []), ('diagnostic_journal_test', ['-DARDUINO_ARCH_ESP8266=1']), ('firmware_update_test', []), ('ota_writer_test', ['-DARDUINO_ARCH_ESP8266=1']),
                                  ('ota_writer_test', ['-DARDUINO_ARCH_ESP32=1'])]:
                executable = Path(folder) / 'receiver.exe'
                subprocess.run([sys.executable, '-m', 'ziglang', 'c++', '-std=c++11', '-O1', '-UNDEBUG',
                                *flags, str(ROOT / f'tests/{source}.cpp'), '-o', str(executable)], check=True)
                subprocess.run([str(executable)], check=True)

if __name__ == '__main__': unittest.main()
