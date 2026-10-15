import struct
import unittest
from pathlib import Path
from tools.firmware_package import OUTPUT, verify

class ReleasePackageTests(unittest.TestCase):
    @unittest.skipUnless((OUTPUT / 'manifest.json').exists(), 'Build the release package first')
    def test_exported_esp32_layout_has_two_slots_and_keeps_every_image_within_its_partition(self):
        manifest = verify()
        data = (OUTPUT / 'esp32_partitions.bin').read_bytes()
        records = []
        for offset in range(0, len(data) - 31, 32):
            magic, kind, subtype, start, size, label, flags = struct.unpack_from('<HBBII16sI', data, offset)
            if magic != 0x50AA: break
            records.append((kind, subtype, start, size, label.rstrip(b'\0')))
        apps = [r for r in records if r[0] == 0]
        self.assertEqual([(r[1], r[2], r[3]) for r in apps], [(0x10, 0x10000, 0x1c0000), (0x11, 0x1d0000, 0x1c0000)])
        self.assertLessEqual(manifest['images']['esp32.bin']['size'], apps[0][3])
        for previous, current in zip(records, records[1:]): self.assertLessEqual(previous[2] + previous[3], current[2])
        self.assertLessEqual(records[-1][2] + records[-1][3], 0x400000)
        self.assertEqual((OUTPUT / 'esp32_boot_app0.bin').stat().st_size, 0x2000)

if __name__ == '__main__': unittest.main()
