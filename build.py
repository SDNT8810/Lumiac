from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BUILD_ENV = "STM32F446ZE_btt"
PROJECT_DIR_CANDIDATES = (
    ROOT / "Marlin" / "marlin-2.1.2.6",
    ROOT / "marlin-2.1.2.6",
    ROOT / "Marlin" / "Marlin-2.1.2.6",
    ROOT / "Marlin" / "MarlinConfigurations-2.1.2.6" / "marlin-2.1.2.6",
    ROOT / "simulator" / "marlin-2.1.2.6",
    ROOT / "Simulator" / "marlin-2.1.2.6",
    ROOT / "webapp" / "marlin-2.1.2.6",
    ROOT / "WebApp" / "marlin-2.1.2.6",
)
SD_ROOT = Path(os.environ.get("OCTOPUS_SD_ROOT", r"E:\\"))


def resolve_project_dir() -> Path:
    for candidate in PROJECT_DIR_CANDIDATES:
        if (candidate / "platformio.ini").exists():
            return candidate

    marlin_dir = ROOT / "Marlin"
    if marlin_dir.exists():
        for candidate in marlin_dir.rglob("platformio.ini"):
            return candidate.parent

    checked = "\n".join(f"- {path}" for path in PROJECT_DIR_CANDIDATES)
    raise FileNotFoundError(
        "Could not find the Marlin PlatformIO project directory. Checked:\n"
        f"{checked}"
    )


PROJECT_DIR = resolve_project_dir()
FIRMWARE_SRC = PROJECT_DIR / ".pio" / "build" / BUILD_ENV / "firmware.bin"
FIRMWARE_DST = SD_ROOT / "firmware.bin"
FIRMWARE_CUR = SD_ROOT / "FIRMWARE.CUR"


def run_build() -> None:
    cmd = [sys.executable, "-m", "platformio", "run", "-d", str(PROJECT_DIR), "-e", BUILD_ENV]
    subprocess.run(cmd, check=True)


def remove_old_cur() -> None:
    if not SD_ROOT.exists():
        print(f"SD card path not found, skipping cleanup/copy: {SD_ROOT}")
        return
    if FIRMWARE_CUR.exists():
        FIRMWARE_CUR.unlink()
        print(f"Removed {FIRMWARE_CUR}")
    else:
        print(f"No {FIRMWARE_CUR.name} found on {SD_ROOT}")


def copy_firmware() -> None:
    if not SD_ROOT.exists():
        print(f"SD card path not found, skipping firmware copy: {SD_ROOT}")
        return
    if not FIRMWARE_SRC.exists():
        raise FileNotFoundError(f"Built firmware not found: {FIRMWARE_SRC}")

    shutil.copy2(FIRMWARE_SRC, FIRMWARE_DST)
    print(f"Copied {FIRMWARE_SRC} -> {FIRMWARE_DST}")


def main() -> int:
    print(f"Building firmware from {PROJECT_DIR}...")
    run_build()
    remove_old_cur()
    copy_firmware()
    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
