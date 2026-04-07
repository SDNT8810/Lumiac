const MOTOR_COUNT = 6;
const MIN_ANGLE = 0;
const MAX_ANGLE = 119;
const LEG_AXES = ["X", "Y", "Z", "A", "B", "C"];
const DEFAULT_POSE = [20, 20, 20, 20, 20, 20];
const SIM_FEED_REFERENCE = 200;
const FEED_MIN = 10;
const FEED_MAX = 400;
const BRIGHTNESS_MAX = 255;
const UI_SCALE_MAX = 100;
const MAX_LOG_LINES = 300;

const legDefinitions = Array.from({ length: MOTOR_COUNT }, (_, index) => ({
  name: `Leg ${index + 1}`,
  axis: LEG_AXES[index],
  ringAngle: -90 + index * 60,
}));

const state = {
  motors: [...DEFAULT_POSE],
  displayMotors: [...DEFAULT_POSE],
  homedAxes: Array(MOTOR_COUNT).fill(false),
  animationFrame: 0,
  sequenceToken: 0,
  feedRate: 200,
  lightsOn: false,
  brightness: 160,
  lastNonZeroBrightness: 160,
  logLines: [],
  spiderPrograms: {},
};

const elements = {
  legControls: document.getElementById("legControls"),
  gcodeInput: document.getElementById("gcodeInput"),
  programFileInput: document.getElementById("programFileInput"),
  pos1Button: document.getElementById("pos1Button"),
  pos2Button: document.getElementById("pos2Button"),
  pos3Button: document.getElementById("pos3Button"),
  randomButton: document.getElementById("randomButton"),
  sendCommandButton: document.getElementById("sendCommandButton"),
  feedSlider: document.getElementById("feedSlider"),
  feedReadout: document.getElementById("feedReadout"),
  lampSwitch: document.getElementById("lampSwitch"),
  lampBrightnessSlider: document.getElementById("lampBrightnessSlider"),
  lampReadout: document.getElementById("lampReadout"),
  sourceNote: document.getElementById("sourceNote"),
  statusLine: document.getElementById("statusLine"),
  terminalLog: document.getElementById("terminalLog"),
  clearLogButton: document.getElementById("clearLogButton"),
  homeButton: document.getElementById("homeButton"),
};

let ringResizeObserver = null;

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function scaleToUi(value, max) {
  return Math.round((clamp(value, 0, max) / max) * UI_SCALE_MAX);
}

function scaleFromUi(value, max) {
  return Math.round((clamp(value, 0, UI_SCALE_MAX) / UI_SCALE_MAX) * max);
}

function feedToUi(feed) {
  return Math.round(((clamp(feed, FEED_MIN, FEED_MAX) - FEED_MIN) / (FEED_MAX - FEED_MIN)) * UI_SCALE_MAX);
}

function feedFromUi(value) {
  return Math.round(FEED_MIN + (clamp(value, 0, UI_SCALE_MAX) / UI_SCALE_MAX) * (FEED_MAX - FEED_MIN));
}

function formatTimestamp() {
  return new Date().toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  });
}

function appendLog(message) {
  const line = `[${formatTimestamp()}] ${message}`;
  state.logLines.push(line);

  if (state.logLines.length > MAX_LOG_LINES) {
    state.logLines = state.logLines.slice(-MAX_LOG_LINES);
  }

  elements.terminalLog.textContent = state.logLines.join("\n");
  elements.terminalLog.scrollTop = elements.terminalLog.scrollHeight;
}

function setStatus(message, options = {}) {
  const { log = true } = options;
  elements.statusLine.textContent = message;

  if (log) {
    appendLog(message);
  }
}

async function loadSpiderPrograms(options = {}) {
  const { force = false } = options;

  if (!force && Object.keys(state.spiderPrograms).length) {
    return state.spiderPrograms;
  }

  const primaryUrl = window.location.origin && window.location.origin !== "null"
    ? new URL("/api/spider-files", window.location.origin).toString()
    : "./api/spider-files";
  let response;

  try {
    response = await fetch(primaryUrl, { cache: "no-store" });
  } catch (primaryError) {
    response = await fetch("./api/spider-files", { cache: "no-store" });
  }

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }

  const payload = await response.json();
  const programs = {};

  for (const entry of payload.programs || []) {
    programs[entry.code] = entry;
  }

  state.spiderPrograms = programs;
  return programs;
}

function getSpiderProgramCodes() {
  return Object.keys(state.spiderPrograms)
    .map(Number)
    .sort((left, right) => left - right);
}

function createMoveCommand(pose, previousPose, feed = SIM_FEED_REFERENCE, extra = {}) {
  return {
    type: "move",
    pose,
    feed,
    ...extra,
  };
}

function getMoveDurationMs(fromPose, toPose, commandFeed = SIM_FEED_REFERENCE) {
  const maxDelta = Math.max(...toPose.map((value, index) => Math.abs(value - fromPose[index])));
  const effectiveFeed = Math.max(1, commandFeed * (state.feedRate / SIM_FEED_REFERENCE));

  return maxDelta <= 0
    ? 0
    : Math.max(40, Math.min(5000, (maxDelta * 2400) / effectiveFeed));
}

function updateFeedRate(nextValue) {
  const uiValue = clamp(Number.isFinite(nextValue) ? nextValue : feedToUi(state.feedRate), 0, UI_SCALE_MAX);
  const feed = feedFromUi(uiValue);
  state.feedRate = feed;
  elements.feedSlider.value = String(uiValue);
  elements.feedReadout.textContent = String(uiValue);
}

function applyFeedRate(nextValue, source = "Feed") {
  updateFeedRate(nextValue);
  setStatus(`${source}: ${feedToUi(state.feedRate)}`);
}

function updateLampControls(brightness, isOn) {
  const nextBrightness = clamp(Number.isFinite(brightness) ? brightness : state.brightness, 0, BRIGHTNESS_MAX);
  state.brightness = nextBrightness;
  state.lightsOn = Boolean(isOn) && nextBrightness > 0;

  if (nextBrightness > 0) {
    state.lastNonZeroBrightness = nextBrightness;
  }

  elements.lampSwitch.checked = state.lightsOn;
  const displayBrightness = state.lightsOn ? scaleToUi(nextBrightness, BRIGHTNESS_MAX) : 0;
  elements.lampBrightnessSlider.value = String(displayBrightness);
  elements.lampReadout.textContent = `${state.lightsOn ? "ON" : "OFF"} ${displayBrightness}`;
  elements.legControls.style.setProperty("--lamp-level", String(state.lightsOn ? nextBrightness / BRIGHTNESS_MAX : 0));
}

function applyLampCommand({ on, brightness }, source = "Lamp") {
  const nextBrightness = clamp(Number.isFinite(brightness) ? brightness : state.brightness, 0, 255);
  updateLampControls(nextBrightness, on);

  const displayBrightness = state.lightsOn ? scaleToUi(state.brightness, BRIGHTNESS_MAX) : 0;
  setStatus(`${source}: ${state.lightsOn ? "ON" : "OFF"} ${displayBrightness}`);
}

function setLampOn(isOn, source = "Lamp") {
  if (isOn) {
    const brightness = state.brightness > 0 ? state.brightness : Math.max(1, state.lastNonZeroBrightness);
    applyLampCommand({ on: true, brightness }, source);
    return;
  }

  applyLampCommand({ on: false, brightness: state.brightness }, source);
}

function setLampBrightness(value, source = "Lamp", keepOff = false) {
  const brightness = clamp(Number.isFinite(value) ? value : state.brightness, 0, 255);
  const shouldBeOn = keepOff ? state.lightsOn && brightness > 0 : brightness > 0 || state.lightsOn;
  applyLampCommand({ on: shouldBeOn, brightness }, source);
}

function createPresetCommands(code, previousPose, homedAxes) {
  const commands = [];
  let pose = [...previousPose];
  let nextHomedAxes = [...homedAxes];
  const allAxes = Array(MOTOR_COUNT).fill(true);
  const homePose = Array(MOTOR_COUNT).fill(0);

  if (code === "H") {
    if (!nextHomedAxes.every(Boolean) || pose.some((value) => value !== 0)) {
      commands.push(createMoveCommand(homePose, pose, 100, { homeAxes: allAxes }));
      pose = homePose;
      nextHomedAxes = allAxes;
    }

    return { commands, pose, homedAxes: nextHomedAxes };
  }

  if (!nextHomedAxes.every(Boolean)) {
    commands.push(createMoveCommand(homePose, pose, 100, { homeAxes: allAxes }));
    pose = homePose;
    nextHomedAxes = allAxes;
  }

  const targetPose = code === "P2"
    ? Array(MOTOR_COUNT).fill(115)
    : code === "P3"
      ? [30, 80, 30, 80, 30, 80]
      : Array(MOTOR_COUNT).fill(50);
  const overshoot = code === "P2" ? 20 : code === "P3" ? 10 : 5;
  const reversePose = pose.map((value) => clamp(value - overshoot, 0, MAX_ANGLE));

  if (reversePose.some((value, index) => value !== pose[index])) {
    commands.push(createMoveCommand(reversePose, pose, 100));
    pose = reversePose;
  }

  commands.push(createMoveCommand(targetPose, pose, 100));

  return { commands, pose: targetPose, homedAxes: nextHomedAxes };
}

function createLegControls() {
  const fragment = document.createDocumentFragment();

  legDefinitions.forEach((leg, index) => {
    const wrapper = document.createElement("div");
    wrapper.className = "leg-control";
    wrapper.dataset.angle = String(leg.ringAngle);

    const axis = document.createElement("div");
    axis.className = "leg-axis";

    const slider = document.createElement("input");
    slider.type = "range";
    slider.min = String(MIN_ANGLE);
    slider.max = String(MAX_ANGLE);
    slider.step = "1";
    slider.id = `motor-slider-${index}`;
    slider.className = "leg-slider";
    slider.dataset.index = String(index);
    slider.setAttribute("aria-label", `${leg.name} ${leg.axis}`);

    slider.addEventListener("input", () => updateMotor(index, Number(slider.value), true));

    axis.append(slider);

    const badgeAnchor = document.createElement("div");
    badgeAnchor.className = "leg-badge-anchor";

    const badge = document.createElement("div");
    badge.className = "leg-badge";

    const badgeContent = document.createElement("div");
    badgeContent.className = "leg-badge-content";

    const label = document.createElement("span");
    label.className = "leg-label";
    label.textContent = `L${index + 1} ${leg.axis}`;

    const readout = document.createElement("span");
    readout.className = "angle-readout";
    readout.id = `motor-readout-${index}`;

    badgeContent.append(label, readout);
    badge.append(badgeContent);
    badgeAnchor.append(badge);
    wrapper.append(axis, badgeAnchor);
    fragment.append(wrapper);
  });

  elements.legControls.append(fragment);
  syncControls();
}

function layoutLegControls() {
  const ringSize = elements.legControls.clientWidth;

  if (!ringSize) {
    return;
  }

  const center = ringSize / 2;
  const homeSize = clamp(ringSize * 0.24, 68, 104);
  const badgeSize = clamp(ringSize * 0.15, 48, 64);
  const edgeGap = ringSize * 0.04;
  const centerGap = ringSize * 0.02;
  const baseBadgeRadius = center - badgeSize / 2 - edgeGap;
  const badgeRadius = Math.min(center - badgeSize / 2 - 2, baseBadgeRadius * 1.3);
  const armStart = homeSize / 2 + centerGap;
  const armEndGap = badgeSize * 0.6;
  const armLength = Math.max(24, badgeRadius - armStart - armEndGap);

  elements.homeButton.style.width = `${homeSize}px`;
  elements.homeButton.style.height = `${homeSize}px`;

  Array.from(elements.legControls.querySelectorAll(".leg-control")).forEach((control, index) => {
    const angle = Number(control.dataset.angle);
    const radians = (angle * Math.PI) / 180;
    const ux = Math.cos(radians);
    const uy = Math.sin(radians);
    const axis = control.querySelector(".leg-axis");
    const badgeAnchor = control.querySelector(".leg-badge-anchor");
    const badge = control.querySelector(".leg-badge");
    const badgeContent = control.querySelector(".leg-badge-content");

    const axisX = center + ux * armStart;
    const axisY = center + uy * armStart;
    axis.style.left = `${axisX}px`;
    axis.style.top = `${axisY}px`;
    axis.style.width = `${armLength}px`;
    axis.style.transform = `translateY(-50%) rotate(${angle}deg)`;

    const badgeX = center + ux * badgeRadius;
    const badgeY = center + uy * badgeRadius;
    badgeAnchor.style.left = `${badgeX}px`;
    badgeAnchor.style.top = `${badgeY}px`;

    badge.style.width = `${badgeSize}px`;
    badge.style.height = `${badgeSize}px`;
    badgeContent.style.transform = `rotate(${-angle}deg)`;
  });
}

function syncControls() {
  state.displayMotors.forEach((angle, index) => {
    const rounded = Math.round(angle);
    const slider = document.getElementById(`motor-slider-${index}`);
    const readout = document.getElementById(`motor-readout-${index}`);

    if (!slider || !readout) {
      return;
    }

    slider.value = String(rounded);
    readout.textContent = `${rounded}\u00b0`;
  });
}

function updateMotor(index, nextValue, cancelProgram = false) {
  const angle = clamp(Number.isFinite(nextValue) ? nextValue : 0, MIN_ANGLE, MAX_ANGLE);

  if (cancelProgram) {
    stopSequence();
    const token = state.sequenceToken;
    const targetPose = [...state.displayMotors];
    targetPose[index] = angle;
    state.motors[index] = angle;
    setStatus(`Manual control: ${legDefinitions[index].name} ${Math.round(angle)} at speed ${feedToUi(state.feedRate)}`);
    animatePose(targetPose, SIM_FEED_REFERENCE, token);
    return;
  }

  state.motors[index] = angle;
  state.displayMotors[index] = angle;
  syncControls();
}

function animatePose(targetPose, commandFeed, token) {
  const startPose = [...state.displayMotors];
  const fullDurationMs = getMoveDurationMs(startPose, targetPose, commandFeed);
  let progress = 0;
  let lastTime = performance.now();

  cancelAnimationFrame(state.animationFrame);

  return new Promise((resolve) => {
    function tick(now) {
      if (token !== undefined && state.sequenceToken !== token) {
        resolve(false);
        return;
      }

      const deltaMs = now - lastTime;
      lastTime = now;

      if (fullDurationMs <= 0) {
        progress = 1;
      } else {
        const currentDurationMs = getMoveDurationMs(startPose, targetPose, commandFeed);
        progress = clamp(progress + deltaMs / Math.max(1, currentDurationMs), 0, 1);
      }

      state.displayMotors = startPose.map((value, index) => lerp(value, targetPose[index], progress));
      state.motors = [...targetPose];
      syncControls();

      if (progress >= 1) {
        resolve(true);
        return;
      }

      state.animationFrame = requestAnimationFrame(tick);
    }

    state.animationFrame = requestAnimationFrame(tick);
  });
}

function sleep(ms, token) {
  return new Promise((resolve) => {
    const timeoutId = window.setTimeout(resolve, ms);
    const startedToken = token;

    function watch() {
      if (state.sequenceToken !== startedToken) {
        clearTimeout(timeoutId);
        resolve();
        return;
      }

      requestAnimationFrame(watch);
    }

    requestAnimationFrame(watch);
  });
}

function parseLine(line, previousPose) {
  const cleaned = line.replace(/;.*$/, "").trim().toUpperCase();

  if (!cleaned) {
    return null;
  }

  if (/^G4\b/.test(cleaned)) {
    const dwellMatch = cleaned.match(/\bP(-?\d+(?:\.\d+)?)\b/);
    return {
      type: "dwell",
      ms: dwellMatch ? Math.max(0, Number(dwellMatch[1])) : 500,
    };
  }

  if (/^G28\b/.test(cleaned)) {
    const pose = [...previousPose];
    const homeAxes = Array(MOTOR_COUNT).fill(false);
    let hasAxis = false;

    LEG_AXES.forEach((axis, index) => {
      if (new RegExp(`\\b${axis}\\b`).test(cleaned)) {
        pose[index] = 0;
        homeAxes[index] = true;
        hasAxis = true;
      }
    });

    if (!hasAxis) {
      pose.fill(0);
      homeAxes.fill(true);
    }

    return createMoveCommand(pose, previousPose, 100, { homeAxes });
  }

  if (/^M355\b/.test(cleaned)) {
    const offMatch = cleaned.match(/\bS(-?\d+(?:\.\d+)?)\b/);
    const brightnessMatch = cleaned.match(/\bP(-?\d+(?:\.\d+)?)\b/);

    if (offMatch && Number(offMatch[1]) <= 0) {
      return {
        type: "lamp",
        on: false,
        brightness: state.brightness,
      };
    }

    if (brightnessMatch) {
      const brightness = clamp(Number(brightnessMatch[1]), 0, 255);
      return {
        type: "lamp",
        on: brightness > 0,
        brightness,
      };
    }

    return {
      type: "lamp",
      on: state.lightsOn,
      brightness: state.brightness,
    };
  }

  if (/^M215\s+S\d+\b/.test(cleaned)) {
    const codeMatch = cleaned.match(/\bS(\d+)\b/);
    const code = codeMatch ? Number(codeMatch[1]) : 0;
    return { type: "spider-file", code };
  }

  const isRapid = /^G0\b/.test(cleaned);
  const isLinear = /^G1\b/.test(cleaned) || (!/^G\d+\b/.test(cleaned) && /[XYZABC]/.test(cleaned));

  if (!isRapid && !isLinear) {
    return null;
  }

  const pose = [...previousPose];
  let hasAxis = false;

  LEG_AXES.forEach((axis, index) => {
    const match = cleaned.match(new RegExp(`\\b${axis}(-?\\d+(?:\\.\\d+)?)\\b`));
    if (match) {
      pose[index] = clamp(Number(match[1]), MIN_ANGLE, MAX_ANGLE);
      hasAxis = true;
    }
  });

  const motorMatch = [...cleaned.matchAll(/\bM([1-6])\s*=?\s*(-?\d+(?:\.\d+)?)\b/g)];
  motorMatch.forEach((match) => {
    const index = Number(match[1]) - 1;
    pose[index] = clamp(Number(match[2]), MIN_ANGLE, MAX_ANGLE);
    hasAxis = true;
  });

  if (!hasAxis) {
    return null;
  }

  return createMoveCommand(pose, previousPose, SIM_FEED_REFERENCE);
}

async function parseProgram(programText) {
  const lines = programText.split(/\r?\n/);
  const commands = [];
  let loopForever = false;
  let previousPose = [...state.motors];
  let homedAxes = [...state.homedAxes];

  for (const line of lines) {
    const trimmed = line.trim().toUpperCase();

    if (trimmed === "@LOOP" || trimmed === "LOOP") {
      loopForever = true;
      continue;
    }

    if (/^M215\s+H\b/.test(trimmed)) {
      const preset = createPresetCommands("H", previousPose, homedAxes);
      commands.push(...preset.commands);
      previousPose = preset.pose;
      homedAxes = preset.homedAxes;
      continue;
    }

    if (/^M215\s+P1\b/.test(trimmed)) {
      const preset = createPresetCommands("P1", previousPose, homedAxes);
      commands.push(...preset.commands);
      previousPose = preset.pose;
      homedAxes = preset.homedAxes;
      continue;
    }

    if (/^M215\s+P2\b/.test(trimmed)) {
      const preset = createPresetCommands("P2", previousPose, homedAxes);
      commands.push(...preset.commands);
      previousPose = preset.pose;
      homedAxes = preset.homedAxes;
      continue;
    }

    if (/^M215\s+P3\b/.test(trimmed)) {
      const preset = createPresetCommands("P3", previousPose, homedAxes);
      commands.push(...preset.commands);
      previousPose = preset.pose;
      homedAxes = preset.homedAxes;
      continue;
    }

    const command = parseLine(line, previousPose);
    if (!command) {
      continue;
    }

    if (command.type === "spider-file") {
      const spiderProgram = state.spiderPrograms[command.code];
      if (!spiderProgram?.exists || typeof spiderProgram.content !== "string") {
        throw new Error(`Spider file S${command.code} not found in gcode directory.`);
      }

      const embedded = await parseProgram(spiderProgram.content);
      commands.push(...embedded.commands);
      loopForever = loopForever || embedded.loopForever;
      if (embedded.commands.length) {
        const lastMove = [...embedded.commands].reverse().find((entry) => entry.type === "move");
        if (lastMove) {
          previousPose = [...lastMove.pose];
        }
      }
      continue;
    }

    commands.push(command);

    if (command.type === "move") {
      previousPose = [...command.pose];
      if (command.homeAxes) {
        homedAxes = homedAxes.map((value, index) => value || command.homeAxes[index]);
      }
    }
  }

  return {
    commands,
    loopForever,
    homedAxes,
  };
}

function openProgramPicker() {
  elements.programFileInput.click();
}

async function handleProgramSelection(event) {
  const [file] = event.target.files || [];

  if (!file) {
    return;
  }

  try {
    const programText = await file.text();
    elements.gcodeInput.value = programText.trimEnd();
    elements.sourceNote.innerHTML = `File: <code>${file.name}</code>`;
    setStatus(`Loaded ${file.name}.`);
  } catch (error) {
    setStatus(`Could not load ${file.name}: ${error.message}`);
  } finally {
    elements.programFileInput.value = "";
  }
}

function stopSequence() {
  state.sequenceToken += 1;
  cancelAnimationFrame(state.animationFrame);
}

async function runProgram() {
  stopSequence();
  const token = state.sequenceToken;
  await loadSpiderPrograms();
  const program = await parseProgram(elements.gcodeInput.value);
  const { commands, loopForever, homedAxes } = program;

  if (!commands.length) {
    setStatus("No valid G-code commands found.");
    return;
  }

  let cycle = 0;

  do {
    cycle += 1;
    setStatus(
      `Running ${commands.length} command${commands.length === 1 ? "" : "s"}${loopForever ? `, cycle ${cycle}` : ""}...`
    );

    for (let index = 0; index < commands.length; index += 1) {
      if (state.sequenceToken !== token) {
        return;
      }

      const command = commands[index];

      if (command.type === "dwell") {
        setStatus(`Dwelling for ${Math.round(command.ms)} ms${loopForever ? `, cycle ${cycle}` : ""}`);
        await sleep(command.ms, token);
        continue;
      }

      if (command.type === "lamp") {
        applyLampCommand(command, "Program");
        continue;
      }

      setStatus(
        `Move ${index + 1}/${commands.length}${loopForever ? `, cycle ${cycle}` : ""}: ${command.pose
          .map((value) => Math.round(value))
          .join(", ")}`
      );

      const completed = await animatePose(command.pose, command.feed, token);
      if (!completed) {
        return;
      }
    }
  } while (loopForever && state.sequenceToken === token);

  if (state.sequenceToken === token) {
    state.homedAxes = homedAxes;
    setStatus("Program completed.");
  }
}

async function sendTerminalCommand() {
  const text = elements.gcodeInput.value.trim();

  if (!text) {
    setStatus("Terminal is empty.");
    return;
  }

  await runProgram();
}

async function runPreset(code, label) {
  stopSequence();
  const token = state.sequenceToken;
  const preset = createPresetCommands(code, state.motors, state.homedAxes);

  if (!preset.commands.length) {
    state.homedAxes = preset.homedAxes;
    setStatus(`${label} ready.`);
    return;
  }

  setStatus(`${label}...`);

  for (let index = 0; index < preset.commands.length; index += 1) {
    if (state.sequenceToken !== token) {
      return;
    }

    const command = preset.commands[index];
    const completed = await animatePose(command.pose, command.feed, token);
    if (!completed) {
      return;
    }
  }

  if (state.sequenceToken === token) {
    state.homedAxes = preset.homedAxes;
    setStatus(`${label} complete.`);
  }
}

function resetPose() {
  stopSequence();
  state.motors = [...DEFAULT_POSE];
  state.displayMotors = [...DEFAULT_POSE];
  state.homedAxes = Array(MOTOR_COUNT).fill(false);
  syncControls();
  setStatus("Pose reset.");
}

function runPresetCommand(command) {
  elements.gcodeInput.value = command;
  runProgram();
}

async function runRandomSpiderFile() {
  await loadSpiderPrograms();
  const codes = getSpiderProgramCodes();
  if (!codes.length) {
    setStatus("No spider files found in gcode directory.");
    return;
  }

  const code = codes[Math.floor(Math.random() * codes.length)];
  elements.gcodeInput.value = `M215 S${code}`;
  setStatus(`Random: M215 S${code}`);
  runProgram();
}

elements.pos1Button.addEventListener("click", () => runPresetCommand("M215 P1"));
elements.pos2Button.addEventListener("click", () => runPresetCommand("M215 P2"));
elements.pos3Button.addEventListener("click", () => runPresetCommand("M215 P3"));
elements.randomButton.addEventListener("click", runRandomSpiderFile);
elements.sendCommandButton.addEventListener("click", sendTerminalCommand);
elements.programFileInput.addEventListener("change", handleProgramSelection);
elements.feedSlider.addEventListener("input", () => applyFeedRate(Number(elements.feedSlider.value), "Speed"));
elements.lampSwitch.addEventListener("change", () => {
  setLampOn(elements.lampSwitch.checked, "Lamp");
});
elements.lampBrightnessSlider.addEventListener("input", () => {
  const value = scaleFromUi(Number(elements.lampBrightnessSlider.value), BRIGHTNESS_MAX);
  setLampBrightness(value, "Lamp");
});
elements.clearLogButton.addEventListener("click", () => {
  state.logLines = [];
  elements.terminalLog.textContent = "";
  setStatus("Log cleared.");
});
elements.homeButton.addEventListener("click", () => {
  runPreset("H", "M215 H");
});

createLegControls();
updateFeedRate(state.feedRate);
updateLampControls(state.brightness, state.lightsOn);
syncControls();
layoutLegControls();

window.addEventListener("resize", layoutLegControls);

if ("ResizeObserver" in window) {
  ringResizeObserver = new ResizeObserver(() => layoutLegControls());
  ringResizeObserver.observe(elements.legControls);
}

setStatus("Idle", { log: false });
appendLog("WebApp ready.");
loadSpiderPrograms()
  .then(() => {
    appendLog(`Loaded ${getSpiderProgramCodes().length} spider files from gcode directory.`);
  })
  .catch((error) => {
    appendLog(`Could not load spider files: ${error.message}`);
  });
