#pragma once

static const char OCTOPUS_WEB_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover" />
    <meta name="theme-color" content="#bfb7aa" />
    <meta name="mobile-web-app-capable" content="yes" />
    <meta name="apple-mobile-web-app-capable" content="yes" />
    <meta name="apple-mobile-web-app-status-bar-style" content="default" />
    <meta name="apple-mobile-web-app-title" content="WebApp" />
    <title>ESP_RF_Octopus</title>
    <link rel="manifest" href="/manifest.webmanifest" />
    <link rel="icon" href="/logo.jpg" type="image/jpeg" />
    <link rel="apple-touch-icon" href="/logo.jpg" />
    <style>
      :root {
        --bg: #e9e4da;
        --bg-shadow: #d7d1c6;
        --surface: rgba(255, 252, 247, 0.78);
        --surface-soft: rgba(247, 243, 236, 0.76);
        --ink: #26241f;
        --muted: #726d64;
        --line: rgba(74, 69, 60, 0.12);
        --metal-a: #7b7d80;
        --metal-b: #c2c4c7;
        --metal-c: #525458;
      }
      * { box-sizing: border-box; }
      html, body {
        margin: 0;
        min-height: 100%;
        background:
          radial-gradient(circle at top, rgba(255,255,255,0.58), transparent 38%),
          linear-gradient(180deg, var(--bg) 0%, var(--bg-shadow) 100%);
        color: var(--ink);
        font-family: "IBM Plex Sans", "Segoe UI", sans-serif;
        font-size: 14px;
      }
      body { min-height: 100vh; }
      .app-shell { min-height: 100vh; padding: 12px; }
      .panel { display: flex; flex-direction: column; gap: 12px; }
      .card {
        width: 100%;
        background: var(--surface);
        border: 1px solid var(--line);
        border-radius: 16px;
        padding: 12px;
        box-shadow: 0 12px 28px rgba(70, 64, 54, 0.08), inset 0 1px 0 rgba(255,255,255,0.48);
        backdrop-filter: blur(12px);
      }
      .command-grid, .terminal-grid {
        display: grid;
        grid-template-columns: 1fr;
        gap: 10px;
        width: 100%;
      }
      .command-block, .terminal-block {
        background: var(--surface-soft);
        border: 1px solid rgba(82,84,88,0.08);
        border-radius: 12px;
        padding: 10px;
      }
      .status-strip {
        display: grid;
        grid-template-columns: repeat(3, minmax(0, 1fr));
        gap: 8px;
        width: 100%;
      }
      .status-pill {
        display: flex;
        align-items: center;
        justify-content: center;
        gap: 8px;
        padding: 6px 10px;
        border-radius: 999px;
        border: 1px solid rgba(82,84,88,0.08);
        background: rgba(255,255,255,0.52);
        color: #4e5459;
        font-size: 0.74rem;
        width: 100%;
      }
      .status-pill.off .status-dot {
        background: #a79f95;
        box-shadow: none;
      }
      .status-dot {
        width: 7px;
        height: 7px;
        border-radius: 50%;
        background: #8fbc8f;
        box-shadow: 0 0 10px rgba(143,188,143,0.45);
      }
      .status-pill-label {
        text-transform: uppercase;
        letter-spacing: 0.06em;
        opacity: 0.7;
      }
      .button-row {
        display: grid;
        grid-template-columns: repeat(6, minmax(0, 1fr));
        gap: 8px;
      }
      .button-row button {
        padding: 8px 4px;
        font-size: 0.64rem;
        white-space: nowrap;
      }
      .remote-key-row {
        display: grid;
        grid-template-columns: repeat(7, minmax(0, 1fr));
        gap: 8px;
        align-items: start;
      }
      .remote-key {
        display: grid;
        justify-items: center;
        gap: 5px;
      }
      .remote-key-dot {
        width: 14px;
        height: 14px;
        border-radius: 50%;
        background: #a9a49b;
        border: 1px solid rgba(70, 64, 54, 0.16);
        box-shadow: inset 0 1px 0 rgba(255,255,255,0.44);
        transition: background 120ms ease, box-shadow 120ms ease, transform 120ms ease;
      }
      .remote-key.active .remote-key-dot {
        background: #5f646a;
        box-shadow: 0 0 0 3px rgba(95,100,106,0.14);
        transform: scale(1.05);
      }
      .remote-key-label {
        color: var(--muted);
        font-size: 0.61rem;
        line-height: 1.1;
        letter-spacing: 0.04em;
        text-transform: uppercase;
        text-align: center;
      }
      .card-head, .card-subhead {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 10px;
      }
      .card-head { margin-bottom: 10px; }
      .card-subhead { margin-bottom: 6px; }
      .card-head h2, .card-subhead h3 {
        margin: 0;
        font-size: 0.82rem;
        font-weight: 600;
      }
      .lamp-row {
        display: grid;
        grid-template-columns: 46px 1fr;
        gap: 10px;
        align-items: center;
      }
      button {
        border: 1px solid rgba(82,84,88,0.08);
        border-radius: 10px;
        padding: 9px 10px;
        background: linear-gradient(180deg, rgba(101,104,109,0.96), rgba(71,74,79,0.96));
        color: #f8f6f1;
        cursor: pointer;
        font: inherit;
        box-shadow: inset 0 1px 0 rgba(255,255,255,0.16);
      }
      .ghost-button {
        background: rgba(255,255,255,0.42);
        color: var(--ink);
      }
      .danger-button {
        background: linear-gradient(180deg, rgba(164,54,54,0.96), rgba(125,31,31,0.96));
        color: #fff8f6;
      }
      .switch {
        position: relative;
        display: inline-flex;
        width: 42px;
        height: 24px;
      }
      .switch input {
        position: absolute;
        inset: 0;
        opacity: 0;
        margin: 0;
      }
      .switch-track {
        width: 100%;
        height: 100%;
        border-radius: 999px;
        background: rgba(82,84,88,0.18);
        border: 1px solid rgba(82,84,88,0.08);
      }
      .switch-track::after {
        content: "";
        position: absolute;
        top: 3px;
        left: 3px;
        width: 16px;
        height: 16px;
        border-radius: 50%;
        background: #fff;
        box-shadow: 0 1px 4px rgba(0,0,0,0.14);
        transition: transform 120ms ease;
      }
      .switch input:checked + .switch-track {
        background: rgba(126,128,132,0.66);
      }
      .switch input:checked + .switch-track::after {
        transform: translateX(18px);
      }
      input[type="range"] {
        -webkit-appearance: none;
        appearance: none;
        width: 100%;
        margin: 0;
        height: 20px;
        background: transparent;
      }
      input[type="range"]::-webkit-slider-runnable-track {
        height: 2px;
        border-radius: 999px;
        background: linear-gradient(90deg, var(--metal-c), var(--metal-b) 52%, var(--metal-a));
      }
      input[type="range"]::-webkit-slider-thumb {
        -webkit-appearance: none;
        appearance: none;
        width: 18px;
        height: 18px;
        margin-top: -8px;
        border: none;
        border-radius: 50%;
        background: #8a8d91;
      }
      input[type="range"]::-moz-range-track {
        height: 2px;
        border: none;
        border-radius: 999px;
        background: linear-gradient(90deg, var(--metal-c), var(--metal-b) 52%, var(--metal-a));
      }
      input[type="range"]::-moz-range-thumb {
        width: 18px;
        height: 18px;
        border: none;
        border-radius: 50%;
        background: #8a8d91;
      }
      textarea {
        width: 100%;
        min-height: 180px;
        resize: vertical;
        border: 1px solid rgba(82,84,88,0.1);
        border-radius: 12px;
        padding: 10px 12px;
        background: rgba(255,253,250,0.92);
        color: var(--ink);
        font: 0.78rem/1.45 "IBM Plex Mono", "Courier New", monospace;
      }
      .hint-list, .source-note {
        margin-top: 10px;
        color: var(--muted);
        font-size: 0.72rem;
      }
      .hint-list p, .source-note { margin: 0; }
      .terminal-log {
        min-height: 180px;
        max-height: 260px;
        overflow: auto;
        border: 1px solid rgba(82,84,88,0.1);
        border-radius: 12px;
        background: rgba(255,253,248,0.72);
        padding: 10px 12px;
        font: 0.74rem/1.45 "IBM Plex Mono", "Courier New", monospace;
        white-space: pre-wrap;
      }
      code {
        background: rgba(226,220,206,0.58);
        border-radius: 6px;
        padding: 1px 5px;
      }
      .radial-card {
        padding-bottom: 10px;
        background: radial-gradient(circle at center, rgba(255,255,255,0.45), transparent 54%), var(--surface);
      }
      .leg-ring-wrap {
        position: relative;
        display: grid;
        place-items: center;
        width: 100%;
        overflow: hidden;
        padding: 6px 0;
      }
      .leg-ring {
        position: relative;
        width: min(100%, 26rem);
        aspect-ratio: 1;
        margin: 0 auto;
        min-width: 0;
        --lamp-level: 0;
      }
      .home-button {
        position: absolute;
        left: 50%;
        top: 50%;
        width: 5.2rem;
        height: 5.2rem;
        transform: translate(-50%, -50%);
        border-radius: 50%;
        border: 1px solid rgba(82,84,88,0.14);
        background: linear-gradient(145deg, rgba(203,205,207,0.95), rgba(115,118,122,0.9));
        color: #fffdf9;
        box-shadow: inset 0 1px 1px rgba(255,255,255,0.42), 0 8px 16px rgba(74,69,60,0.12);
        z-index: 3;
        font-size: 0.88rem;
      }
      .leg-control {
        position: absolute;
        inset: 0;
      }
      .leg-axis {
        position: absolute;
        left: 0;
        top: 0;
        width: 0;
        height: 20px;
        display: flex;
        align-items: center;
        pointer-events: none;
        z-index: 2;
        transform-origin: 0 50%;
      }
      .leg-slider {
        position: relative;
        width: 100%;
        pointer-events: auto;
      }
      .leg-badge-anchor {
        position: absolute;
        left: 0;
        top: 0;
        width: 0;
        height: 0;
        z-index: 1;
        pointer-events: none;
      }
      .leg-badge {
        position: absolute;
        left: 0;
        top: 0;
        width: 3.6rem;
        height: 3.6rem;
        transform: translate(-50%, -50%);
        border: 1px solid rgba(255,244,223,0.8);
        border-radius: 50%;
        background:
          radial-gradient(circle at 35% 30%,
            rgba(255,255,255,calc(0.35 + var(--lamp-level) * 0.65)) 0%,
            rgba(255,252,243,calc(0.22 + var(--lamp-level) * 0.74)) 38%,
            rgba(255,247,226,calc(0.18 + var(--lamp-level) * 0.7)) 74%,
            rgba(241,231,205,calc(0.16 + var(--lamp-level) * 0.8)) 100%);
        box-shadow:
          0 0 0 4px rgba(255,255,255,calc(0.04 + var(--lamp-level) * 0.2)),
          0 0 calc(4px + var(--lamp-level) * 18px) rgba(255,244,215,calc(var(--lamp-level) * 0.6)),
          0 8px 18px rgba(87,80,68,0.12);
        display: flex;
        align-items: center;
        justify-content: center;
        filter: saturate(calc(0.55 + var(--lamp-level) * 0.7)) brightness(calc(0.72 + var(--lamp-level) * 0.5));
      }
      .leg-badge-content {
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 2px;
        text-align: center;
      }
      .leg-label {
        color: rgba(70,66,58,0.76);
        font-size: clamp(0.48rem, 1.1vw, 0.58rem);
        line-height: 1;
        text-transform: uppercase;
        letter-spacing: 0.06em;
      }
      .angle-readout {
        color: #4f4f4c;
        font-size: clamp(0.64rem, 1.5vw, 0.82rem);
        line-height: 1;
      }
      @media (min-width: 760px) {
        .app-shell { padding: 16px; }
      }
    </style>
  </head>
  <body>
    <div class="app-shell">
      <section class="panel">
        <div class="card">
          <div class="status-strip" aria-label="System status">
            <div id="wifiPill" class="status-pill">
              <span class="status-dot"></span><span class="status-pill-label">WiFi</span><span id="wifiStatus">On</span>
            </div>
            <div id="octopusPill" class="status-pill off">
              <span class="status-dot"></span><span class="status-pill-label">Octopus</span><span id="octopusStatus">Off</span>
            </div>
            <div id="remotePill" class="status-pill off">
              <span class="status-dot"></span><span class="status-pill-label">Remote</span><span id="remoteStatus">Idle</span>
            </div>
          </div>
          <div class="command-grid" style="margin-top:10px;">
            <div class="command-block">
              <div class="button-row">
                <button id="pos1Button" class="ghost-button" type="button">POS 1</button>
                <button id="pos2Button" class="ghost-button" type="button">POS 2</button>
                <button id="pos3Button" class="ghost-button" type="button">POS 3</button>
                <button id="randomButton" type="button">Random</button>
                <button id="stopButton" class="danger-button" type="button">Stop</button>
                <button id="resetOctopusButton" class="danger-button" type="button">Reset</button>
              </div>
            </div>
            <div class="command-block">
              <div class="card-subhead"><h3>Remote Keys</h3><span>Live</span></div>
              <div class="remote-key-row">
                <div id="remoteKeyLightOn" class="remote-key"><span class="remote-key-dot"></span><span class="remote-key-label">Light+</span></div>
                <div id="remoteKeyLightOff" class="remote-key"><span class="remote-key-dot"></span><span class="remote-key-label">Light-</span></div>
                <div id="remoteKeyPos1" class="remote-key"><span class="remote-key-dot"></span><span class="remote-key-label">Pos1</span></div>
                <div id="remoteKeyPos2" class="remote-key"><span class="remote-key-dot"></span><span class="remote-key-label">Pos2</span></div>
                <div id="remoteKeyRandom" class="remote-key"><span class="remote-key-dot"></span><span class="remote-key-label">Random</span></div>
                <div id="remoteKeyDimmer" class="remote-key"><span class="remote-key-dot"></span><span class="remote-key-label">Dim</span></div>
                <div id="remoteKeyPos3" class="remote-key"><span class="remote-key-dot"></span><span class="remote-key-label">Pos3</span></div>
              </div>
            </div>
            <div class="command-block">
              <div class="card-subhead"><h3>Speed</h3><span id="speedReadout">0</span></div>
              <input id="speedSlider" type="range" min="0" max="100" step="1" value="0" />
            </div>
            <div class="command-block">
              <div class="card-subhead"><h3>Lamp</h3><span id="lampReadout">OFF 0</span></div>
              <div class="lamp-row">
                <label class="switch"><input id="lampSwitch" type="checkbox" /><span class="switch-track"></span></label>
                <input id="lampSlider" type="range" min="0" max="100" step="1" value="0" />
              </div>
            </div>
          </div>
        </div>

        <div class="card radial-card">
          <div class="leg-ring-wrap">
            <div id="legControls" class="leg-ring">
              <button id="homeButton" class="home-button" type="button">Home</button>
            </div>
          </div>
        </div>

        <div class="card">
          <div class="terminal-grid">
            <section class="terminal-block">
              <div class="card-head"><h2>Log</h2><button id="clearLogButton" class="ghost-button" type="button">Clear Log</button></div>
              <div id="terminalLog" class="terminal-log" aria-live="polite"></div>
            </section>
            <section class="terminal-block">
              <div class="card-head"><h2>Terminal</h2><button id="sendButton" type="button">Send</button></div>
              <textarea id="gcodeInput" spellcheck="false" placeholder="Type G-code, one command per line."></textarea>
              <div class="hint-list"><p><code>G1 X20</code> <code>M355 P180 S1</code> <code>M215 P3</code> <code>M215 S3</code></p></div>
            </section>
          </div>
        </div>
      </section>
    </div>

    <script>
      const FEED_MIN = 10;
      const FEED_MAX = 400;
      const BRIGHTNESS_MAX = 255;
      const UI_MAX = 100;
      const AXIS_MAX = 119;
      const AXES = ["X", "Y", "Z", "A", "B", "C"];
      const state = {
        socket: null,
        reconnectTimer: 0,
        randomCodes: [],
        positions: { X: 0, Y: 0, Z: 0, A: 0, B: 0, C: 0 },
        feed: 200,
        lightOn: false,
        brightness: 0,
        remoteButtons: {
          lightOn: false,
          lightOff: false,
          pos1: false,
          pos2: false,
          random: false,
          dimmer: false,
          pos3: false
        }
      };
      const legDefinitions = AXES.map((axis, index) => ({ axis, angle: -90 + index * 60, label: `L${index + 1} ${axis}` }));
      const els = {
        wifiPill: document.getElementById("wifiPill"),
        octopusPill: document.getElementById("octopusPill"),
        remotePill: document.getElementById("remotePill"),
        wifiStatus: document.getElementById("wifiStatus"),
        octopusStatus: document.getElementById("octopusStatus"),
        remoteStatus: document.getElementById("remoteStatus"),
        pos1Button: document.getElementById("pos1Button"),
        pos2Button: document.getElementById("pos2Button"),
        pos3Button: document.getElementById("pos3Button"),
        randomButton: document.getElementById("randomButton"),
        stopButton: document.getElementById("stopButton"),
        resetOctopusButton: document.getElementById("resetOctopusButton"),
        remoteKeyLightOn: document.getElementById("remoteKeyLightOn"),
        remoteKeyLightOff: document.getElementById("remoteKeyLightOff"),
        remoteKeyPos1: document.getElementById("remoteKeyPos1"),
        remoteKeyPos2: document.getElementById("remoteKeyPos2"),
        remoteKeyRandom: document.getElementById("remoteKeyRandom"),
        remoteKeyDimmer: document.getElementById("remoteKeyDimmer"),
        remoteKeyPos3: document.getElementById("remoteKeyPos3"),
        speedSlider: document.getElementById("speedSlider"),
        speedReadout: document.getElementById("speedReadout"),
        lampSwitch: document.getElementById("lampSwitch"),
        lampSlider: document.getElementById("lampSlider"),
        lampReadout: document.getElementById("lampReadout"),
        legControls: document.getElementById("legControls"),
        homeButton: document.getElementById("homeButton"),
        gcodeInput: document.getElementById("gcodeInput"),
        sendButton: document.getElementById("sendButton"),
        clearLogButton: document.getElementById("clearLogButton"),
        terminalLog: document.getElementById("terminalLog")
      };

      function clamp(value, min, max) { return Math.max(min, Math.min(max, value)); }
      function feedToUi(feed) { return Math.round(((clamp(feed, FEED_MIN, FEED_MAX) - FEED_MIN) / (FEED_MAX - FEED_MIN)) * UI_MAX); }
      function feedFromUi(value) { return Math.round(FEED_MIN + (clamp(value, 0, UI_MAX) / UI_MAX) * (FEED_MAX - FEED_MIN)); }
      function brightnessToUi(value) { return Math.round((clamp(value, 0, BRIGHTNESS_MAX) / BRIGHTNESS_MAX) * UI_MAX); }
      function brightnessFromUi(value) { return Math.round((clamp(value, 0, UI_MAX) / UI_MAX) * BRIGHTNESS_MAX); }
      function log(message) {
        const stamp = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false });
        const line = `[${stamp}] ${message}`;
        els.terminalLog.textContent = (els.terminalLog.textContent ? els.terminalLog.textContent + "\n" : "") + line;
        const lines = els.terminalLog.textContent.split("\n");
        if (lines.length > 300) els.terminalLog.textContent = lines.slice(-300).join("\n");
        els.terminalLog.scrollTop = els.terminalLog.scrollHeight;
      }
      function setPill(pill, statusEl, active, text) {
        pill.classList.toggle("off", !active);
        statusEl.textContent = text;
      }
      function syncRemoteButtons(buttons) {
        state.remoteButtons = Object.assign({}, state.remoteButtons, buttons || {});
        els.remoteKeyLightOn.classList.toggle("active", !!state.remoteButtons.lightOn);
        els.remoteKeyLightOff.classList.toggle("active", !!state.remoteButtons.lightOff);
        els.remoteKeyPos1.classList.toggle("active", !!state.remoteButtons.pos1);
        els.remoteKeyPos2.classList.toggle("active", !!state.remoteButtons.pos2);
        els.remoteKeyRandom.classList.toggle("active", !!state.remoteButtons.random);
        els.remoteKeyDimmer.classList.toggle("active", !!state.remoteButtons.dimmer);
        els.remoteKeyPos3.classList.toggle("active", !!state.remoteButtons.pos3);
      }
      function send(obj) {
        if (state.socket && state.socket.readyState === WebSocket.OPEN) {
          state.socket.send(JSON.stringify(obj));
          return Promise.resolve(true);
        }

        return fetch("/api/command", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          cache: "no-store",
          body: JSON.stringify(Object.assign({ source: "Web UI" }, obj))
        }).then((response) => {
          if (!response.ok) throw new Error("HTTP " + response.status);
          return response.json();
        }).then((data) => {
          if (data && data.type === "state") applyState(data);
          return true;
        }).catch((error) => {
          log("Command send failed: " + error.message);
          return false;
        });
      }
      function sendAction(action, extra) { send(Object.assign({ type: "action", action: action }, extra || {})); }
      function syncSpeed(feed) {
        state.feed = clamp(Number(feed) || FEED_MIN, FEED_MIN, FEED_MAX);
        const ui = feedToUi(state.feed);
        els.speedSlider.value = String(ui);
        els.speedReadout.textContent = String(ui);
      }
      function syncLamp(on, brightness) {
        state.lightOn = !!on && brightness > 0;
        state.brightness = clamp(Number(brightness) || 0, 0, BRIGHTNESS_MAX);
        const ui = state.lightOn ? brightnessToUi(state.brightness) : 0;
        els.lampSwitch.checked = state.lightOn;
        els.lampSlider.value = String(ui);
        els.lampReadout.textContent = (state.lightOn ? "ON " : "OFF ") + ui;
        els.legControls.style.setProperty("--lamp-level", state.lightOn ? String(state.brightness / BRIGHTNESS_MAX) : "0");
      }
      function syncPositions() {
        legDefinitions.forEach((leg, index) => {
          const value = Math.round(clamp(state.positions[leg.axis] || 0, 0, AXIS_MAX));
          const slider = document.getElementById("motor-slider-" + index);
          const readout = document.getElementById("motor-readout-" + index);
          if (slider) slider.value = String(value);
          if (readout) readout.textContent = value + "°";
        });
      }
      function applyState(data) {
        setPill(els.wifiPill, els.wifiStatus, true, "On");
        setPill(els.octopusPill, els.octopusStatus, !!data.octopusOnline, data.octopusOnline ? "On" : "Off");
        setPill(els.remotePill, els.remoteStatus, !!data.remoteOnline, data.remoteOnline ? "On" : "Idle");
        if (data.feed !== undefined) syncSpeed(data.feed);
        if (data.lights) syncLamp(data.lights.on, data.lights.brightness);
        if (data.remoteButtons) syncRemoteButtons(data.remoteButtons);
        if (Array.isArray(data.randomCodes)) state.randomCodes = data.randomCodes.slice();
        if (data.positions) {
          AXES.forEach((axis) => {
            if (data.positions[axis] !== undefined) state.positions[axis] = Number(data.positions[axis]);
          });
          syncPositions();
        }
      }
      function createLegControls() {
        const fragment = document.createDocumentFragment();
        legDefinitions.forEach((leg, index) => {
          const control = document.createElement("div");
          control.className = "leg-control";
          control.dataset.angle = String(leg.angle);

          const axis = document.createElement("div");
          axis.className = "leg-axis";

          const slider = document.createElement("input");
          slider.type = "range";
          slider.min = "0";
          slider.max = String(AXIS_MAX);
          slider.step = "1";
          slider.id = "motor-slider-" + index;
          slider.className = "leg-slider";
          slider.addEventListener("input", () => {
            const value = clamp(Number(slider.value), 0, AXIS_MAX);
            state.positions[leg.axis] = value;
            syncPositions();
            send({ type: "move", axes: { [leg.axis]: value }, feed: state.feed });
          });
          axis.appendChild(slider);

          const anchor = document.createElement("div");
          anchor.className = "leg-badge-anchor";
          const badge = document.createElement("div");
          badge.className = "leg-badge";
          const content = document.createElement("div");
          content.className = "leg-badge-content";
          const label = document.createElement("span");
          label.className = "leg-label";
          label.textContent = leg.label;
          const readout = document.createElement("span");
          readout.className = "angle-readout";
          readout.id = "motor-readout-" + index;
          content.appendChild(label);
          content.appendChild(readout);
          badge.appendChild(content);
          anchor.appendChild(badge);

          control.appendChild(axis);
          control.appendChild(anchor);
          fragment.appendChild(control);
        });
        els.legControls.appendChild(fragment);
        syncPositions();
      }
      function layoutLegControls() {
        const ringSize = els.legControls.clientWidth;
        if (!ringSize) return;
        const center = ringSize / 2;
        const homeSize = clamp(ringSize * 0.24, 68, 104);
        const badgeSize = clamp(ringSize * 0.15, 48, 64);
        const edgeGap = ringSize * 0.04;
        const centerGap = ringSize * 0.02;
        const badgeRadius = center - badgeSize / 2 - edgeGap;
        const armStart = homeSize / 2 + centerGap;
        const armEndGap = badgeSize * 0.72;
        const armLength = Math.max(24, badgeRadius - armStart - armEndGap);
        els.homeButton.style.width = homeSize + "px";
        els.homeButton.style.height = homeSize + "px";
        Array.from(els.legControls.querySelectorAll(".leg-control")).forEach((control) => {
          const angle = Number(control.dataset.angle);
          const radians = angle * Math.PI / 180;
          const ux = Math.cos(radians);
          const uy = Math.sin(radians);
          const axis = control.querySelector(".leg-axis");
          const anchor = control.querySelector(".leg-badge-anchor");
          const badge = control.querySelector(".leg-badge");
          const content = control.querySelector(".leg-badge-content");
          axis.style.left = (center + ux * armStart) + "px";
          axis.style.top = (center + uy * armStart) + "px";
          axis.style.width = armLength + "px";
          axis.style.transform = "translateY(-50%) rotate(" + angle + "deg)";
          anchor.style.left = (center + ux * badgeRadius) + "px";
          anchor.style.top = (center + uy * badgeRadius) + "px";
          badge.style.width = badgeSize + "px";
          badge.style.height = badgeSize + "px";
          content.style.transform = "rotate(" + (-angle) + "deg)";
        });
      }
      function connectSocket() {
        state.socket = new WebSocket("ws://" + location.hostname + ":81/");
        state.socket.addEventListener("open", () => log("Web UI connected."));
        state.socket.addEventListener("message", (event) => {
          let data = null;
          try { data = JSON.parse(event.data); } catch (error) { return; }
          if (data.type === "state") applyState(data);
          else if (data.type === "log" && data.line) log(data.line);
          else if (data.type === "status" && data.message) log(data.message);
        });
        state.socket.addEventListener("close", () => {
          log("WebSocket disconnected. Reconnecting...");
          clearTimeout(state.reconnectTimer);
          state.reconnectTimer = setTimeout(connectSocket, 1000);
        });
      }
      els.pos1Button.addEventListener("click", () => sendAction("pos1"));
      els.pos2Button.addEventListener("click", () => sendAction("pos2"));
      els.pos3Button.addEventListener("click", () => sendAction("pos3"));
      els.randomButton.addEventListener("click", () => {
        if (!state.randomCodes.length) {
          sendAction("refresh_random_codes");
          log("Random code list requested.");
          return;
        }
        sendAction("random_position");
      });
      els.stopButton.addEventListener("click", () => sendAction("stop_motion"));
      els.resetOctopusButton.addEventListener("click", () => {
        if (!window.confirm("Restart the Octopus board now? Current motion will stop.")) return;
        sendAction("reset_octopus");
      });
      els.homeButton.addEventListener("click", () => sendAction("home"));
      els.speedSlider.addEventListener("input", () => {
        const feed = feedFromUi(Number(els.speedSlider.value));
        syncSpeed(feed);
        sendAction("set_feed", { feed: feed });
      });
      els.lampSwitch.addEventListener("change", () => sendAction(els.lampSwitch.checked ? "light_on" : "light_off"));
      els.lampSlider.addEventListener("input", () => {
        const brightness = brightnessFromUi(Number(els.lampSlider.value));
        syncLamp(brightness > 0, brightness);
        sendAction("set_brightness", { brightness: brightness });
      });
      els.sendButton.addEventListener("click", () => {
        const lines = els.gcodeInput.value.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
        if (!lines.length) {
          log("Terminal is empty.");
          return;
        }
        lines.forEach((line) => send({ type: "cmd", gcode: line }));
      });
      els.clearLogButton.addEventListener("click", () => { els.terminalLog.textContent = ""; });
      createLegControls();
      layoutLegControls();
      window.addEventListener("resize", layoutLegControls);
      if ("ResizeObserver" in window) new ResizeObserver(() => layoutLegControls()).observe(els.legControls);
      fetch("/api/state", { cache: "no-store" }).then((response) => response.json()).then((data) => applyState(data)).catch(() => log("Initial state request failed."));
      connectSocket();
    </script>
  </body>
</html>
)HTML";
