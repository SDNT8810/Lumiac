const MOTOR_COUNT = 6;
const MIN_ANGLE = 0;
const MAX_ANGLE = 120;
const LEG_AXES = ["X", "Y", "Z", "A", "B", "C"];
const DEFAULT_POSE = [20, 20, 20, 20, 20, 20];
const SIM_FEED_REFERENCE = 200;
const FEED_MIN = 10;
const FEED_MAX = 300;
const BRIGHTNESS_MAX = 255;
const UI_SCALE_MAX = 100;
const MAX_LOG_LINES = 300;
const SPIDER_SD_CODE_PROGRAMS = {
  1: `; Smooth struggling motion for the 6-leg spider
G1 X18 Y26 Z22 A24 B20 C28 F200
G1 X42 Y18 Z48 A20 B40 C24 F200
G1 X76 Y28 Z62 A12 B68 C30 F200
G1 X58 Y46 Z32 A26 B74 C54 F200
G1 X24 Y72 Z18 A44 B38 C70 F200
G1 X12 Y84 Z36 A62 B20 C82 F200
G1 X34 Y58 Z64 A74 B14 C60 F200
G1 X68 Y30 Z80 A54 B34 C28 F200
G1 X88 Y18 Z58 A30 B64 C16 F200
G1 X64 Y42 Z22 A16 B86 C34 F200
G1 X30 Y78 Z14 A26 B66 C68 F200
G1 X18 Y92 Z26 A48 B32 C88 F200
G1 X44 Y62 Z58 A78 B20 C56 F200
G1 X82 Y36 Z72 A60 B28 C24 F200
G1 X70 Y18 Z38 A28 B72 C12 F200
G1 X36 Y54 Z12 A18 B90 C30 F200
G1 X20 Y74 Z34 A40 B58 C74 F200
G1 X26 Y48 Z70 A72 B24 C52 F200
G1 X54 Y28 Z86 A80 B16 C26 F200
G1 X72 Y40 Z60 A52 B34 C18 F200
G1 X50 Y66 Z24 A22 B80 C44 F200
G1 X28 Y84 Z16 A34 B64 C82 F200
G1 X46 Y60 Z44 A62 B30 C58 F200
G1 X58 Y52 Z36 A54 B42 C50 F200
G1 X38 Y44 Z40 A44 B40 C42 F200`,
  2: `; Phased sinusoidal tapping cycle
G1 X46 Y70 Z70 A46 B22 C22 F200
G1 X60 Y74 Z60 A32 B18 C32 F200
G1 X70 Y70 Z46 A22 B22 C46 F200
G1 X74 Y60 Z32 A18 B32 C60 F200
G1 X70 Y46 Z22 A22 B46 C70 F200
G1 X60 Y32 Z18 A32 B60 C74 F200
G1 X46 Y22 Z22 A46 B70 C70 F200
G1 X32 Y18 Z32 A60 B74 C60 F200
G1 X22 Y22 Z46 A70 B70 C46 F200
G1 X18 Y32 Z60 A74 B60 C32 F200
G1 X22 Y46 Z70 A70 B46 C22 F200
G1 X32 Y60 Z74 A60 B32 C18 F200
@LOOP`,
  3: `; Tripod wave gait
G1 X28 Y68 Z28 A68 B28 C68 F200
G1 X36 Y78 Z36 A78 B36 C78 F200
G1 X48 Y72 Z48 A72 B48 C72 F200
G1 X62 Y58 Z62 A58 B62 C58 F200
G1 X76 Y40 Z76 A40 B76 C40 F200
G1 X62 Y24 Z62 A24 B62 C24 F200
G1 X48 Y18 Z48 A18 B48 C18 F200
G1 X36 Y26 Z36 A26 B36 C26 F200
G1 X28 Y42 Z28 A42 B28 C42 F200
G1 X36 Y58 Z36 A58 B36 C58 F200
G1 X48 Y72 Z48 A72 B48 C72 F200
G1 X62 Y78 Z62 A78 B62 C78 F200
G1 X76 Y68 Z76 A68 B76 C68 F200
G1 X62 Y52 Z62 A52 B62 C52 F200
G1 X48 Y34 Z48 A34 B48 C34 F200
G1 X36 Y22 Z36 A22 B36 C22 F200
@LOOP`,
  4: `; Ripple walk
G1 X26 Y38 Z56 A76 B64 C44 F200
G1 X34 Y30 Z46 A68 B72 C56 F200
G1 X48 Y26 Z34 A56 B76 C68 F200
G1 X64 Y30 Z26 A42 B72 C76 F200
G1 X76 Y42 Z24 A30 B62 C72 F200
G1 X80 Y58 Z28 A24 B48 C62 F200
G1 X72 Y72 Z38 A26 B34 C48 F200
G1 X58 Y80 Z54 A34 B26 C34 F200
G1 X42 Y76 Z70 A46 B24 C26 F200
G1 X30 Y64 Z78 A62 B28 C24 F200
G1 X24 Y48 Z80 A74 B38 C28 F200
G1 X26 Y34 Z72 A80 B54 C36 F200
G1 X34 Y26 Z58 A76 B70 C48 F200
G1 X46 Y24 Z42 A66 B78 C62 F200
G1 X60 Y28 Z30 A52 B76 C74 F200
G1 X72 Y38 Z24 A38 B68 C78 F200
@LOOP`,
  5: `; Alert pulse
G1 X24 Y26 Z28 A24 B26 C28 F200
G1 X40 Y42 Z44 A40 B42 C44 F200
G1 X62 Y64 Z66 A62 B64 C66 F200
G1 X84 Y86 Z88 A84 B86 C88 F200
G1 X72 Y78 Z68 A58 B48 C42 F200
G1 X54 Y70 Z76 A72 B64 C46 F200
G1 X38 Y58 Z72 A84 B80 C58 F200
G1 X26 Y40 Z58 A76 B88 C72 F200
G1 X22 Y28 Z40 A60 B78 C84 F200
G1 X30 Y24 Z28 A42 B60 C76 F200
G1 X44 Y30 Z24 A30 B42 C58 F200
G1 X62 Y42 Z30 A24 B28 C40 F200
G1 X78 Y58 Z42 A28 B24 C28 F200
G1 X88 Y74 Z58 A40 B30 C24 F200
G1 X74 Y86 Z74 A58 B42 C30 F200
G1 X52 Y76 Z86 A74 B58 C42 F200
G1 X34 Y58 Z78 A86 B74 C58 F200
G1 X24 Y38 Z56 A78 B86 C74 F200
G1 X22 Y26 Z36 A60 B74 C86 F200
G1 X24 Y24 Z28 A42 B52 C72 F200
@LOOP`,
  6: `; Clock sweep
G1 X82 Y66 Z50 A34 B18 C34 F200
G1 X88 Y74 Z58 A38 B16 C26 F200
G1 X82 Y82 Z68 A46 B18 C20 F200
G1 X72 Y88 Z78 A58 B24 C18 F200
G1 X58 Y82 Z86 A72 B34 C20 F200
G1 X42 Y72 Z88 A82 B48 C26 F200
G1 X28 Y58 Z82 A88 B64 C36 F200
G1 X18 Y42 Z72 A82 B78 C48 F200
G1 X16 Y28 Z58 A72 B88 C62 F200
G1 X20 Y18 Z42 A58 B82 C76 F200
G1 X28 Y16 Z28 A42 B72 C86 F200
G1 X38 Y20 Z18 A28 B58 C88 F200
G1 X50 Y28 Z16 A18 B42 C82 F200
G1 X66 Y38 Z20 A16 B28 C72 F200
G1 X78 Y50 Z28 A20 B18 C58 F200
G1 X86 Y64 Z40 A28 B16 C42 F200
@LOOP`,
  7: `; Chaotic recovery
G1 X22 Y54 Z34 A72 B40 C62 F200
G1 X34 Y68 Z28 A80 B26 C54 F200
G1 X58 Y74 Z20 A66 B18 C42 F200
G1 X76 Y62 Z26 A44 B24 C28 F200
G1 X84 Y42 Z40 A26 B40 C18 F200
G1 X72 Y26 Z58 A18 B62 C22 F200
G1 X50 Y18 Z74 A24 B80 C36 F200
G1 X30 Y24 Z86 A40 B84 C56 F200
G1 X18 Y40 Z80 A62 B72 C74 F200
G1 X20 Y62 Z62 A80 B52 C86 F200
G1 X34 Y80 Z40 A86 B30 C76 F200
G1 X56 Y86 Z24 A74 B18 C54 F200
G1 X78 Y72 Z18 A54 B22 C34 F200
G1 X88 Y50 Z24 A34 B36 C22 F200
G1 X80 Y30 Z40 A22 B58 C20 F200
G1 X60 Y18 Z60 A24 B78 C30 F200
G1 X38 Y20 Z78 A38 B88 C48 F200
G1 X24 Y34 Z88 A58 B80 C68 F200
G1 X20 Y54 Z80 A76 B60 C82 F200
G1 X28 Y72 Z62 A86 B38 C72 F200
G1 X42 Y84 Z42 A78 B24 C52 F200
G1 X60 Y80 Z28 A62 B20 C36 F200
G1 X74 Y66 Z22 A46 B28 C26 F200
G1 X78 Y48 Z26 A34 B42 C22 F200
G1 X66 Y32 Z34 A28 B56 C28 F200
G1 X48 Y24 Z42 A30 B64 C40 F200
@LOOP`,
};

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
    return SPIDER_SD_CODE_PROGRAMS[code]
      ? { type: "spider-file", code }
      : null;
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

  const feedMatch = cleaned.match(/\bF(-?\d+(?:\.\d+)?)\b/);
  const feed = feedMatch ? Math.max(1, Number(feedMatch[1])) : SIM_FEED_REFERENCE;
  return createMoveCommand(pose, previousPose, feed);
}

function parseProgram(programText) {
  const lines = programText.split(/\r?\n/);
  const commands = [];
  let loopForever = false;
  let previousPose = [...state.motors];
  let homedAxes = [...state.homedAxes];

  lines.forEach((line) => {
    const trimmed = line.trim().toUpperCase();

    if (trimmed === "@LOOP" || trimmed === "LOOP") {
      loopForever = true;
      return;
    }

    if (/^M215\s+H\b/.test(trimmed)) {
      const preset = createPresetCommands("H", previousPose, homedAxes);
      commands.push(...preset.commands);
      previousPose = preset.pose;
      homedAxes = preset.homedAxes;
      return;
    }

    if (/^M215\s+P1\b/.test(trimmed)) {
      const preset = createPresetCommands("P1", previousPose, homedAxes);
      commands.push(...preset.commands);
      previousPose = preset.pose;
      homedAxes = preset.homedAxes;
      return;
    }

    if (/^M215\s+P2\b/.test(trimmed)) {
      const preset = createPresetCommands("P2", previousPose, homedAxes);
      commands.push(...preset.commands);
      previousPose = preset.pose;
      homedAxes = preset.homedAxes;
      return;
    }

    if (/^M215\s+P3\b/.test(trimmed)) {
      const preset = createPresetCommands("P3", previousPose, homedAxes);
      commands.push(...preset.commands);
      previousPose = preset.pose;
      homedAxes = preset.homedAxes;
      return;
    }

    const command = parseLine(line, previousPose);
    if (!command) {
      return;
    }

    if (command.type === "spider-file") {
      const embedded = parseProgram(SPIDER_SD_CODE_PROGRAMS[command.code]);
      commands.push(...embedded.commands);
      loopForever = loopForever || embedded.loopForever;
      if (embedded.commands.length) {
        const lastMove = [...embedded.commands].reverse().find((entry) => entry.type === "move");
        if (lastMove) {
          previousPose = [...lastMove.pose];
        }
      }
      return;
    }

    commands.push(command);

    if (command.type === "move") {
      previousPose = [...command.pose];
      if (command.homeAxes) {
        homedAxes = homedAxes.map((value, index) => value || command.homeAxes[index]);
      }
    }
  });

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
  const program = parseProgram(elements.gcodeInput.value);
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

function runRandomSpiderFile() {
  const codes = Object.keys(SPIDER_SD_CODE_PROGRAMS).map(Number);
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

if ("serviceWorker" in navigator) {
  window.addEventListener("load", () => {
    navigator.serviceWorker.register("./sw.js").catch(() => {});
  });
}
