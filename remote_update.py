"""Install a verified Lumiac release through its remote panel; no local PC needed."""
import argparse
import getpass
import http.cookiejar
import json
import os
import time
import urllib.error
import urllib.request
from tools.firmware_package import OUTPUT, verify

class Gateway:
    def __init__(self, url):
        self.url = url.rstrip('/')
        self.client = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    def call(self, path, body=None, headers=None):
        binary = isinstance(body, bytes)
        data = body if binary else json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.url + path, data=data, headers={
            'User-Agent': 'Lumiac-Updater/1.0', 'Origin': self.url,
            'Content-Type': 'application/octet-stream' if binary else 'application/json', **(headers or {})})
        try:
            with self.client.open(request, timeout=90) as response: return json.load(response)
        except urllib.error.HTTPError as error:
            try: message = json.load(error).get('error', str(error))
            except (ValueError, AttributeError): message = str(error)
            raise RuntimeError(message) from error
    def install(self, device, target):
        base = '/api/devices/' + device
        image = self.call(base + '/firmware', (OUTPUT / (target + '.bin')).read_bytes(),
                          {'X-Lumiac-Target': target, 'X-Lumiac-Kind': 'octopus' if target == 'octopus' else 'app'})
        job = self.call(base + '/updates', {'imageId': image['id']})
        started, previous = time.monotonic(), ''
        while time.monotonic() - started < 660:
            time.sleep(3)
            try: remote = self.call(base + '/status')
            except (RuntimeError, urllib.error.URLError, TimeoutError) as error:
                print('Waiting for the status connection:', error, flush=True); continue
            ota = (remote.get('status') or {}).get('ota') or {}
            receipt = remote.get('update') or {}
            if receipt.get('id') == job['id'] and receipt.get('state') in ['failed', 'expired', 'cancelled']:
                raise RuntimeError(receipt.get('error') or receipt['state'])
            if ota.get('id') != job['id']: continue
            label = ota.get('phase') or ota.get('state')
            if label != previous: print(target + ': ' + label, flush=True); previous = label
            if ota.get('state') == 'failed': raise RuntimeError(ota.get('error') or 'Update failed')
            if ota.get('state') == 'complete' and ota.get('sha256') == image['sha256']: return
        raise RuntimeError('Update confirmation timed out. Check the dashboard before retrying.')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=['all', 'controller', 'esp12', 'esp32', 'octopus'], default='all')
    parser.add_argument('--url', default='https://lumiac.control.timiac.it')
    parser.add_argument('--username', default='Andrea')
    parser.add_argument('--password-env', help='Name of an environment variable containing the password')
    args = parser.parse_args()
    verify()
    gateway = Gateway(args.url)
    password = os.environ[args.password_env] if args.password_env else getpass.getpass('Andrea dashboard password: ')
    gateway.call('/api/login', {'username': args.username, 'password': password})
    try:
        device = gateway.call('/api/config')['deviceId']
        remote = gateway.call('/api/devices/' + device + '/status')
        if not remote['online']: raise RuntimeError('Lumiac is offline')
        controller = remote['status']['controller']
        targets = ['octopus', controller] if args.target == 'all' else [controller if args.target == 'controller' else args.target]
        if any(target not in ['octopus', controller] for target in targets): raise RuntimeError('Choose the connected ESP controller type')
        for target in targets: gateway.install(device, target)
        print('Update complete. Motion remains parked. Use Home, then a position or motion loop.')
    finally:
        try: gateway.call('/api/logout', {})
        except Exception: pass

if __name__ == '__main__': main()
