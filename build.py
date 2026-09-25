from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BUILD_ENV = "STM32F446ZE_btt"
MARLIN_ENVIRONMENTS = {"marlin": BUILD_ENV, "marlin-max": "STM32F446ZE_btt_max_power"}
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
SD_ROOT = Path(os.environ.get("OCTOPUS_SD_ROOT", r"I:\\"))


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


ESP_PROJECT_DIR = ROOT / "ESP_RF_Octopus"
TARGET_ALIASES = {
    "marlin": "marlin", "octopus": "marlin",
    "marlin-max": "marlin-max",
    "esp12": "esp12", "eps12": "esp12", "esp12s": "esp12", "eps12s": "esp12",
    "esp32": "esp32", "eps32": "esp32",
}
ESP_ENVIRONMENTS = {"esp12": "esp12s", "esp32": "esp32dev"}


def run_build(project_dir: Path, environment: str, port: str | None = None) -> None:
    cmd = [sys.executable, "-m", "platformio", "run", "-d", str(project_dir), "-e", environment]
    if port:
        cmd.extend(["-t", "upload", "--upload-port", port])
    subprocess.run(cmd, check=True)


def remove_old_cur() -> None:
    if not SD_ROOT.exists():
        print(f"SD card path not found, skipping cleanup/copy: {SD_ROOT}")
        return
    firmware_cur = SD_ROOT / "FIRMWARE.CUR"
    if firmware_cur.exists():
        firmware_cur.unlink()
        print(f"Removed {firmware_cur}")
    else:
        print(f"No {firmware_cur.name} found on {SD_ROOT}")


def copy_firmware(firmware_src: Path) -> None:
    if not SD_ROOT.exists():
        print(f"SD card path not found, skipping firmware copy: {SD_ROOT}")
        return
    if not firmware_src.exists():
        raise FileNotFoundError(f"Built firmware not found: {firmware_src}")

    firmware_dst = SD_ROOT / "firmware.bin"
    shutil.copy2(firmware_src, firmware_dst)
    print(f"Copied {firmware_src} -> {firmware_dst}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build Lumiac firmware. For ESP targets, adding a serial port also uploads it.",
        epilog="Examples: python build.py esp12 COM5 | python build.py esp32 COM4 | python build.py marlin --build-only",
    )
    parser.add_argument("target", nargs="?", default="marlin", type=str.lower, choices=TARGET_ALIASES)
    parser.add_argument("port", nargs="?", help="ESP upload port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--build-only", action="store_true", help="Compile without uploading or copying to SD")
    args = parser.parse_args(argv)
    target = TARGET_ALIASES[args.target]
    if target in MARLIN_ENVIRONMENTS and args.port:
        parser.error("Marlin uses SD-card flashing; a serial upload port is only valid for esp12/esp32.")

    try:
        if target in ESP_ENVIRONMENTS:
            environment = ESP_ENVIRONMENTS[target]
            port = args.port if not args.build_only else None
            print(f"Building {target} ({environment})" + (f" and uploading to {port}" if port else " only") + "...", flush=True)
            run_build(ESP_PROJECT_DIR, environment, port)
            print(f"Firmware: {ESP_PROJECT_DIR / '.pio' / 'build' / environment / 'firmware.bin'}")
        else:
            project_dir = resolve_project_dir()
            environment = MARLIN_ENVIRONMENTS[target]
            firmware_src = project_dir / ".pio" / "build" / environment / "firmware.bin"
            print(f"Building {target} ({environment}) from {project_dir}...", flush=True)
            if target == "marlin-max":
                print("Test profile: 3.0 A RMS run current, 256 microsteps. Requires effective driver cooling and compatible motor ratings.", flush=True)
            run_build(project_dir, environment)
            if not args.build_only:
                # Check the build artifact before removing the previous SD marker.
                if not firmware_src.is_file():
                    raise FileNotFoundError(f"Built firmware not found: {firmware_src}")
                remove_old_cur()
                copy_firmware(firmware_src)
            print(f"Firmware: {firmware_src}")
    except subprocess.CalledProcessError as error:
        print(f"Build/upload failed (exit {error.returncode}).", file=sys.stderr)
        return error.returncode or 1
    except OSError as error:
        print(f"Build failed: {error}", file=sys.stderr)
        return 1
    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
