"""Run the actual planner reduction stages against changing ESP motion settings."""
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


class MotionReductionTests(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec("ziglang"), "Run with uv run --with ziglang")
    def test_octopus_scales_limited_blocks_without_changing_requests(self):
        root = Path(__file__).resolve().parents[1]
        marlin = root / "Marlin/marlin-2.1.2.6/Marlin"
        config = (marlin / "Configuration.h").read_text(encoding="utf-8")
        planner = (marlin / "src/module/planner.cpp").read_text(encoding="utf-8")
        percent = re.search(r"^#define LUMIAC_MOTION_PERCENT (\d+)$", config, re.M).group(1)
        self.assertEqual(percent, "70")
        speed_start = planner.index("  #ifdef LUMIAC_MOTION_PERCENT")
        speed_end = planner.index("  // Compute and limit the acceleration rate", speed_start)
        acceleration_start = planner.index("  #ifdef LUMIAC_MOTION_PERCENT", speed_end)
        acceleration_end = planner.index("  #if DISABLED(S_CURVE_ACCELERATION)", acceleration_start)
        # This is the shared block planner, downstream of both normal and homing
        # admission. Scaling must follow limits and precede trajectory planning.
        self.assertLess(planner.index("max_fr = settings.max_feedrate_mm_s[i]"), speed_start)
        self.assertLess(planner.index("// Limit acceleration per axis", speed_end), acceleration_start)
        self.assertLess(acceleration_end, planner.index("float vmax_junction_sqr", acceleration_end))
        harness = r"""
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#define _MAX(a,b) ((a) > (b) ? (a) : (b))
struct Speeds {
  float axis[6];
  void operator*=(float factor) { for (auto& speed : axis) speed *= factor; }
};
struct Block {
  float nominal_speed, acceleration;
  uint32_t nominal_rate, acceleration_steps_per_s2;
  int32_t target[6];
};
void reduce(Block* block, Speeds& current_speed, float speed_factor,
            uint32_t accel, float steps_per_mm) {
SPEED_STAGE
ACCELERATION_STAGE
}
bool near(float a, float b) { return std::fabs(a - b) < 0.0002f; }
int main() {
  // Changing F, M220, M203, M201 and M204, including EEPROM/default and direct
  // homing values. Repeated calls must not scale settings a second time.
  struct Input { float feed, override_percent, max_feed, acceleration, max_acceleration; };
  const Input inputs[] = {
    {300, 100, 390, 15, 50}, {338, 51, 390, 8, 50},
    {600, 100, 390, 25, 80}, {160, 100, 390, 25, 80},
    {600, 100, 2, 15, 5}, {10, 100, 390, 8, 50},
    {338, 20, 390, 8, 50}, {338, 100, 390, 8, 50}
  };
  for (const auto& input : inputs) for (int repeat = 0; repeat < 3; ++repeat) {
    const float requested = input.feed / 60 * input.override_percent / 100;
    const float limit = std::fmin(1.0f, input.max_feed / requested);
    const uint32_t original_accel = uint32_t(std::fmin(input.acceleration, input.max_acceleration) * 10000);
    Block block{};
    block.nominal_speed = requested;
    block.nominal_rate = uint32_t(requested * 10000);
    Speeds speeds{};
    for (int axis = 0; axis < 6; ++axis) {
      speeds.axis[axis] = (axis & 1 ? -1 : 1) * requested * (axis + 1) / 6;
      block.target[axis] = axis * 1700 - 900;
    }
    const Block before = block;
    const Speeds before_speeds = speeds;
    reduce(&block, speeds, limit, original_accel, 10000);
    assert(near(block.nominal_speed, requested * limit * .7f));
    assert(std::fabs(float(block.nominal_rate) - before.nominal_rate * limit * .7f) < 1.1f);
    assert(std::fabs(float(block.acceleration_steps_per_s2) - original_accel * .7f) < 1.1f);
    assert(near(block.acceleration, std::fmin(input.acceleration, input.max_acceleration) * .7f));
    for (int axis = 0; axis < 6; ++axis) {
      assert(near(speeds.axis[axis], before_speeds.axis[axis] * limit * .7f));
      assert(block.target[axis] == before.target[axis]);
    }
  }
  Block tiny{1, 0, 1000, 0, {}}; Speeds speeds{};
  reduce(&tiny, speeds, 1, 1, 10000);
  assert(tiny.acceleration_steps_per_s2 == 1); // Never divide by zero in profile generation.
  puts("PASS actual planner stages: 70% speed/acceleration, all axes, capped moves, homing rates, repeated ESP settings and unchanged destinations");
}
"""
        harness = f"#define LUMIAC_MOTION_PERCENT {percent}\n" + harness
        harness = harness.replace("SPEED_STAGE", planner[speed_start:speed_end])
        harness = harness.replace("ACCELERATION_STAGE", planner[acceleration_start:acceleration_end])
        with tempfile.TemporaryDirectory(prefix="lumiac-motion-") as folder:
            path = Path(folder)
            source, executable = path / "motion.cpp", path / "motion.exe"
            source.write_text(harness, encoding="utf-8")
            result = subprocess.run([
                sys.executable, "-m", "ziglang", "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                str(source), "-o", str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
