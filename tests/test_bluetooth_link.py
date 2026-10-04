"""Catch Arduino releasing Classic Bluetooth memory before setup() can use it."""
import os
from pathlib import Path
import subprocess
import unittest


class BluetoothLinkTests(unittest.TestCase):
    def test_esp32_image_retains_arduino_bluetooth_memory_hook(self):
        root = Path(__file__).resolve().parents[1]
        elf = root / "ESP_RF_Octopus/.pio/build/esp32dev/firmware.elf"
        core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
        executable = "xtensa-esp32-elf-nm" + (".exe" if os.name == "nt" else "")
        nm = core / "packages/toolchain-xtensa-esp32/bin" / executable
        if not elf.exists() or not nm.exists():
            self.skipTest("Build esp32 first to check its actual linked startup hook")
        result = subprocess.run([str(nm), str(elf)], capture_output=True, text=True, check=True)
        symbols = {parts[2]: parts[1] for line in result.stdout.splitlines()
                   if len(parts := line.split()) == 3}
        # The weak W btInUse returns false: initArduino frees BT memory forever,
        # and esp_bt_controller_init subsequently returns ESP_ERR_INVALID_STATE.
        self.assertEqual(symbols.get("btInUse"), "T", "ESP32 linked Arduino's weak Bluetooth-disabled startup hook")
        self.assertEqual(symbols.get("btStarted"), "T")


if __name__ == "__main__":
    unittest.main()
