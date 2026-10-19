"""Install the verified prebuilt Lumiac package by USB/SD, without rebuilding."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
from tools.firmware_package import OUTPUT, verify

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=['verify', 'esp12', 'esp32', 'octopus'])
    parser.add_argument('--port')
    parser.add_argument('--sd', type=Path)
    parser.add_argument('--host', help='Update over the local hotspot/LAN, for example 192.168.4.1')
    args = parser.parse_args()
    verify()
    if args.target == 'verify': return
    if args.host:
        if args.port or args.sd: parser.error('Choose --host for Wi-Fi or --port/--sd for USB/SD')
        from local_update import install_targets
        install_targets(args.host, [args.target])
        return
    if args.target == 'octopus':
        if not args.sd or not args.sd.is_dir(): parser.error('Use --sd with the mounted microSD root, for example I:\\')
        shutil.copy2(OUTPUT / 'octopus.bin', args.sd / 'firmware.bin')
        (args.sd / 'FIRMWARE.CUR').unlink(missing_ok=True)
        print('Insert the card into the powered-off Octopus. Boot, confirm FIRMWARE.CUR, then leave the card installed.')
        return
    if not args.port: parser.error('Use --port with the ESP serial port')
    chip = 'esp8266' if args.target == 'esp12' else 'esp32'
    images = [('0x0', 'esp12.bin')] if chip == 'esp8266' else [
        ('0x1000', 'esp32_bootloader.bin'), ('0x8000', 'esp32_partitions.bin'),
        ('0xe000', 'esp32_boot_app0.bin'), ('0x10000', 'esp32.bin')]
    command = [sys.executable, '-m', 'esptool', '--chip', chip, '--port', args.port, '--baud', '115200',
               'write-flash', '--flash-mode', 'dio', '--flash-size', '4MB', '--flash-freq', '40m']
    for offset, name in images: command += [offset, str(OUTPUT / name)]
    subprocess.run(command, check=True)
    print('Controller installed. Connect to Lumiac Wi-Fi and open http://192.168.4.1/network to configure its router.')

if __name__ == '__main__': main()
