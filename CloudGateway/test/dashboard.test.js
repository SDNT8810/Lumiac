import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { JSDOM } from 'jsdom';

async function dashboard(patch = {}, online = true, fullUpdate = null) {
  const html = readFileSync(new URL('../public/index.html', import.meta.url), 'utf8');
  const dom = new JSDOM(html, { url: 'https://lumiac.control.timiac.it', runScripts: 'outside-only' });
  const requests = [], state = { controller: 'esp32', octopusOnline: true, homed: true,
    software: '2026-10-15.1', positions: {X: 10}, feed: 305, lights: {brightness: 179},
    ota: {supported: true, sdSupported: true}, ...patch };
  dom.window.setInterval = () => 1; dom.window.clearInterval = () => {};
  dom.window.fetch = async (path, options = {}) => {
    requests.push({path, options});
    if (path.endsWith('/config')) return Response.json({deviceId: 'lumiac-01'});
    if (path.endsWith('/status')) return Response.json({online, queued: 0, status: state, fullUpdate});
    if (path.endsWith('/firmware')) return Response.json({id: 'image-one'});
    return Response.json({id: 'request-one'});
  };
  dom.window.eval(readFileSync(new URL('../public/app.js', import.meta.url), 'utf8'));
  await new Promise(resolve => setImmediate(resolve));
  return {dom, requests, $: id => dom.window.document.getElementById(id)};
}
test('the real dashboard enables connected controls and shows actual position data', async () => {
  const f = await dashboard();
  assert.equal(f.$('axis-X').textContent, '10.0 mm');
  assert.equal(f.$('move').disabled, false); assert.equal(f.$('stop').disabled, false);
  assert.equal(f.$('firmware-target').value, 'esp32:app');
  f.dom.window.document.querySelector('[data-action="pos3"]').click();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(JSON.parse(f.requests.find(r => r.path.endsWith('/actions')).options.body).type, 'pos3');
  f.dom.window.close();
});
test('updates block motion and settings while Stop remains available', async () => {
  const f = await dashboard({ota: {supported: true, active: true, id: 'job', phase: 'verifying SD', size: 100, done: 50}});
  assert.ok([...f.dom.window.document.querySelectorAll('[data-action]')].every(button => button.disabled));
  for (const id of ['feed', 'brightness', 'move', 'install-firmware']) assert.equal(f.$(id).disabled, true);
  assert.equal(f.$('stop').disabled, false);
  assert.match(f.$('firmware-status').textContent, /verifying SD.*50%/);
  f.dom.window.close();
});
test('firmware form uploads the selected bytes and queues exactly the returned image', async () => {
  const f = await dashboard(), file = new f.dom.window.File(['bytes'], 'octopus.bin');
  Object.defineProperty(f.$('firmware-file'), 'files', {value: [file]});
  f.$('firmware-target').value = 'octopus:octopus';
  await f.$('firmware-form').onsubmit({preventDefault() {}});
  const request = f.requests.find(r => r.path.endsWith('/firmware'));
  assert.equal(request.options.body, file); assert.equal(request.options.headers['X-Lumiac-Target'], 'octopus');
  assert.deepEqual(JSON.parse(f.requests.find(r => r.path.endsWith('/updates')).options.body), {imageId: 'image-one'});
  f.dom.window.close();
});
test('offline controllers cannot accept movement or updates', async () => {
  const f = await dashboard({}, false);
  assert.equal(f.$('stop').disabled, true); assert.equal(f.$('move').disabled, true);
  assert.match(f.$('connection').textContent, /offline/); f.dom.window.close();
});


test('Full update starts a server build with no individual file uploads', async () => {
  const f = await dashboard();
  assert.equal(f.$('full-update').disabled, false); await f.$('full-update').onclick();
  const calls = f.requests.filter(r => r.path.endsWith('/full-update'));
  assert.equal(calls.length, 1); assert.deepEqual(JSON.parse(calls[0].options.body), {});
  assert.equal(f.requests.some(r => r.path.endsWith('/firmware') || r.path.endsWith('/updates')), false);
  f.dom.window.close();
});

test('durable Full update status is restored after reopening the page', async () => {
  const f = await dashboard({}, true, {state: 'building', steps: []});
  assert.match(f.$('full-update-status').textContent, /Building all firmware/);
  assert.equal(f.$('full-update').disabled, true); assert.equal(f.$('feed').disabled, true);
  assert.equal(f.$('stop').disabled, false); f.dom.window.close();
});
