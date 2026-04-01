from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PROJECT_DIR = ROOT / "simulator" / "marlin-2.1.2.6"
BUILD_ENV = "STM32F446ZE_btt"
FIRMWARE_SRC = PROJECT_DIR / ".pio" / "build" / BUILD_ENV / "firmware.bin"
SD_ROOT = Path(r"E:\\")
FIRMWARE_DST = SD_ROOT / "firmware.bin"
FIRMWARE_CUR = SD_ROOT / "FIRMWARE.CUR"


def run_build() -> None:
    cmd = [sys.executable, "-m", "platformio", "run", "-d", str(PROJECT_DIR), "-e", BUILD_ENV]
    subprocess.run(cmd, check=True)


def remove_old_cur() -> None:
    if FIRMWARE_CUR.exists():
        FIRMWARE_CUR.unlink()
        print(f"Removed {FIRMWARE_CUR}")
    else:
        print(f"No {FIRMWARE_CUR.name} found on {SD_ROOT}")


def copy_firmware() -> None:
    if not SD_ROOT.exists():
        raise FileNotFoundError(f"SD card path not found: {SD_ROOT}")
    if not FIRMWARE_SRC.exists():
        raise FileNotFoundError(f"Built firmware not found: {FIRMWARE_SRC}")

    shutil.copy2(FIRMWARE_SRC, FIRMWARE_DST)
    print(f"Copied {FIRMWARE_SRC} -> {FIRMWARE_DST}")


def main() -> int:
    print("Building firmware...")
    run_build()
    remove_old_cur()
    copy_firmware()
    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
