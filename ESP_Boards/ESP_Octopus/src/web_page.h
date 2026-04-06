#pragma once

static const char OCTOPUS_WEB_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>ESP Octopus Console</title>
    <style>
      :root {
        --bg: #e8efe7;
        --bg-accent: #d4dfd1;
        --panel: rgba(255, 252, 245, 0.9);
        --panel-strong: #fffaf0;
        --ink: #17221a;
        --muted: #5f6b61;
        --line: rgba(23, 34, 26, 0.12);
        --accent: #b14f1b;
        --body: #35483d;
      }

      * { box-sizing: border-box; }
      html, body {
        margin: 0;
        min-height: 100%;
        background:
          radial-gradient(circle at top, rgba(255, 255, 255, 0.65), transparent 40%),
          linear-gradient(180deg, var(--bg) 0%, var(--bg-accent) 100%);
        color: var(--ink);
        font-family: Georgia, "Times New Roman", serif;
        font-size: 16px;
      }

      .app-shell {
        display: grid;
        grid-template-columns: 1fr;
        gap: 14px;
        min-height: 100vh;
        padding: max(12px, env(safe-area-inset-top)) 12px max(12px, env(safe-area-inset-bottom)) 12px;
      }

      .panel {
        display: flex;
        flex-direction: column;
        gap: 12px;
      }

      .panel-left { max-height: none; }
      .panel-right { min-width: 0; }
      .panel-right { order: -1; }

      .card, .canvas-frame {
        background: var(--panel);
        border: 1px solid var(--line);
        border-radius: 22px;
        box-shadow: 0 18px 40px rgba(43, 54, 46, 0.08);
        backdrop-filter: blur(12px);
      }

      .card { padding: 16px; }
      .canvas-frame { padding: 10px; min-height: 42vh; }

      h1, h2, p { margin: 0; }
      h1 { font-size: 1.45rem; line-height: 1.1; }
      h2 { font-size: 1.05rem; }

      .panel-head, .card-head {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 10px;
      }

      .panel-head {
        align-items: flex-start;
        flex-direction: column;
        padding: 4px 2px;
      }

      .muted { color: var(--muted); }

      .button-row, .chip-row {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 8px;
        width: 100%;
      }

      button {
        border: none;
        border-radius: 999px;
        padding: 13px 16px;
        background: var(--accent);
        color: #fffaf5;
        cursor: pointer;
        font: inherit;
        min-height: 48px;
        font-size: 1rem;
        touch-action: manipulation;
      }

      button.ghost {
        background: rgba(177, 79, 27, 0.12);
        color: var(--accent);
      }

      .chip {
        border-radius: 999px;
        padding: 8px 12px;
        background: rgba(47, 65, 54, 0.08);
        color: var(--muted);
        font-size: 0.93rem;
        text-align: center;
      }

      .chip.online {
        background: rgba(33, 115, 57, 0.12);
        color: #215b34;
      }

      .motor-grid {
        display: grid;
        gap: 10px;
      }

      .motor-control {
        background: rgba(255, 255, 255, 0.55);
        border: 1px solid var(--line);
        border-radius: 16px;
        padding: 12px;
      }

      .motor-row {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 10px;
        margin-bottom: 8px;
      }

      .motor-row label {
        font-size: 0.98rem;
        font-weight: 600;
      }

      .motor-inputs {
        display: grid;
        grid-template-columns: 1fr 92px;
        gap: 10px;
        align-items: center;
      }

      .feed-card {
        display: grid;
        gap: 10px;
      }

      .feed-row {
        display: grid;
        grid-template-columns: 1fr 92px;
        gap: 10px;
        align-items: center;
      }

      input[type="range"] {
        width: 100%;
        accent-color: var(--accent);
      }

      input[type="number"],
      input[type="text"],
      textarea {
        width: 100%;
        border: 1px solid var(--line);
        border-radius: 14px;
        padding: 12px 12px;
        background: var(--panel-strong);
        color: var(--ink);
        font: inherit;
        min-height: 48px;
      }

      textarea {
        min-height: 180px;
        resize: vertical;
        font-family: "Courier New", monospace;
        line-height: 1.4;
      }

      .terminal-log {
        min-height: 150px;
        max-height: 22vh;
        overflow: auto;
        border: 1px solid var(--line);
        border-radius: 16px;
        padding: 12px;
        background: rgba(255, 250, 240, 0.78);
        font-family: "Courier New", monospace;
        font-size: 0.92rem;
        line-height: 1.4;
        white-space: pre-wrap;
      }

      .status-card { margin-top: 0; }

      canvas {
        width: 100%;
        height: 100%;
        display: block;
        border-radius: 16px;
        background:
          radial-gradient(circle at 50% 30%, rgba(255, 255, 255, 0.38), transparent 34%),
          linear-gradient(180deg, rgba(255, 252, 245, 0.92), rgba(232, 239, 231, 0.96));
      }

      @media (min-width: 720px) {
        .button-row, .chip-row {
          display: flex;
          flex-wrap: wrap;
        }
      }

      @media (min-width: 1100px) {
        .app-shell {
          grid-template-columns: minmax(340px, 430px) 1fr;
          gap: 18px;
          padding: 18px;
        }

        .panel-left { max-height: calc(100vh - 36px); }
        .panel-right { order: 0; }
        .canvas-frame { min-height: calc(100vh - 36px); }
        .terminal-log { max-height: 28vh; }
      }
    </style>
  </head>
  <body>
    <div class="app-shell">
      <section class="panel panel-left">
        <div class="panel-head">
          <h1>ESP Octopus Control</h1>
          <p class="muted">Access-point control page for Octopus serial G-code, shared light control, remote logs, and live robot state.</p>
          <div class="chip-row">
            <span id="wsStatus" class="chip">Connecting...</span>
            <span id="octopusStatus" class="chip">Octopus: unknown</span>
            <span id="remoteStatus" class="chip">Remote: waiting</span>
          </div>
        </div>

        <div class="card feed-card">
          <div class="card-head">
            <h2>Feed Rate</h2>
            <span id="feedReadout">F200</span>
          </div>
          <div class="feed-row">
            <input id="feedSlider" type="range" min="10" max="2000" step="10" value="200" />
            <input id="feedNumber" type="number" min="10" max="2000" step="10" value="200" />
          </div>
          <p class="muted">Used for direct pose commands sent from this page.</p>
        </div>

        <div class="card feed-card">
          <div class="card-head">
            <h2>Lights</h2>
            <span id="lightsReadout">OFF · 160/255</span>
          </div>
          <div class="button-row">
            <button id="lightOnButton" type="button">Lights On</button>
            <button id="lightOffButton" class="ghost" type="button">Lights Off</button>
          </div>
          <div class="feed-row">
            <input id="brightnessSlider" type="range" min="0" max="255" step="1" value="160" />
            <input id="brightnessNumber" type="number" min="0" max="255" step="1" value="160" />
          </div>
          <p class="muted">Lamp output is generated by Octopus through Marlin M355 on the configured bed/heater output.</p>
        </div>

        <div class="card">
          <div class="card-head">
            <h2>Direct Pose</h2>
            <div class="button-row">
              <button id="sendPoseButton" type="button">Send Pose</button>
              <button id="refreshButton" class="ghost" type="button">Refresh M114</button>
            </div>
          </div>
          <div id="motorControls" class="motor-grid"></div>
        </div>

        <div class="card">
          <div class="card-head">
            <h2>Quick Commands</h2>
            <div class="button-row">
              <button id="homeButton" type="button">M215 H</button>
              <button id="pos1Button" class="ghost" type="button">M215 P1</button>
              <button id="pos2Button" class="ghost" type="button">M215 P2</button>
              <button id="randomButton" type="button">Random Pos</button>
              <button id="stopButton" class="ghost" type="button">M112</button>
            </div>
          </div>
          <p id="randomCodesLine" class="muted">Random codes: waiting for M215 list...</p>
        </div>

        <div class="card status-card">
          <h2>Status</h2>
          <p id="statusLine" class="muted">Waiting for Octopus...</p>
          <p id="networkLine" class="muted" style="margin-top: 8px;">AP: 192.168.4.1 · Remote: 192.168.4.2</p>
        </div>
      </section>

      <section class="panel panel-right">
        <div class="canvas-frame">
          <canvas id="simCanvas" width="1200" height="800"></canvas>
        </div>
        <div class="card">
          <div class="card-head">
            <h2>Terminal</h2>
            <div class="button-row">
              <button id="sendCommandButton" type="button">Send</button>
              <button id="clearLogButton" class="ghost" type="button">Clear Log</button>
            </div>
          </div>
          <div id="terminalLog" class="terminal-log"></div>
          <div class="feed-row" style="margin-top: 10px;">
            <input id="commandInput" type="text" placeholder="Type a G-code command, e.g. M114 or G1 X50 F200" />
            <button id="sendSingleButton" type="button">Run</button>
          </div>
        </div>
      </section>
    </div>

    <script>
      const MOTOR_COUNT = 6;
      const MIN_POS = 0;
      const MAX_POS = 115;
      const LEG_AXES = ["X", "Y", "Z", "A", "B", "C"];
      const DEFAULT_POSE = [0, 0, 0, 0, 0, 0];
      const BODY_SCALE = 0.7;
      const LEG_SCALE = 1.4 * 1.3;
      const LEG_LENGTH_1 = 92 * LEG_SCALE;
      const LEG_LENGTH_2 = 80 * LEG_SCALE;

      const legDefinitions = [
        { name: "Leg 1", anchor: [-75, -62], direction: [-0.96, -0.34], color: "#31513f" },
        { name: "Leg 2", anchor: [-92, 0], direction: [-1, 0], color: "#365541" },
        { name: "Leg 3", anchor: [-75, 62], direction: [-0.96, 0.34], color: "#406248" },
        { name: "Leg 4", anchor: [75, -62], direction: [0.96, -0.34], color: "#31513f" },
        { name: "Leg 5", anchor: [92, 0], direction: [1, 0], color: "#365541" },
        { name: "Leg 6", anchor: [75, 62], direction: [0.96, 0.34], color: "#406248" },
      ];

      const state = {
        connected: false,
        octopusOnline: false,
        remoteOnline: false,
        lightsOn: false,
        brightness: 160,
        randomCodes: [],
        motors: [...DEFAULT_POSE],
        displayMotors: [...DEFAULT_POSE],
        feedRate: 200,
        draggingIndex: null,
      };

      const elements = {
        wsStatus: document.getElementById("wsStatus"),
        octopusStatus: document.getElementById("octopusStatus"),
        remoteStatus: document.getElementById("remoteStatus"),
        motorControls: document.getElementById("motorControls"),
        feedSlider: document.getElementById("feedSlider"),
        feedNumber: document.getElementById("feedNumber"),
        feedReadout: document.getElementById("feedReadout"),
        lightsReadout: document.getElementById("lightsReadout"),
        brightnessSlider: document.getElementById("brightnessSlider"),
        brightnessNumber: document.getElementById("brightnessNumber"),
        lightOnButton: document.getElementById("lightOnButton"),
        lightOffButton: document.getElementById("lightOffButton"),
        sendPoseButton: document.getElementById("sendPoseButton"),
        refreshButton: document.getElementById("refreshButton"),
        homeButton: document.getElementById("homeButton"),
        pos1Button: document.getElementById("pos1Button"),
        pos2Button: document.getElementById("pos2Button"),
        randomButton: document.getElementById("randomButton"),
        stopButton: document.getElementById("stopButton"),
        terminalLog: document.getElementById("terminalLog"),
        commandInput: document.getElementById("commandInput"),
        sendCommandButton: document.getElementById("sendCommandButton"),
        sendSingleButton: document.getElementById("sendSingleButton"),
        clearLogButton: document.getElementById("clearLogButton"),
        statusLine: document.getElementById("statusLine"),
        networkLine: document.getElementById("networkLine"),
        randomCodesLine: document.getElementById("randomCodesLine"),
        canvas: document.getElementById("simCanvas"),
      };

      const ctx = elements.canvas.getContext("2d");
      let ws = null;

      function clamp(value, min, max) {
        return Math.max(min, Math.min(max, value));
      }

      function lerp(a, b, t) {
        return a + (b - a) * t;
      }

      function setStatus(message) {
        elements.statusLine.textContent = message;
      }

      function appendLog(line) {
        const stamp = new Date().toLocaleTimeString();
        const next = `${elements.terminalLog.textContent}[${stamp}] ${line}\n`;
        const rows = next.split("\n");
        elements.terminalLog.textContent = rows.length > 300 ? rows.slice(rows.length - 300).join("\n") : next;
        elements.terminalLog.scrollTop = elements.terminalLog.scrollHeight;
      }

      function updateFeed(value) {
        const next = clamp(Number.isFinite(value) ? value : 200, 10, 2000);
        state.feedRate = next;
        elements.feedSlider.value = String(next);
        elements.feedNumber.value = String(next);
        elements.feedReadout.textContent = `F${next}`;
      }

      function updateLightControls(brightness, isOn) {
        const nextBrightness = clamp(Number.isFinite(brightness) ? brightness : state.brightness, 0, 255);
        state.brightness = nextBrightness;
        state.lightsOn = Boolean(isOn) && nextBrightness > 0;
        elements.brightnessSlider.value = String(nextBrightness);
        elements.brightnessNumber.value = String(nextBrightness);
        elements.lightsReadout.textContent = `${state.lightsOn ? "ON" : "OFF"} · ${nextBrightness}/255`;
      }

      function createMotorControls() {
        const fragment = document.createDocumentFragment();

        legDefinitions.forEach((leg, index) => {
          const wrapper = document.createElement("div");
          wrapper.className = "motor-control";

          const row = document.createElement("div");
          row.className = "motor-row";

          const label = document.createElement("label");
          label.htmlFor = `motor-slider-${index}`;
          label.textContent = `${leg.name} (${LEG_AXES[index]})`;

          const readout = document.createElement("span");
          readout.className = "angle-readout";
          readout.id = `motor-readout-${index}`;

          row.append(label, readout);

          const inputs = document.createElement("div");
          inputs.className = "motor-inputs";

          const slider = document.createElement("input");
          slider.type = "range";
          slider.min = String(MIN_POS);
          slider.max = String(MAX_POS);
          slider.step = "1";
          slider.id = `motor-slider-${index}`;

          const number = document.createElement("input");
          number.type = "number";
          number.min = String(MIN_POS);
          number.max = String(MAX_POS);
          number.step = "1";
          number.id = `motor-number-${index}`;

          slider.addEventListener("pointerdown", () => { state.draggingIndex = index; });
          slider.addEventListener("pointerup", () => { state.draggingIndex = null; sendPose(); });
          slider.addEventListener("input", () => setMotor(index, Number(slider.value), true));

          number.addEventListener("focus", () => { state.draggingIndex = index; });
          number.addEventListener("change", () => {
            setMotor(index, Number(number.value), true);
            state.draggingIndex = null;
            sendPose();
          });

          inputs.append(slider, number);
          wrapper.append(row, inputs);
          fragment.append(wrapper);
        });

        elements.motorControls.append(fragment);
        syncControls();
      }

      function syncControls() {
        state.displayMotors.forEach((value, index) => {
          const rounded = Math.round(value);
          const slider = document.getElementById(`motor-slider-${index}`);
          const number = document.getElementById(`motor-number-${index}`);
          const readout = document.getElementById(`motor-readout-${index}`);
          if (state.draggingIndex !== index) {
            slider.value = String(rounded);
            number.value = String(rounded);
          }
          readout.textContent = `${rounded} mm`;
        });
      }

      function setMotor(index, value, updateDisplay) {
        const next = clamp(Number.isFinite(value) ? value : 0, MIN_POS, MAX_POS);
        state.motors[index] = next;
        if (updateDisplay) state.displayMotors[index] = next;
        syncControls();
        drawScene();
      }

      function applyPose(pose) {
        state.motors = [...pose];
        state.displayMotors = [...pose];
        syncControls();
        drawScene();
      }

      function sendJson(payload) {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
          appendLog("> websocket not connected");
          return;
        }
        ws.send(JSON.stringify(payload));
      }

      function sendCommand(command) {
        const trimmed = command.trim();
        if (!trimmed) return;
        appendLog(`> ${trimmed}`);
        sendJson({ type: "cmd", gcode: trimmed });
      }

      function sendAction(action, extra = {}) {
        appendLog(`> action ${action}`);
        sendJson({ type: "action", action, ...extra });
      }

      function sendPose() {
        const pose = {};
        LEG_AXES.forEach((axis, index) => {
          pose[axis] = Math.round(state.motors[index]);
        });
        setStatus(`Sending pose at F${state.feedRate}`);
        sendJson({ type: "move", axes: pose, feed: state.feedRate });
      }

      function connectWebSocket() {
        const protocol = location.protocol === "https:" ? "wss" : "ws";
        ws = new WebSocket(`${protocol}://${location.hostname}:81/`);

        ws.addEventListener("open", () => {
          state.connected = true;
          elements.wsStatus.textContent = "Web UI online";
          elements.wsStatus.classList.add("online");
          setStatus("Connected to ESP32.");
        });

        ws.addEventListener("close", () => {
          state.connected = false;
          state.octopusOnline = false;
          state.remoteOnline = false;
          elements.wsStatus.textContent = "Reconnecting...";
          elements.wsStatus.classList.remove("online");
          elements.octopusStatus.textContent = "Octopus: offline";
          elements.octopusStatus.classList.remove("online");
          elements.remoteStatus.textContent = "Remote: offline";
          elements.remoteStatus.classList.remove("online");
          setStatus("WebSocket disconnected. Retrying...");
          setTimeout(connectWebSocket, 1000);
        });

        ws.addEventListener("message", (event) => {
          let payload = null;
          try {
            payload = JSON.parse(event.data);
          } catch (error) {
            appendLog(`< invalid JSON: ${event.data}`);
            return;
          }

          if (payload.type === "state") {
            const nextPose = LEG_AXES.map(axis => clamp(Number(payload.positions?.[axis] ?? 0), MIN_POS, MAX_POS));
            applyPose(nextPose);
            state.octopusOnline = Boolean(payload.octopusOnline);
            state.remoteOnline = Boolean(payload.remoteOnline);
            elements.octopusStatus.textContent = state.octopusOnline ? "Octopus: online" : "Octopus: waiting";
            elements.octopusStatus.classList.toggle("online", state.octopusOnline);
            elements.remoteStatus.textContent = state.remoteOnline ? "Remote: online" : "Remote: waiting";
            elements.remoteStatus.classList.toggle("online", state.remoteOnline);
            if (typeof payload.feed === "number") updateFeed(payload.feed);
            updateLightControls(Number(payload.lights?.brightness ?? state.brightness), Boolean(payload.lights?.on));
            state.randomCodes = Array.isArray(payload.randomCodes) ? payload.randomCodes : [];
            elements.randomCodesLine.textContent = state.randomCodes.length
              ? `Random codes: ${state.randomCodes.join(", ")}`
              : "Random codes: waiting for M215 list...";
            const apIp = payload.accessPoint?.ip ?? "192.168.4.1";
            const remoteIp = payload.remoteIp ?? "192.168.4.2";
            elements.networkLine.textContent = `AP: ${apIp} · Remote: ${remoteIp}`;
            return;
          }

          if (payload.type === "log") {
            appendLog(`< ${payload.line}`);
            return;
          }

          if (payload.type === "status") {
            setStatus(payload.message || "Ready");
          }
        });
      }

      function poseToTarget(position) {
        const t = clamp(position, MIN_POS, MAX_POS) / MAX_POS;
        return {
          x: lerp(148 * LEG_SCALE, 44 * LEG_SCALE, t),
          y: lerp(-84 * LEG_SCALE, 110 * LEG_SCALE, t),
        };
      }

      function solveLeg(target) {
        const distance = clamp(Math.hypot(target.x, target.y), 8, LEG_LENGTH_1 + LEG_LENGTH_2 - 0.0001);
        const baseAngle = Math.atan2(target.y, target.x);
        const shoulderOffset = Math.acos(
          clamp((LEG_LENGTH_1 ** 2 + distance ** 2 - LEG_LENGTH_2 ** 2) / (2 * LEG_LENGTH_1 * distance), -1, 1)
        );
        const elbowInner = Math.acos(
          clamp((LEG_LENGTH_1 ** 2 + LEG_LENGTH_2 ** 2 - distance ** 2) / (2 * LEG_LENGTH_1 * LEG_LENGTH_2), -1, 1)
        );

        const shoulderAngle = baseAngle + shoulderOffset;
        const elbowAngle = shoulderAngle - (Math.PI - elbowInner);

        return {
          joint: { x: Math.cos(shoulderAngle) * LEG_LENGTH_1, y: Math.sin(shoulderAngle) * LEG_LENGTH_1 },
          foot: { x: target.x, y: target.y },
          shadow: lerp(1, 0.56, clamp(target.y / 110, 0, 1)),
        };
      }

      function projectPoint(anchor, direction, point) {
        return {
          x: anchor[0] + direction[0] * point.x,
          y: anchor[1] + direction[1] * point.x - point.y,
        };
      }

      function resizeCanvas() {
        const ratio = window.devicePixelRatio || 1;
        const bounds = elements.canvas.getBoundingClientRect();
        elements.canvas.width = Math.max(480, Math.floor(bounds.width * ratio));
        elements.canvas.height = Math.max(360, Math.floor(bounds.height * ratio));
        drawScene();
      }

      function drawGround(centerX, centerY, scale) {
        ctx.save();
        ctx.translate(centerX, centerY);
        ctx.scale(scale, scale);
        ctx.strokeStyle = "rgba(47, 65, 54, 0.08)";
        ctx.lineWidth = 1;
        for (let y = -150; y <= 210; y += 40) {
          ctx.beginPath();
          ctx.moveTo(-380, y);
          ctx.lineTo(380, y);
          ctx.stroke();
        }
        ctx.restore();
      }

      function drawBody(centerX, centerY, scale) {
        ctx.save();
        ctx.translate(centerX, centerY);
        ctx.scale(scale, scale);
        ctx.scale(BODY_SCALE, BODY_SCALE);

        ctx.fillStyle = "rgba(27, 31, 28, 0.12)";
        ctx.beginPath();
        ctx.ellipse(0, 62, 126, 32, 0, 0, Math.PI * 2);
        ctx.fill();

        ctx.fillStyle = "#35483d";
        ctx.beginPath();
        ctx.ellipse(0, 0, 112, 72, 0, 0, Math.PI * 2);
        ctx.fill();

        ctx.fillStyle = "#41594a";
        ctx.beginPath();
        ctx.ellipse(0, -6, 58, 42, 0, 0, Math.PI * 2);
        ctx.fill();
        ctx.restore();
      }

      function drawLeg(definition, position, centerX, centerY, scale) {
        const solved = solveLeg(poseToTarget(position));
        const anchor = [definition.anchor[0] * BODY_SCALE, definition.anchor[1] * BODY_SCALE];
        const joint = projectPoint(anchor, definition.direction, solved.joint);
        const foot = projectPoint(anchor, definition.direction, solved.foot);

        ctx.save();
        ctx.translate(centerX, centerY);
        ctx.scale(scale, scale);

        ctx.strokeStyle = "rgba(22, 27, 23, 0.15)";
        ctx.lineWidth = 7;
        ctx.lineCap = "round";
        ctx.beginPath();
        ctx.moveTo(anchor[0], anchor[1] + 62);
        ctx.lineTo(foot.x, foot.y + 62 * solved.shadow);
        ctx.stroke();

        ctx.strokeStyle = definition.color;
        ctx.lineWidth = 11;
        ctx.beginPath();
        ctx.moveTo(anchor[0], anchor[1]);
        ctx.lineTo(joint.x, joint.y);
        ctx.stroke();

        ctx.lineWidth = 9;
        ctx.beginPath();
        ctx.moveTo(joint.x, joint.y);
        ctx.lineTo(foot.x, foot.y);
        ctx.stroke();

        ctx.fillStyle = "#d86d2d";
        ctx.beginPath();
        ctx.arc(anchor[0], anchor[1], 6.5, 0, Math.PI * 2);
        ctx.fill();
        ctx.beginPath();
        ctx.arc(joint.x, joint.y, 6, 0, Math.PI * 2);
        ctx.fill();

        ctx.fillStyle = "#223228";
        ctx.beginPath();
        ctx.arc(foot.x, foot.y, 4.8, 0, Math.PI * 2);
        ctx.fill();
        ctx.restore();
      }

      function drawLabels(centerX, centerY, scale) {
        ctx.save();
        ctx.translate(centerX, centerY);
        ctx.scale(scale, scale);
        ctx.fillStyle = "rgba(23, 34, 26, 0.72)";
        ctx.font = "12px Georgia";
        ctx.textAlign = "center";
        legDefinitions.forEach((leg, index) => {
          ctx.fillText(`${LEG_AXES[index]} ${Math.round(state.displayMotors[index])}mm`, leg.anchor[0], leg.anchor[1] - 18);
        });
        ctx.restore();
      }

      function drawScene() {
        const width = elements.canvas.width;
        const height = elements.canvas.height;
        const centerX = width / 2;
        const centerY = height / 2 + 20;
        const scale = Math.min(width / 1320, height / 980);
        ctx.clearRect(0, 0, width, height);
        drawGround(centerX, centerY, scale);
        legDefinitions.forEach((leg, index) => drawLeg(leg, state.displayMotors[index], centerX, centerY, scale));
        drawBody(centerX, centerY, scale);
        drawLabels(centerX, centerY, scale);
      }

      elements.feedSlider.addEventListener("input", () => updateFeed(Number(elements.feedSlider.value)));
      elements.feedSlider.addEventListener("change", () => sendAction("set_feed", { feed: Number(elements.feedSlider.value) }));
      elements.feedNumber.addEventListener("change", () => {
        updateFeed(Number(elements.feedNumber.value));
        sendAction("set_feed", { feed: Number(elements.feedNumber.value) });
      });
      elements.brightnessSlider.addEventListener("input", () => updateLightControls(Number(elements.brightnessSlider.value), state.lightsOn || Number(elements.brightnessSlider.value) > 0));
      elements.brightnessSlider.addEventListener("change", () => sendAction("set_brightness", { brightness: Number(elements.brightnessSlider.value) }));
      elements.brightnessNumber.addEventListener("change", () => {
        updateLightControls(Number(elements.brightnessNumber.value), state.lightsOn || Number(elements.brightnessNumber.value) > 0);
        sendAction("set_brightness", { brightness: Number(elements.brightnessNumber.value) });
      });
      elements.lightOnButton.addEventListener("click", () => sendAction("light_on"));
      elements.lightOffButton.addEventListener("click", () => sendAction("light_off"));
      elements.sendPoseButton.addEventListener("click", sendPose);
      elements.refreshButton.addEventListener("click", () => sendAction("refresh_positions"));
      elements.homeButton.addEventListener("click", () => sendAction("home"));
      elements.pos1Button.addEventListener("click", () => sendAction("pos1"));
      elements.pos2Button.addEventListener("click", () => sendAction("pos2"));
      elements.randomButton.addEventListener("click", () => sendAction("random_position"));
      elements.stopButton.addEventListener("click", () => sendCommand("M112"));
      elements.sendCommandButton.addEventListener("click", () => sendCommand(elements.commandInput.value));
      elements.sendSingleButton.addEventListener("click", () => sendCommand(elements.commandInput.value));
      elements.commandInput.addEventListener("keydown", (event) => {
        if (event.key === "Enter") {
          event.preventDefault();
          sendCommand(elements.commandInput.value);
        }
      });
      elements.clearLogButton.addEventListener("click", () => { elements.terminalLog.textContent = ""; });
      window.addEventListener("resize", resizeCanvas);

      createMotorControls();
      updateFeed(200);
      updateLightControls(160, false);
      resizeCanvas();
      drawScene();
      connectWebSocket();
    </script>
  </body>
</html>
)HTML";
