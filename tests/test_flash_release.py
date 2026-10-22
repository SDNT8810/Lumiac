import contextlib
import io
from pathlib import Path
import unittest
from unittest.mock import patch

import flash_release


class FlashReleaseTests(unittest.TestCase):
    def test_esp32_initial_install_includes_bootloader_partitions_and_ota_selection(self):
        with patch('sys.argv', ['flash_release.py', 'esp32', '--port', 'COM4']), \
                patch.object(flash_release, 'verify') as verify, \
                patch.object(flash_release.subprocess, 'run') as run, \
                contextlib.redirect_stdout(io.StringIO()):
            flash_release.main()
        verify.assert_called_once()
        command = run.call_args.args[0]
        self.assertEqual(command[command.index('--chip') + 1], 'esp32')
        self.assertEqual(command[command.index('--port') + 1], 'COM4')
        self.assertEqual(command[-8::2], ['0x1000', '0x8000', '0xe000', '0x10000'])
        self.assertEqual([Path(p).name for p in command[-7::2]],
                         ['esp32_bootloader.bin', 'esp32_partitions.bin', 'esp32_boot_app0.bin', 'esp32.bin'])
        self.assertTrue(run.call_args.kwargs['check'])

    def test_invalid_bundle_never_reaches_the_serial_port(self):
        with patch('sys.argv', ['flash_release.py', 'esp32', '--port', 'COM4']), \
                patch.object(flash_release, 'verify', side_effect=ValueError('checksum failed')), \
                patch.object(flash_release.subprocess, 'run') as run:
            with self.assertRaises(ValueError):
                flash_release.main()
        run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
