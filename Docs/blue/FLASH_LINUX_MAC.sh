#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    echo 'Usage: bash FLASH_LINUX_MAC.sh [SERIAL_PORT]'
    echo 'Example: bash FLASH_LINUX_MAC.sh /dev/ttyUSB0'
    echo 'Activate ESP-IDF v5.3.1 before running this script.'
    exit 0
fi

if ! command -v idf.py >/dev/null 2>&1; then
    echo 'ESP-IDF is not available in this terminal.' >&2
    echo 'Install ESP-IDF v5.3.1 and source its export.sh, then try again.' >&2
    exit 1
fi

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
export IDF_TARGET=esp32
idf.py build

echo 'Flashing firmware and starting the serial monitor. Press Ctrl+] to exit.'
if [[ -n "${1:-}" ]]; then
    idf.py -p "$1" flash monitor
else
    idf.py flash monitor
fi
