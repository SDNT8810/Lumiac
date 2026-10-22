#pragma once
void handleNetworkPage() {
  static const char page[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lumiac Internet connection</title><style>body{font:17px system-ui;max-width:600px;margin:40px auto;padding:20px;background:#e9e4da;color:#263831}label,input,button{display:block;margin:12px 0}input,button{padding:12px;font:inherit}input{width:90%}input[type=checkbox]{display:inline;width:auto}a{color:inherit}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style>
<h1>Lumiac remote access</h1><p>Connect this controller to a 2.4 GHz router. The Lumiac local Wi-Fi remains available. Your phone may briefly disconnect while the controller joins the router; reconnect to Lumiac if needed.</p>
<form id="network"><label>Wi-Fi name<input id="ssid" maxlength="32" required></label><label>Wi-Fi password<input id="password" type="password" maxlength="63" autocomplete="new-password" placeholder="Leave blank to keep the saved password"></label><label><input id="open-network" type="checkbox"> This network has no password</label><button id="save">Save and connect</button></form><p id="state" role="status">Reading controller status...</p>
<details><summary>Connection details</summary><pre id="diagnostics"></pre></details>
<p>Local firmware updates: connect the PC to Lumiac Wi-Fi, then run <code>python local_update.py --target all</code> from the project folder. <a href="/api/diagnostics">Download diagnostic log</a>.</p>
<p>Remote panel: <a href="https://lumiac.control.timiac.it">lumiac.control.timiac.it</a></p><a href="/">Back to dashboard</a>
<script>
const $ = id => document.getElementById(id);
let pollTimer, reading = false, saving = false, initialized = false, generation = 0;
async function request(options = {}) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 8000);
  try {
    const response = await fetch('/api/network', {...options, cache: 'no-store', signal: controller.signal});
    const status = await response.json();
    if (!response.ok) throw Object.assign(new Error(status.error || 'Controller HTTP ' + response.status), {controllerError: true});
    return status;
  } finally { clearTimeout(timeout); }
}
function render(status) {
  const wifi = status.wifi, reliability = status.reliability;
  $('state').textContent = (wifi.pending ? 'Wi-Fi settings saved. ' : wifi.connected ? 'Router connected. ' : 'Router disconnected. ') +
    (wifi.cloud || '') + (wifi.error ? ' - ' + wifi.error : '');
  $('diagnostics').textContent = [status.controller + ' / ' + status.software + ' / ' + status.sourceBranch,
    'IP: ' + wifi.ip, 'Gateway: ' + wifi.gateway, 'DNS: ' + wifi.dns, 'Signal: ' + wifi.rssi + ' dBm',
    'Wi-Fi status: ' + wifi.statusCode, 'Disconnect reason: ' + (wifi.disconnectReason || 'none'),
    'Connection attempts: ' + wifi.attempts,
    'UTC: ' + (wifi.utcEpoch >= 1700000000 ? new Date(wifi.utcEpoch * 1000).toISOString() : 'not synchronized'),
    'Free RAM: ' + reliability.freeHeap + ' bytes', 'Largest block: ' + (reliability.largestHeapBlock ?? 'n/a'),
    'Last request: ' + reliability.cloudRequestMs + ' ms'].join('\n');
  if (!initialized) {
    if (!$('ssid').value) $('ssid').value = wifi.ssid || '';
    initialized = true;
  }
}
async function refresh() {
  if (reading || saving) return;
  reading = true;
  const requestedGeneration = generation;
  try {
    const status = await request();
    if (!saving && requestedGeneration === generation) render(status);
  } catch (error) {
    if (!saving && requestedGeneration === generation) $('state').textContent = error.controllerError ? error.message :
      'Cannot reach Lumiac locally. Reconnect your phone to Lumiac Wi-Fi and keep this page open. Retrying...';
  } finally { reading = false; }
}
async function poll() {
  await refresh();
  clearTimeout(pollTimer);
  pollTimer = setTimeout(poll, 3000);
}
$('open-network').onchange = () => { $('password').disabled = $('open-network').checked; };
$('network').onsubmit = async event => {
  event.preventDefault();
  if (saving) return;
  saving = true; ++generation; $('save').disabled = true;
  clearTimeout(pollTimer);
  $('state').textContent = 'Saving Wi-Fi settings...';
  const body = {ssid: $('ssid').value, openNetwork: $('open-network').checked};
  if (!body.openNetwork && $('password').value) body.password = $('password').value;
  try {
    const status = await request({method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)});
    $('password').value = '';
    render(status);
  } catch (error) {
    $('state').textContent = error.controllerError ? error.message :
      'Could not confirm the save. Reconnect to Lumiac Wi-Fi and check the status before saving again.';
  } finally {
    saving = false; $('save').disabled = false;
    clearTimeout(pollTimer); pollTimer = setTimeout(poll, 3000);
  }
};
poll();
</script></html>)HTML";
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", page);
}
