import test from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { readFileSync } from 'node:fs';
import { generateKeyPair, SignJWT, jwtVerify } from 'jose';
import { createGateway } from '../cloudflare/worker.js';
import { firmwareCrc32, validateFirmware } from '../cloudflare/firmware.js';
import { validateAction } from '../cloudflare/actions.js';

const { privateKey, publicKey } = await generateKeyPair('RS256');
const issuer = 'https://securetoken.google.com/test-project';
async function token(overrides = {}, audience = 'test-project', expiry = '1h') {
  return new SignJWT({ email: 'andrea@lumiac.it', firebase: { sign_in_provider: 'password' }, ...overrides })
    .setProtectedHeader({ alg: 'RS256' }).setSubject(overrides.sub || 'andrea')
    .setIssuer(issuer).setAudience(audience).setIssuedAt().setExpirationTime(expiry).sign(privateKey);
}
const goodToken = await token();
function fixture(t) {
  const sql = new DatabaseSync(':memory:');
  sql.exec(readFileSync(new URL('../cloudflare/schema.sql', import.meta.url), 'utf8'));
  t.after(() => sql.close());
  const DB = { prepare(query) {
    let args = [];
    return { bind(...values) { args = values.map(value => value instanceof ArrayBuffer ? new Uint8Array(value) : value); return this; },
      async first() { return sql.prepare(query).get(...args) || null; },
      async run() { const result = sql.prepare(query).run(...args); return { meta: { changes: Number(result.changes) } }; },
      async all() { return { results: sql.prepare(query).all(...args) }; },
    };
  }};
  let now = Date.now();
  const env = { DB, LUMIAC_PUBLIC_ORIGIN: 'https://control.lumiac.it', LUMIAC_DEVICE_ID: 'clock-01',
    LUMIAC_ADMIN_USERNAME: 'Andrea', LUMIAC_ADMIN_EMAIL: 'andrea@lumiac.it', LUMIAC_ADMIN_UID: 'andrea',
    FIREBASE_PROJECT_ID: 'test-project', FIREBASE_API_KEY: 'test-only-api-key', LUMIAC_DEVICE_KEY: 'test-only-device-key',
    ASSETS: { fetch: async () => new Response('<html>Sign in</html>', { headers: { 'Content-Type': 'text/html' } }) },
  };
  const gateway = createGateway({
    clock: () => now,
    verifyToken: async value => (await jwtVerify(value, publicKey, { issuer, audience: 'test-project', algorithms: ['RS256'] })).payload,
    upstream: async (_url, options) => JSON.parse(options.body).password === 'test-password' ?
      Response.json({ localId: 'andrea', idToken: goodToken, expiresIn: '3600' }) : Response.json({ error: 'bad password' }, { status: 400 }),
  });
  const call = (path, body, headers = {}) => gateway.fetch(new Request('https://control.lumiac.it' + path, {
    method: body === undefined ? 'GET' : 'POST',
    headers: { Origin: env.LUMIAC_PUBLIC_ORIGIN, 'Content-Type': 'application/json', ...headers },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  }), env);
  const admin = { Cookie: '__Host-lumiac-session=' + goodToken };
  const device = { 'X-Lumiac-Device-Key': env.LUMIAC_DEVICE_KEY };
  const state = { session: 'boot-one', machineReady: true, operation: { state: 'complete' }, a: { homed: 63 }, b: { homed: 63 } };
  const sync = status => call('/device/v1/devices/clock-01/sync', { status: status || state }, device);
  const action = (type, values = {}) => call('/api/devices/clock-01/actions', { type, ...values }, admin);
  return { call, admin, device, sync, action, state, sql, env, gateway, advance: ms => { now += ms; } };
}

test('hardware status and commands require an authenticated administrator', async t => {
  const f = fixture(t);
  assert.equal((await f.call('/api/devices/clock-01/status')).status, 401);
  assert.equal((await f.call('/api/devices/clock-01/actions', { type: 'home' }, f.device)).status, 401);
  assert.equal((await f.call('/api/devices/other/status', undefined, f.admin)).status, 404);
});
test('password login returns only the username and a secure session cookie', async t => {
  const f = fixture(t), response = await f.call('/api/login', { username: 'Andrea', password: 'test-password' });
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { username: 'Andrea' });
  const cookie = response.headers.get('Set-Cookie');
  for (const flag of ['HttpOnly', 'Secure', 'SameSite=Strict', 'Path=/', 'Max-Age=3600']) assert.ok(cookie.includes(flag));
  assert.equal((await f.call('/api/session', undefined, f.admin)).status, 200);
});
test('incorrect usernames and passwords fail without exposing provider errors', async t => {
  const f = fixture(t);
  for (const body of [{ username: 'Andrea', password: 'wrong' }, { username: 'somebody', password: 'test-password' }]) {
    const r = await f.call('/api/login', body); assert.equal(r.status, 401);
    assert.deepEqual(await r.json(), { error: 'Incorrect username or password' });
  }
});
test('login attempts are limited in the database and recover after 15 minutes', async t => {
  const f = fixture(t);
  for (let i = 0; i < 5; i++) assert.equal((await f.call('/api/login', { username: 'Andrea', password: 'wrong' })).status, 401);
  assert.equal((await f.call('/api/login', { username: 'Andrea', password: 'test-password' })).status, 429);
  f.advance(900001);
  assert.equal((await f.call('/api/login', { username: 'Andrea', password: 'test-password' })).status, 200);
});
test('cross-site login, logout and control requests are rejected', async t => {
  const f = fixture(t);
  for (const [path, body] of [['/api/login', { username: 'Andrea', password: 'test-password' }], ['/api/logout', {}], ['/api/devices/clock-01/actions', { type: 'home' }]]) {
    assert.equal((await f.call(path, body, { ...f.admin, Origin: 'https://untrusted.example' })).status, 403);
  }
  const r = await f.call('/api/logout', {}, f.admin); assert.equal(r.status, 200);
  assert.ok(r.headers.get('Set-Cookie').includes('Max-Age=0'));
});
test('expired or wrong-audience tokens fail and other users or Google logins are denied', async t => {
  const f = fixture(t);
  for (const [value, status] of [[await token({}, 'wrong-project'), 401], [await token({}, 'test-project', 1), 401],
    [await token({ sub: 'someone-else' }), 403], [await token({ firebase: { sign_in_provider: 'google.com' } }), 403],
    [goodToken.slice(0, -5) + 'xxxxx', 401]]) {
    assert.equal((await f.call('/api/session', undefined, { Cookie: '__Host-lumiac-session=' + value })).status, status);
  }
});
test('hardware sync requires its own key, and never accepts a browser session', async t => {
  const f = fixture(t);
  assert.equal((await f.call('/device/v1/devices/clock-01/sync', { status: f.state }, f.admin)).status, 401);
  assert.equal((await f.call('/device/v1/devices/clock-01/sync', { status: f.state }, { 'X-Lumiac-Device-Key': 'wrong' })).status, 401);
  assert.equal((await f.sync()).status, 200);
  assert.equal((await f.call('/device/v1/devices/clock-01/sync', { status: [] }, f.device)).status, 400);
});

test('typed motion and lighting payloads are delivered once while a loop is running', async t => {
  const f = fixture(t); f.state.motion = {active: true}; await f.sync();
  for (const [type, values] of [['home', {}], ['pos3', {}], ['random_loop', {}], ['pause', {}], ['resume', {}],
    ['set_feed', {feed: 305}], ['set_brightness', {brightness: 179}], ['move', {axes: {X: 35.5, C: 90}, feed: 200}]]) {
    const queued = await f.action(type, values); assert.equal(queued.status, 202);
    const id = (await queued.json()).id;
    const received = (await (await f.sync()).json()).action;
    assert.equal(received.id, id); assert.equal(received.type, type);
    for (const [key, value] of Object.entries(values)) assert.deepEqual(received[key], value);
    assert.equal((await (await f.sync()).json()).action, null);
  }
});
test('invalid axes, feed, brightness and arbitrary G-code never enter the queue', async t => {
  const f = fixture(t); await f.sync();
  for (const [type, payload] of [['move', {axes: {X: 121}, feed: 100}], ['move', {axes: {Q: 10}, feed: 100}],
    ['move', {axes: {X: 10}, feed: 601}], ['move', {axes: {}, feed: 100}], ['set_feed', {feed: '100'}],
    ['set_brightness', {brightness: -1}], ['send_gcode', {gcode: 'M997 P3'}]]) {
    assert.equal((await f.action(type, payload)).status, 400);
  }
  assert.deepEqual(validateAction({type: 'home', gcode: 'M997 P3'}), {});
});
test('offline, stale and restarted controllers never receive an old queued move', async t => {
  const f = fixture(t); assert.equal((await f.action('pos1')).status, 409);
  await f.sync(); await f.action('pos1');
  f.state.session = 'new-boot'; assert.equal((await (await f.sync()).json()).action, null);
  f.advance(31000); assert.equal((await f.action('pos2')).status, 409);
});
test('Stop wins the queue and firmware work excludes other commands', async t => {
  const f = fixture(t); await f.sync(); await f.action('pos1'); await f.action('stop');
  assert.equal((await (await f.sync()).json()).action.type, 'stop');
  assert.equal((await (await f.sync()).json()).action, null);
  f.state.ota = {supported: true, active: true}; await f.sync();
  assert.equal((await f.action('home')).status, 409);
  assert.equal((await f.action('stop')).status, 202);
  assert.equal((await (await f.sync()).json()).action.type, 'stop');
});
function image(target) {
  const bytes = new Uint8Array(2048);
  if (target === 'octopus') {
    const view = new DataView(bytes.buffer); view.setUint32(0, 0x20010000, true); view.setUint32(4, 0x08008101, true);
  } else bytes[0] = 0xE9;
  bytes.set(new TextEncoder().encode('LUMIAC_OTA_ROLE:' + target + ' 2026-10-15.1 LUMIAC1-123456789abc'), 64);
  return bytes;
}
async function upload(f, target, bytes = image(target), headers = f.admin) {
  return f.gateway.fetch(new Request(f.env.LUMIAC_PUBLIC_ORIGIN + '/api/devices/clock-01/firmware', {
    method: 'POST', headers: {...headers, Origin: f.env.LUMIAC_PUBLIC_ORIGIN, 'Content-Type': 'application/octet-stream',
      'X-Lumiac-Target': target, 'X-Lumiac-Kind': target === 'octopus' ? 'octopus' : 'app'}, body: bytes,
  }), f.env);
}
test('firmware uploads validate role, hardware identity, image headers and authentication', async t => {
  const f = fixture(t);
  assert.equal((await upload(f, 'esp12', image('esp12'), {})).status, 401);
  assert.equal((await upload(f, 'esp32', image('esp12'))).status, 400);
  for (const target of ['esp12', 'esp32', 'octopus']) assert.equal((await upload(f, target)).status, 201);
  const bad = image('octopus'); bad[4] = 0; assert.throws(() => validateFirmware(bad, 'octopus', 'octopus'));
  assert.equal(firmwareCrc32(new TextEncoder().encode('123456789')), 0xCBF43926);
});
test('only the connected ESP variant can update, and Octopus needs its initial receiver', async t => {
  const f = fixture(t);
  Object.assign(f.state, {controller: 'esp32', firmware: 'LUMIAC1-123456789abc', octopusOnline: true, ota: {supported: true, sdSupported: true}});
  await f.sync();
  const wrong = await (await upload(f, 'esp12')).json();
  assert.equal((await f.call('/api/devices/clock-01/updates', {imageId: wrong.id}, f.admin)).status, 409);
  const octopus = await (await upload(f, 'octopus')).json();
  f.state.ota.sdSupported = false; await f.sync();
  assert.equal((await f.call('/api/devices/clock-01/updates', {imageId: octopus.id}, f.admin)).status, 409);
  f.state.ota.sdSupported = true; f.state.motion = {active: true}; await f.sync();
  const queued = await f.call('/api/devices/clock-01/updates', {imageId: octopus.id}, f.admin);
  assert.equal(queued.status, 202);
  const received = await (await f.sync()).json(); assert.equal(received.update.target, 'octopus');
  assert.equal((await (await f.sync()).json()).update, undefined);
  assert.equal((await f.action('pos1')).status, 409);
  assert.equal((await f.action('stop')).status, 202);
});
test('firmware completion is matched by job ID and SHA-256 and never replays a flash', async t => {
  const f = fixture(t); Object.assign(f.state, {controller: 'esp32', firmware: 'LUMIAC1-123456789abc', ota: {supported: true}}); await f.sync();
  const stored = await (await upload(f, 'esp32')).json();
  const job = await (await f.call('/api/devices/clock-01/updates', {imageId: stored.id}, f.admin)).json();
  const delivered = (await (await f.sync()).json()).update; assert.equal(delivered.id, job.id);
  f.state.ota = {...f.state.ota, id: job.id, sha256: stored.sha256, state: 'complete'};
  f.state.session = 'new-controller-boot'; await f.sync();
  const status = await (await f.call('/api/devices/clock-01/status', undefined, f.admin)).json();
  assert.equal(status.update.state, 'complete'); assert.equal(status.queued, 0);
  assert.equal((await (await f.sync()).json()).update, undefined);
});
test('only matching byte/phase progress renews an update lease; stalled progress expires', async t => {
  const f = fixture(t);
  Object.assign(f.state, {controller: 'esp32', firmware: 'LUMIAC1-123456789abc', ota: {supported: true}});
  await f.sync(); const stored = await (await upload(f, 'esp32')).json();
  const job = await (await f.call('/api/devices/clock-01/updates', {imageId: stored.id}, f.admin)).json();
  await f.sync();
  Object.assign(f.state.ota, {id: job.id, sha256: stored.sha256, active: true, state: 'downloading', done: 10});
  await f.sync();
  const lease = () => f.sql.prepare('SELECT expires_at FROM ota_jobs WHERE id=?').get(job.id).expires_at;
  const first = lease(); f.advance(120000); await f.sync(); assert.equal(lease(), first);
  f.state.ota.done = 20; await f.sync(); const second = lease(); assert.ok(second > first);
  f.advance(120000); f.state.ota.sha256 = '0'.repeat(64); f.state.ota.done = 30;
  await f.sync(); assert.equal(lease(), second);
  f.advance(60001); await f.sync();
  assert.equal(f.sql.prepare('SELECT state FROM ota_jobs WHERE id=?').get(job.id).state, 'expired');
});
test('an unconfirmed controller reboot expires the old update and never resubmits it', async t => {
  const f = fixture(t);
  Object.assign(f.state, {controller: 'esp12', firmware: 'LUMIAC1-123456789abc', ota: {supported: true}});
  await f.sync(); const stored = await (await upload(f, 'esp12')).json();
  const job = await (await f.call('/api/devices/clock-01/updates', {imageId: stored.id}, f.admin)).json();
  await f.sync(); f.state.session = 'rebooted';
  assert.equal((await (await f.sync()).json()).update, undefined);
  const result = f.sql.prepare('SELECT state,error FROM ota_jobs WHERE id=?').get(job.id);
  assert.equal(result.state, 'expired'); assert.match(result.error, /Controller restarted/);
});
test('saved diagnostics are deduplicated and remain available offline after reboot', async t => {
  const f = fixture(t);
  assert.equal((await f.call('/api/devices/clock-01/diagnostics')).status, 401);
  f.state.diagnostics = [{id: 'p1', boot: 'previous-boot', at: 300, utc: 1792248000, text: 'OTA esp12 verifying', persistent: true}];
  await f.sync(); await f.sync(); f.state.session = 'new-boot'; await f.sync(); f.advance(60000);
  const response = await f.call('/api/devices/clock-01/diagnostics', undefined, f.admin);
  assert.equal(response.status, 200); assert.match(response.headers.get('Content-Disposition'), /attachment/);
  const saved = await response.json(); assert.equal(saved.lastKnownStatus.session, 'new-boot');
  const events = saved.events.filter(event => event.message === 'OTA esp12 verifying');
  assert.equal(events.length, 1); assert.equal(events[0].persistent, 1);
  assert.equal((await (await f.call('/api/devices/clock-01/status', undefined, f.admin)).json()).online, false);
});
