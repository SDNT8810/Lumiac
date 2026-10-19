const $ = id => document.getElementById(id);
let config, timer, pending = '', uploading = false, targetInitialized = false;
function say(message, error = false) { $('message').textContent = message; $('message').classList.toggle('error', error); }
function login(message = '') { clearInterval(timer); timer = null; $('dashboard').hidden = true; $('sign-in').hidden = false; $('login-message').textContent = message; $('password').value = ''; }
async function api(path, options = {}) {
  const response = await fetch(path, { ...options, credentials: 'same-origin', cache: 'no-store', headers: { 'Content-Type': 'application/json', ...options.headers } });
  const body = await response.json();
  if (!response.ok) { if (response.status === 401 && path !== '/api/login') login(body.error); throw Error(body.error || 'HTTP ' + response.status); }
  return body;
}
function render(remote) {
  const state = remote.status || {}, ota = state.ota || {}, motion = state.motion || {};
  $('diagnostic-download').href = '/api/devices/' + config.deviceId + '/diagnostics';
  $('reliability').textContent = state.reliability?.lastFault ? 'Last interruption: ' + state.reliability.lastFault : '';
  const busy = ['building', 'installing'].includes(remote.fullUpdate?.state) || uploading || !remote.online || ota.active || remote.queued > 0;
  $('connection').textContent = !remote.online ? 'Lumiac is offline' : ['building', 'installing'].includes(remote.fullUpdate?.state) ? 'Full update in progress. Stop is available.' : ota.active ? 'Firmware update in progress' : remote.queued ? 'Waiting for the controller to receive your command' : 'Connected to Lumiac';
  $('hardware').textContent = remote.online ? `${state.controller === 'esp12' ? 'ESP-12S' : 'ESP32'} · firmware ${state.software || 'unknown'}` : 'Connect the controller to its router using http://192.168.4.1/network on Lumiac Wi-Fi.';
  $('octopus-state').textContent = !state.octopusOnline ? 'Offline' : state.homing ? 'Homing' : state.homed ? 'Ready' : 'Home required';
  $('motion-state').textContent = motion.standby ? 'Standby' : motion.paused ? 'Paused' : motion.active ? 'Moving' : 'Stopped';
  $('wifi-state').textContent = state.wifi?.connected ? state.wifi.ssid || 'Connected' : 'Disconnected';
  for (const axis of 'XYZABC') $('axis-' + axis).textContent = remote.online && Number.isFinite(state.positions?.[axis]) ? state.positions[axis].toFixed(1) + ' mm' : '—';
  document.querySelectorAll('[data-action]').forEach(button => { button.disabled = busy || (!state.octopusOnline && button.dataset.action !== 'reset_octopus'); });
  $('stop').disabled = !remote.online;
  for (const id of ['brightness', 'feed']) {
    $(id).disabled = busy || !state.octopusOnline;
    if (document.activeElement !== $(id)) $(id).value = id === 'brightness' ? state.lights?.brightness ?? 179 : state.feed ?? 305;
  }
  $('brightness-value').textContent = Math.round(Number($('brightness').value) / 255 * 100) + '%';
  $('feed-value').textContent = $('feed').value + ' mm/min';
  $('move').disabled = busy || !state.octopusOnline || !state.homed;
  for (const id of ['axis', 'position']) $(id).disabled = busy;
  if (!targetInitialized && state.controller) { $('firmware-target').value = state.controller + ':app'; targetInitialized = true; }
  $('install-firmware').disabled = busy || !ota.supported;
  const full = remote.fullUpdate;
  const job = remote.update;
  $('full-update-log').hidden = !full?.buildUrl;
  if (full?.buildUrl) $('full-update-log').href = full.buildUrl;
  const fullActive = ['building', 'installing'].includes(full?.state);
  $('full-update').disabled = busy || fullActive || !state.ota?.supported;
  const completed = full?.steps?.filter(step => step.state === 'complete').length || 0;
  const current = full?.steps?.find(step => step.state !== 'complete');
  const previousFull = full && !fullActive && job?.createdAt > full.updated_at;
  const fullResult = !full ? 'Build all firmware and install it automatically. No files to select.'
    : full.state === 'building' ? 'Building all firmware. This can take several minutes. You may close this page.'
    : full.state === 'installing' ? `Installing ${completed + 1}/${full.steps.length}: ${current?.target || ''} ${current?.kind === 'filesystem' ? 'dashboard' : ''}. Keep power connected.`
    : full.state === 'complete' ? full.validate_only ? 'Build verified. No hardware was updated.' : 'Full update complete. Calibrate/Home the arms before resuming.'
    : `Full update ${full.state}: ${full.error || 'Check the connection and try again.'}`;
  $('full-update-status').textContent = previousFull ? 'Individual component updates were started after the previous Full update. Latest result is shown below.' : fullResult;
  $('full-update-status').classList.toggle('error', !previousFull && full?.state === 'failed');
  $('full-update-history').hidden = !previousFull;
  $('full-update-history-status').textContent = previousFull ? fullResult : '';
  const jobTerminal = ['complete', 'failed', 'expired', 'cancelled'].includes(job?.state);
  const useJob = job && (job.id !== ota?.id || jobTerminal);
  const progress = ota?.size ? ` · ${Math.min(100, Math.round(ota.done / ota.size * 100))}%` : '';
  $('firmware-status').textContent = !ota?.supported ? 'Install the initial USB/SD update package before using remote updates.'
    : useJob ? `${job.target || 'Update'} · ${job.state}${job.error ? ': ' + job.error : ''}`
    : ota?.id ? `${ota.target} · ${ota.phase || ota.state}${ota.active ? progress : ''}${ota.error ? ': ' + ota.error : ''}`
    : 'Ready for remote updates. Leave the Octopus microSD installed.';
  $('firmware-status').classList.toggle('error', useJob ? ['failed', 'expired'].includes(job.state) : ota?.state === 'failed');
  if (pending && state.remoteAction?.id === pending) { say(state.remoteAction.error || 'Command accepted.', Boolean(state.remoteAction.error)); pending = ''; }
}
async function refresh() { try { render(await api('/api/devices/' + config.deviceId + '/status')); } catch (error) { say(error.message, true); } }
async function queue(type, values = {}) {
  const action = await api('/api/devices/' + config.deviceId + '/actions', { method: 'POST', body: JSON.stringify({ type, ...values }) });
  pending = action.id; say('Command queued for Lumiac.'); await refresh();
}
async function start() {
  await api('/api/session'); config = await api('/api/config');
  $('sign-in').hidden = true; $('dashboard').hidden = false;
  await refresh(); clearInterval(timer); timer = setInterval(refresh, 3000);
}
$('positions').innerHTML = [...'XYZABC'].map(axis => `<div><small>${axis}</small><strong id="axis-${axis}">—</strong></div>`).join('');
$('login-form').onsubmit = async event => {
  event.preventDefault(); $('login-button').disabled = true;
  try { await api('/api/login', { method: 'POST', body: JSON.stringify({ username: $('username').value, password: $('password').value }) }); $('password').value = ''; await start(); }
  catch (error) { $('login-message').textContent = error.message; } finally { $('login-button').disabled = false; }
};
document.querySelectorAll('[data-action]').forEach(button => button.onclick = () => queue(button.dataset.action).catch(error => say(error.message, true)));
$('stop').onclick = () => queue('stop').catch(error => say(error.message, true));
$('brightness').oninput = () => { $('brightness-value').textContent = Math.round(Number($('brightness').value) / 255 * 100) + '%'; };
$('brightness').onchange = () => queue('set_brightness', { brightness: Number($('brightness').value) }).catch(error => say(error.message, true));
$('feed').oninput = () => { $('feed-value').textContent = $('feed').value + ' mm/min'; };
$('feed').onchange = () => queue('set_feed', { feed: Number($('feed').value) }).catch(error => say(error.message, true));
$('move-form').onsubmit = event => { event.preventDefault(); queue('move', { axes: { [$('axis').value]: Number($('position').value) }, feed: Number($('feed').value) }).catch(error => say(error.message, true)); };
$('firmware-form').onsubmit = async event => {
  event.preventDefault(); const file = $('firmware-file').files[0]; if (!file || uploading) return;
  const [target, kind] = $('firmware-target').value.split(':'); uploading = true; $('install-firmware').disabled = true; say('Uploading and checking firmware…');
  try {
    const image = await api('/api/devices/' + config.deviceId + '/firmware', { method: 'POST', headers: { 'Content-Type': 'application/octet-stream', 'X-Lumiac-Target': target, 'X-Lumiac-Kind': kind }, body: file });
    await api('/api/devices/' + config.deviceId + '/updates', { method: 'POST', body: JSON.stringify({ imageId: image.id }) });
    say('Firmware update queued. Motion will stop and stay parked.'); $('firmware-file').value = '';
  } catch (error) { say(error.message, true); } finally { uploading = false; await refresh(); }
};
$('sign-out').onclick = async () => { try { await api('/api/logout', { method: 'POST' }); login(); } catch (error) { say(error.message, true); } };
start().catch(error => login(error.message === 'Please sign in' ? '' : error.message));

$('full-update').onclick = async () => {
  $('full-update').disabled = true;
  uploading = true;
  try {
    await api('/api/devices/' + config.deviceId + '/full-update', { method: 'POST', body: '{}' });
    say('Building all firmware. Updates will install automatically when the build succeeds.');
  } catch (error) { say(error.message, true); }
  finally { uploading = false; await refresh(); }
};
