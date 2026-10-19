"""Update Lumiac over its hotspot or router LAN, without a cloud connection."""
from __future__ import annotations
import argparse
import hashlib
import hmac
import http.server
import ipaddress
import json
import re
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
import zlib
from tools.firmware_package import ROOT, OUTPUT, verify
MASTER_CLOUD_CONFIG = ROOT / "ESP_RF_Octopus/src/cloud_config.h"
MASTER_LOCAL_CONFIG = ROOT / "ESP_RF_Octopus/src/cloud_config.local.h"

IMAGES = {
    'esp12': ('esp12', 'app', 'esp12.bin'),
    'esp32': ('esp32', 'app', 'esp32.bin'),
    'octopus': ('octopus', 'octopus', 'octopus.bin'),
}

def pairing_key():
    for path in (MASTER_LOCAL_CONFIG, MASTER_CLOUD_CONFIG):
        if path.is_file():
            match = re.search(r'^\s*#define\s+LUMIAC_CLOUD_DEVICE_KEY\s+"([^"\r\n]+)"', path.read_text(encoding='utf-8'), re.M)
            if match:
                return match[1].encode()
    raise ValueError('Local pairing key is missing from the firmware configuration')

def prepare_images(steps):
    manifest = verify()
    result = []
    for step in steps:
        target, kind, filename = IMAGES[step]
        data = (OUTPUT / filename).read_bytes()
        revision = manifest['revision']
        machine = manifest['identity']
        if revision.encode() not in data or machine.encode() not in data or ('LUMIAC_OTA_ROLE:' + target).encode() not in data:
            raise ValueError('Wrong role or missing identity/revision in ' + filename)
        if kind == 'app' and b'LUMIAC_LOCAL_OTA_V1' not in data:
            raise ValueError('This controller image removes local-update recovery; select a release with hotspot support')
        result.append((step, data, dict(target=target, kind=kind, size=len(data), revision=revision,
            machine=machine, sha256=hashlib.sha256(data).hexdigest(), crc32=zlib.crc32(data) if kind == 'octopus' else 0)))
    return result

def sign_manifest(manifest, key):
    names = ('session', 'nonce', 'id', 'target', 'kind', 'size', 'sha256', 'crc32', 'revision', 'machine', 'port', 'imageId')
    message = 'lumiac-local-v1|' + '|'.join(str(manifest[name]) for name in names)
    return hmac.new(key, message.encode(), hashlib.sha256).hexdigest()

class ImageServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, master_ip):
        self.images, self.master_ip = {}, master_ip
        super().__init__(address, ImageHandler)

class ImageHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        data = self.server.images.get(self.path)
        if self.client_address[0] != self.server.master_ip or data is None:
            self.send_error(404); return
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        try:
            self.connection.settimeout(30)
            self.wfile.write(data)
        except (OSError, TimeoutError):
            pass  # Device checks length/hash; never replay an interrupted flash.

    def log_message(self, *_):
        pass

def route_address(master):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((master, 80))
        return route.getsockname()[0]

class LocalGateway:
    def __init__(self, host='192.168.4.1'):
        address = ipaddress.IPv4Address(host)
        if not address.is_private or address.is_loopback or address.is_unspecified or address.is_multicast:
            raise ValueError('Use the master hotspot/LAN IPv4 address, for example 192.168.4.1')
        self.host, self.url = str(address), 'http://' + str(address)
        self.client = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def call(self, path, body=None):
        request = urllib.request.Request(self.url + path,
            data=None if body is None else json.dumps(body).encode(),
            headers={'Content-Type': 'application/json', 'User-Agent': 'Lumiac-Local-Updater/1.0'})
        try:
            with self.client.open(request, timeout=15) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            try:
                message = json.load(error).get('error', str(error))
            except (ValueError, AttributeError):
                message = str(error)
            if error.code == 404 and path == '/api/firmware/local':
                message = 'Install local-update support on the master by USB once before using hotspot updates'
            failure = RuntimeError(message); failure.status = error.code
            raise failure from None

    def info(self):
        info = self.call('/api/firmware/local')
        if not info.get('localSupported') or not info.get('nonce'):
            raise ValueError('Install local-update support on the master by USB once')
        return info

    def pause(self):
        # Firmware start itself parks motion and waits for the exact M400/M118
        # barrier. Never interrupt homing just to begin an update.
        state = self.info()
        if state.get('homing'):
            raise RuntimeError('Wait for homing to finish before updating')

    def install(self, prepared, server, key, timeout=1260):
        step, data, metadata = prepared
        info = self.info()
        if metadata['machine'] != info.get('machine'):
            raise ValueError('Hardware identity changed; install a matched USB/SD package')
        manifest = dict(metadata, id=str(uuid.uuid4()), imageId=str(uuid.uuid4()),
            session=info['session'], nonce=info['nonce'], port=server.server_port)
        manifest['auth'] = sign_manifest(manifest, key)
        path = '/firmware/' + manifest['imageId']
        server.images[path] = data
        print(f"{step}: starting local Wi-Fi update. Keep the PC connected and powered.", flush=True)
        try:
            # One start request only. A lost reply is reconciled by exact receipt.
            try:
                self.call('/api/firmware/local', manifest)
            except (OSError, RuntimeError) as error:
                if isinstance(error, RuntimeError) and getattr(error, 'status', 0) < 500:
                    raise
                print('Start reply unavailable; checking the existing job without resubmitting.', flush=True)
            started = time.monotonic()
            deadline, previous, accepted = started + timeout, '', False
            while time.monotonic() < deadline:
                try:
                    current = self.info()
                except (OSError, RuntimeError):
                    time.sleep(2); continue
                ota = current.get('ota') or {}
                if ota.get('id') == manifest['id'] and ota.get('sha256') == metadata['sha256']:
                    accepted = True
                    if ota.get('state') == 'complete':
                        if metadata['kind'] == 'app' and current.get('software') != metadata['revision']:
                            raise RuntimeError('Master restarted with an unexpected revision')
                        print(step + ': complete. Motion remains parked.', flush=True)
                        return
                    if ota.get('state') == 'failed':
                        raise RuntimeError(step + ': ' + (ota.get('error') or 'Firmware update failed'))
                    label = str(ota.get('phase') or ota.get('state') or 'starting')
                    if ota.get('size'):
                        label += ' ' + str(min(100, round(100 * ota.get('done', 0) / ota['size']))) + '%'
                else:
                    # A reboot without our receipt is never treated as success.
                    if current.get('session') != info['session']:
                        raise RuntimeError('Controller restarted without confirming this update; inspect its diagnostic log')
                    if not accepted and time.monotonic() - started > 30:
                        raise RuntimeError('Master did not confirm accepting this update; it was not resubmitted')
                    label = 'waiting for this update to be accepted'
                if label != previous:
                    print(step + ': ' + label, flush=True); previous = label
                time.sleep(2)
            raise RuntimeError(step + ': confirmation timed out. Inspect local diagnostics before retrying')
        finally:
            server.images.pop(path, None)

def install_targets(host, steps):
    prepared, key = prepare_images(steps), pairing_key()
    gateway = LocalGateway(host)
    info = gateway.info()
    if (info.get('ota') or {}).get('active'):
        raise RuntimeError('Another firmware update is active; wait for its result')
    if steps == list(IMAGES):
        prepared = [item for item in prepared if item[0] in (info.get('controller'), 'octopus')]
    if any(metadata['target'] not in (info.get('controller'), 'octopus') for _, _, metadata in prepared):
        raise ValueError('Select the image for the connected ESP controller')
    if any(metadata['machine'] != info.get('machine') for _, _, metadata in prepared):
        raise ValueError('Hardware identity changed; install a matched USB/SD package')
    interface = route_address(gateway.host)
    # The request may address the station IP through the hotspot. The return
    # download still originates from the master's AP IP on that interface.
    downloader = '192.168.4.1' if ipaddress.IPv4Address(interface) in ipaddress.IPv4Network('192.168.4.0/24') else gateway.host
    with ImageServer((interface, 0), downloader) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        print('Serving firmware on the PC local interface. Allow Python through Windows Firewall if prompted.', flush=True)
        try:
            gateway.pause()
            for image in prepared:
                gateway.install(image, server, key)
        finally:
            server.shutdown(); thread.join(timeout=5)
    print('Finished. Home the arms before resuming motion.', flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.4.1', help='master hotspot or router LAN IPv4 address')
    parser.add_argument('--target', choices=['all', *IMAGES], default='all')
    parser.add_argument('--build', action='store_true', help='build all firmware before verifying/installing the bundle')
    args = parser.parse_args()
    if args.build:
        subprocess.run([sys.executable, str(ROOT / 'tools/firmware_package.py'), 'all'], check=True)
    steps = list(IMAGES) if args.target == 'all' else [args.target]
    install_targets(args.host, steps)

if __name__ == '__main__':
    try:
        main()
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
