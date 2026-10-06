const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
// npm install --prefix .test-tools --no-save --no-package-lock jsdom@22.1.0
let JSDOM;
try { ({JSDOM} = require('../.test-tools/node_modules/jsdom')); }
catch { ({JSDOM} = require('../Docs/blue/.test-tools/node_modules/jsdom')); }
const header = fs.readFileSync(path.join(__dirname, '../ESP_RF_Octopus/src/web_page.h'), 'utf8');
const html = header.split('R"HTML(')[1].split(')HTML";')[0];
const requests = [];
const socketRequests = [], timers = new Map();
let now = 0, nextTimer = 0, socket;
function advance(ms) {
  now += ms;
  for (const [id, timer] of [...timers]) if (timer.at <= now) {
    timers.delete(id); timer.fn();
  }
}
// Pairing must work with only USB power to the ESP32, before Octopus is attached.
const offlineState = {
  type: 'state', octopusOnline: false, remoteOnline: false,
  lights: {on: false, brightness: 0}, motion: {realtimeReady: false}
};
let failure = '', bluetooth = {
  supported: true, ready: true, connected: false, connecting: false, scanning: false,
  message: 'Ready', savedAddress: 'AA:BB:CC:DD:EE:FF', name: 'POP Multimedia',
  devices: [{ name: '<img src=x onerror=alert(1)>', address: 'AA:BB:CC:DD:EE:FF', rssi: -55 }]
};
const dom = new JSDOM(html, { url: 'http://192.168.4.1/', runScripts: 'dangerously', beforeParse(w) {
  w.performance.now = () => now;
  w.setTimeout = (fn, ms) => { timers.set(++nextTimer, {fn, at: now + ms}); return nextTimer; };
  w.clearTimeout = id => timers.delete(id);
  w.WebSocket = class extends w.EventTarget {
    static OPEN = 1;
    readyState = 1;
    constructor() { super(); socket = this; }
    send(body) { socketRequests.push(JSON.parse(body)); }
  };
  w.fetch = async (url, options = {}) => {
    if (options.method === 'POST') requests.push(JSON.parse(options.body));
    return { ok: !failure, status: failure ? 409 : 200, json: async () => failure ? {ok: false, message: failure} :
      options.method === 'POST' ? {ok: true} : url === '/api/bluetooth' ? structuredClone(bluetooth) :
      structuredClone(offlineState) };
  };
}});
const tick = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  await tick(); await tick();
  const w = dom.window, d = w.document;
  const frame = data => socket.dispatchEvent(new w.MessageEvent('message', {data: JSON.stringify(data)}));
  const btKey = index => d.querySelector('#bluetoothKeyRow [data-key="' + index + '"]');
  assert.equal(d.querySelector('#bluetoothCard').hidden, false);
  const actions = d.querySelector('#bluetoothActions');
  const actionStyle = w.getComputedStyle(actions);
  assert.equal(actions.classList.contains('button-row'), false);
  assert.equal(actionStyle.display, 'grid');
  assert.equal(actionStyle.width, '100%');
  assert.equal(actionStyle.gridAutoFlow, 'column');
  assert.equal(actionStyle.gridAutoColumns, 'minmax(0, 1fr)');
  assert.equal(actionStyle.gap, '8px');
  for (const button of actions.children) {
    assert.equal(w.getComputedStyle(button).minWidth, '0');
    assert.equal(w.getComputedStyle(button).whiteSpace, 'normal');
  }
  assert.equal(d.querySelector('#bluetoothCard .hint-list'), null);
  assert.equal(d.querySelector('#motionStatus'), null);
  assert.equal(d.querySelector('#bluetoothDisconnect').hidden, true);
  assert.equal(d.querySelector('#octopusStatus').textContent, 'Off');
  assert.equal(d.querySelector('#bluetoothScan').disabled, false);
  d.querySelector('#bluetoothScan').click(); await tick(); await tick();
  assert.deepEqual(requests.at(-1), {action: 'scan', address: ''});
  assert.equal(d.querySelector('#bluetoothError').hidden, true);
  assert.equal(d.querySelector('#bluetoothDevices button').disabled, false);
  assert.equal(w.getComputedStyle(d.querySelector('.bluetooth-device')).gridTemplateColumns, 'minmax(0, 1fr) auto');
  assert.equal(d.querySelector('#bluetoothDevices img'), null);
  assert.match(d.querySelector('#bluetoothDevices').textContent, /<img/);
  d.querySelector('#bluetoothDevices button').click(); await tick(); await tick();
  assert.deepEqual(requests.at(-1), {action: 'connect', address: 'AA:BB:CC:DD:EE:FF'});
  failure = 'Pairing rejected';
  d.querySelector('#bluetoothScan').click(); await tick(); await tick();
  assert.equal(d.querySelector('#bluetoothError').textContent, 'Pairing rejected');
  assert.equal(d.querySelector('#bluetoothError').hidden, false);
  failure = '';
  bluetooth.connecting = true;
  await w.refreshBluetooth();
  assert.equal(d.querySelector('#bluetoothScan').disabled, true);
  assert.equal(d.querySelector('#bluetoothScan').hidden, true);
  assert.equal(d.querySelector('#bluetoothDevices button').disabled, true);
  assert.equal(d.querySelector('#bluetoothDisconnect').disabled, false);
  assert.equal(d.querySelector('#bluetoothDisconnect').hidden, false);
  bluetooth.connecting = false; bluetooth.connected = true; bluetooth.controlsReady = true;
  await w.refreshBluetooth();
  // Continued Octopus-offline updates must not hide the Bluetooth connection.
  w.applyState(structuredClone(offlineState));
  assert.equal(d.querySelector('#octopusStatus').textContent, 'Off');
  assert.equal(d.querySelector('#remoteStatus').textContent, 'Bluetooth');
  assert.equal(d.querySelector('#rfKeyRow').hidden, true);
  assert.equal(d.querySelector('#bluetoothKeyRow').hidden, false);
  // The real WebSocket path must show every key, even with Octopus offline.
  for (let key = 0; key < 5; ++key) {
    frame({type: 'remote_keys', bluetoothKeys: {down: 1 << key, held: 0}});
    assert.equal(btKey(key).classList.contains('active'), true);
    assert.equal(w.getComputedStyle(btKey(key).querySelector('.remote-key-dot')).backgroundColor, 'rgb(36, 161, 72)');
    frame({type: 'remote_keys', bluetoothKeys: {down: 0, held: 0}});
    assert.equal(btKey(key).classList.contains('active'), true); // Quick tap remains visible.
    advance(180);
    assert.equal(btKey(key).classList.contains('active'), false);
  }
  frame({type: 'remote_keys', bluetoothKeys: {down: 17, held: 1}});
  assert.equal(btKey(0).classList.contains('held'), true);
  assert.equal(btKey(4).classList.contains('active'), true);
  assert.equal(w.getComputedStyle(btKey(0).querySelector('.remote-key-dot')).backgroundColor, 'rgb(229, 139, 36)');
  frame({type: 'remote_keys', bluetoothKeys: {down: 0, held: 0}});
  assert.equal(btKey(0).classList.contains('held'), false);
  advance(180);
  // Re-pressing a key must cancel its previous delayed release.
  frame({type: 'remote_keys', bluetoothKeys: {down: 2, held: 0}});
  frame({type: 'remote_keys', bluetoothKeys: {down: 0, held: 0}});
  advance(90);
  frame({type: 'remote_keys', bluetoothKeys: {down: 2, held: 2}});
  advance(100);
  assert.equal(btKey(1).classList.contains('held'), true);
  // Phone clicks wait for authoritative selection; remote/other-phone updates use the same state.
  d.querySelector('#pos2Button').click();
  assert.deepEqual(socketRequests.at(-1), {type: 'action', action: 'pos2'});
  function checkPreset(preset, phase) {
    frame({...offlineState, motion: {preset, presetState: phase}});
    for (let index = 1; index <= 3; ++index) {
      const button = d.querySelector('#pos' + index + 'Button');
      const current = index === preset;
      assert.equal(button.classList.contains('selected'), current && phase === 'reached');
      assert.equal(button.classList.contains('running'), current && phase === 'moving');
      assert.equal(button.getAttribute('aria-pressed'), String(current && phase !== 'idle'));
      assert.equal(w.getComputedStyle(button).backgroundColor,
        current && phase === 'reached' ? 'rgb(36, 120, 62)' :
        current && phase === 'moving' ? 'rgb(229, 139, 36)' : 'rgb(209, 206, 199)');
    }
  }
  for (const preset of [2, 3, 1]) {
    checkPreset(preset, 'moving');
    frame({...offlineState, motion: {preset, presetState: 'moving', paused: true}});
    assert.equal(d.querySelector('#pos' + preset + 'Button').classList.contains('running'), true);
    assert.match(d.querySelector('#pos' + preset + 'Button').getAttribute('aria-label'), /paused/);
    checkPreset(preset, 'reached');
  }
  checkPreset(2, 'moving');
  checkPreset(3, 'moving'); // Switching target clears the previous orange button.
  checkPreset(0, 'idle'); // Abort/timeout/standby cannot produce a green button.
  checkPreset(3, 'moving');
  frame({...offlineState, motion: {preset: 3, presetState: 'moving'}, lights: {on: true, brightness: 173}});
  assert.equal(d.querySelector('#pos3Button').classList.contains('running'), true);
  checkPreset(3, 'reached');
  frame({...offlineState, motion: {preset: 3, presetState: 'reached'}, bluetoothKeys: {down: 1, held: 1}});
  frame({...offlineState, motion: {preset: 3, presetState: 'reached'}, lights: {on: true, brightness: 184}});
  assert.equal(d.querySelector('#pos3Button').classList.contains('selected'), true);
  // A long-press standby update must turn off every lamp indicator, even if
  // Octopus is disconnected or has not advertised its firmware capabilities.
  for (const octopusOnline of [false, true]) {
    frame({...offlineState, octopusOnline, lights: {on: true, brightness: 179}});
    assert.equal(d.querySelector('#lampSwitch').checked, true);
    assert.equal(d.querySelector('#lampReadout').textContent, 'ON 70');
    frame({...offlineState, octopusOnline, motion: {standby: true, realtimeReady: octopusOnline},
      lights: {on: false, brightness: 0}, bluetoothKeys: {down: 16, held: 16}});
    assert.equal(d.querySelector('#lampSwitch').checked, false);
    assert.equal(d.querySelector('#lampSlider').value, '0');
    assert.equal(d.querySelector('#lampReadout').textContent, 'OFF 0');
    assert.equal(d.querySelector('#legControls').style.getPropertyValue('--lamp-level'), '0');
    // Brightness-only commands in standby must still be displayed normally.
    frame({...offlineState, motion: {standby: true}, lights: {on: true, brightness: 5}});
    assert.equal(d.querySelector('#lampReadout').textContent, 'ON 2');
  }
  socket.dispatchEvent(new w.Event('close'));
  assert.equal(d.querySelectorAll('.remote-key.active, .remote-key.held').length, 0);
  assert.equal(d.querySelector('#pos3Button').classList.contains('selected'), true);
  assert.equal(d.querySelectorAll('#bluetoothDevices button').length, 0);
  assert.equal(d.querySelector('#bluetoothDevices').hidden, true);
  assert.equal(d.querySelector('#bluetoothStatus').textContent, 'POP Multimedia · Connected');
  d.querySelector('#bluetoothDisconnect').click(); await tick(); await tick();
  assert.equal(requests.at(-1).action, 'disconnect');
  d.querySelector('#bluetoothForget').click(); await tick(); await tick();
  assert.equal(requests.at(-1).action, 'forget');
  assert.equal(d.querySelector('#bluetoothError').hidden, true);
  bluetooth.connected = false; bluetooth.controlsReady = false;
  bluetooth.ready = false; bluetooth.message = 'Bluetooth unavailable. Restart ESP32.';
  await w.refreshBluetooth();
  assert.equal(d.querySelector('#bluetoothScan').disabled, true);
  assert.equal(d.querySelector('#bluetoothDisconnect').hidden, true);
  assert.match(d.querySelector('#bluetoothStatus').textContent, /Bluetooth unavailable/);
  assert.equal(d.querySelector('#rfKeyRow').hidden, false);
  frame({type: 'remote_keys', rfKeys: {down: 4, held: 0}});
  assert.equal(d.querySelector('#remoteKeyPos1').classList.contains('active'), true);
  frame({type: 'remote_keys', rfKeys: {down: 4, held: 4}});
  assert.equal(d.querySelector('#remoteKeyPos1').classList.contains('held'), true);
  frame({type: 'remote_keys', rfKeys: {down: 0, held: 0}});
  assert.equal(d.querySelector('#remoteKeyPos1').classList.contains('held'), false);
  bluetooth.supported = false; await w.refreshBluetooth();
  assert.equal(d.querySelector('#bluetoothCard').hidden, true);
  console.log('PASS standalone pairing, full-width Bluetooth actions, live RF/Bluetooth press/hold colors, quick taps, disconnects, orange/green/gray POS lifecycle and ESP8266 fallback');
  dom.window.close();
})().catch(error => { console.error(error); dom.window.close(); process.exitCode = 1; });
