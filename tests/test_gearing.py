from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from tools import firmware_package

ROOT = Path(__file__).resolve().parents[1]
MARLIN = ROOT / 'Marlin/marlin-2.1.2.6/Marlin'

class GearingTests(unittest.TestCase):
    def test_real_configuration_converts_motor_turns_into_arm_distance(self):
        config = (MARLIN / 'Configuration.h').read_text(encoding='utf-8')
        start = config.index('#ifndef SPIDER_MICROSTEPS')
        end = config.index('// Octopus-only motion reduction', start)
        with tempfile.TemporaryDirectory(prefix='lumiac-gearing-') as folder:
            source = Path(folder) / 'gearing.cpp'
            # Compile the production microstep checks, gearing header and six-axis
            # initializer together, including override settings and invalid ratios.
            source.write_text(config[start:end] + '\n#include <cassert>\n#include <cmath>\n'
                              'int main() { float axes[] = DEFAULT_AXIS_STEPS_PER_UNIT;\n'
                              'static_assert(sizeof(axes)/sizeof(*axes)==6, "six axes");\n'
                              'for (float steps:axes) assert(std::fabs(steps-EXPECTED)<0.02f); }\n', encoding='utf-8')
            executable = Path(folder) / 'gearing.exe'
            subprocess.run([sys.executable, '-m', 'ziglang', 'c++', '-std=c++11', '-I', str(MARLIN),
                            '-DEXPECTED=3199.98', str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
            cases = [(32, 1, 1), (16, 1, 3), (32, 3, 1), (256, 1, 1)]
            for micro, motor, output in cases:
                expected = 533.33 * 3 * micro / 16 * motor / output
                command = [sys.executable, '-m', 'ziglang', 'c++', '-std=c++11', '-I', str(MARLIN),
                           f'-DSPIDER_MICROSTEPS={micro}', f'-DLUMIAC_GEAR_INPUT_TURNS={motor}',
                           f'-DLUMIAC_GEAR_OUTPUT_TURNS={output}', f'-DEXPECTED={expected}',
                           str(source), '-o', str(executable)]
                subprocess.run(command, check=True)
                subprocess.run([str(executable)], check=True)
            bad = subprocess.run([sys.executable, '-m', 'ziglang', 'c++', '-fsyntax-only', '-I', str(MARLIN),
                                  '-DLUMIAC_GEAR_OUTPUT_TURNS=0', '-DEXPECTED=0', str(source)], capture_output=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn(b'positive integers', bad.stderr)

    def test_gearing_changes_machine_identity_for_every_image(self):
        with tempfile.TemporaryDirectory(prefix='lumiac-identity-') as folder:
            root = Path(folder)
            (root / 'shared').mkdir(); (root / 'shared/release.json').write_text('{"revision":"test","branch":"onetoone"}')
            (root / 'shared/firmware_transfer.h').write_text('test protocol')
            marlin, esp = root / 'Marlin', root / 'ESP'
            (marlin / 'Marlin/src/feature').mkdir(parents=True); (esp / 'src').mkdir(parents=True)
            for name in ('Configuration.h', 'Configuration_adv.h', 'lumiac_gearing.h'):
                (marlin / 'Marlin' / name).write_bytes((MARLIN / name).read_bytes())
            with patch.multiple(firmware_package, ROOT=root, MARLIN=marlin, ESP=esp):
                first, _ = firmware_package.prepare()
                header = (esp / 'src/build_version.h').read_bytes()
                self.assertEqual(header, (marlin / 'Marlin/lumiac_build.h').read_bytes())
                gear = marlin / 'Marlin/lumiac_gearing.h'
                gear.write_text(gear.read_text().replace('LUMIAC_GEAR_OUTPUT_TURNS 1', 'LUMIAC_GEAR_OUTPUT_TURNS 3'))
                second, _ = firmware_package.prepare()
                self.assertNotEqual(first, second)

if __name__ == '__main__': unittest.main()
