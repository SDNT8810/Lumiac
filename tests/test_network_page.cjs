const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const {JSDOM} = require('../CloudGateway/node_modules/jsdom');
const header = fs.readFileSync(path.join(__dirname, '../ESP_RF_Octopus/src/network_page.h'), 'utf8');
const html = header.match(/R"HTML\(([\s\S]*)\)HTML"/)[1];
const tick = () => new Promise(resolve => setImmediate(resolve));
const snapshot = (extra = {}) => ({controller: 'esp32', software: 'test', sourceBranch: 'onetoone',
  wifi: {ssid: 'studio', connected: false, cloud: 'connecting to Wi-Fi', error: '', ip: '', gateway: '',
    dns: '', rssi: -60, utcEpoch: 0, statusCode: 0, disconnectReason: 0, attempts: 1, ...extra},
  reliability: {freeHeap: 100000, largestHeapBlock: 50000, cloudRequestMs: 0}});
const reply = (status = snapshot(), ok = true) => ({ok, status: ok ? 200 : 400, json: async () => status});
function fixture(respond) {
  const dom = new JSDOM(html, {runScripts: 'outside-only', url: 'http://192.168.4.1/network'});
  const w = dom.window, requests = [], timers = new Map();
  let sequence = 0;
  w.setTimeout = (fn, delay) => { timers.set(++sequence, {fn, delay}); return sequence; };
  w.clearTimeout = id => timers.delete(id);
  w.fetch = (url, options) => { requests.push({url, ...options}); return respond(requests.at(-1), requests.length); };
  w.eval(w.document.querySelector('script').textContent);
  return {w, requests, timers, $: id => w.document.getElementById(id), close: () => dom.window.close()};
}
test('blank saved password is omitted; joining is acknowledged and diagnostics include the reason', async () => {
  const f = fixture(async request => reply(snapshot(request.method === 'POST' ? {pending: true} : {})));
  await tick();
  assert.equal(f.$('ssid').value, 'studio');
  await f.$('network').onsubmit({preventDefault() {}});
  const body = JSON.parse(f.requests.at(-1).body);
  assert.equal(body.ssid, 'studio'); assert.equal(body.openNetwork, false);
  assert.equal('password' in body, false);
  assert.match(f.$('state').textContent, /settings saved/i);
  assert.match(f.$('diagnostics').textContent, /Disconnect reason: none/);
  assert.match(f.$('diagnostics').textContent, /UTC: not synchronized/);
  f.close();
});
test('repeated clicks cannot submit twice; open network clearing is explicit', async () => {
  let finish;
  const f = fixture(async request => request.method === 'POST' ? new Promise(resolve => {finish = resolve;}) : reply());
  await tick();
  f.$('password').value = 'new-secret'; f.$('open-network').checked = true;
  const saving = f.$('network').onsubmit({preventDefault() {}});
  await f.$('network').onsubmit({preventDefault() {}});
  assert.equal(f.requests.filter(r => r.method === 'POST').length, 1);
  assert.deepEqual(JSON.parse(f.requests.at(-1).body), {ssid: 'studio', openNetwork: true});
  assert.equal(f.$('save').disabled, true);
  finish(reply(snapshot({pending: true}))); await saving;
  assert.equal(f.$('save').disabled, false); assert.equal(f.$('password').value, '');
  f.close();
});
test('local connection failures retry reads but never replay an uncertain save', async () => {
  const f = fixture(async () => {throw new TypeError('Load failed');});
  await tick();
  assert.match(f.$('state').textContent, /Reconnect your phone to Lumiac Wi-Fi/);
  f.$('ssid').value = 'studio'; f.$('password').value = 'new-secret';
  await f.$('network').onsubmit({preventDefault() {}});
  assert.match(f.$('state').textContent, /Could not confirm the save/);
  const timer = [...f.timers.values()].find(t => t.delay === 3000);
  await timer.fn();
  assert.equal(f.requests.filter(r => r.method === 'POST').length, 1);
  assert.equal(f.requests.at(-1).method, undefined);
  assert.equal(f.$('password').value, 'new-secret');
  f.close();
});
test('polling does not overlap and an old response cannot replace a successful save', async () => {
  let finishRead;
  const f = fixture(async request => request.method === 'POST' ? reply(snapshot({pending: true})) :
    new Promise(resolve => {finishRead = resolve;}));
  await tick();
  await f.w.refresh(); assert.equal(f.requests.length, 1);
  f.$('ssid').value = 'studio';
  await f.$('network').onsubmit({preventDefault() {}});
  finishRead(reply(snapshot({cloud: 'old state'}))); await tick();
  assert.match(f.$('state').textContent, /settings saved/i);
  assert.doesNotMatch(f.$('state').textContent, /old state/);
  assert.equal([...f.timers.values()].filter(t => t.delay === 3000).length, 1);
  f.close();
});
test('controller validation errors remain visible', async () => {
  const f = fixture(async request => request.method === 'POST' ? reply({error: 'Enter a password'}, false) : reply());
  await tick();
  await f.$('network').onsubmit({preventDefault() {}});
  assert.equal(f.$('state').textContent, 'Enter a password'); f.close();
});
