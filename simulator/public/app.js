const MOTOR_COUNT = 6;
const MIN_ANGLE = 0;
const MAX_ANGLE = 120;
const BODY_SCALE = 0.7;
const LEG_SCALE = 1.4 * 1.3;
const LEG_LENGTH_1 = 92 * LEG_SCALE;
const LEG_LENGTH_2 = 80 * LEG_SCALE;
const LEG_AXES = ["X", "Y", "Z", "A", "B", "C"];
const DEFAULT_POSE = [20, 20, 20, 20, 20, 20];
const SIM_FEED_REFERENCE = 200;

const legDefinitions = [
  { name: "Leg 1", anchor: [-75, -62], direction: [-0.96, -0.34], color: "#31513f" },
  { name: "Leg 2", anchor: [-92, 0], direction: [-1, 0], color: "#365541" },
  { name: "Leg 3", anchor: [-75, 62], direction: [-0.96, 0.34], color: "#406248" },
  { name: "Leg 4", anchor: [75, -62], direction: [0.96, -0.34], color: "#31513f" },
  { name: "Leg 5", anchor: [92, 0], direction: [1, 0], color: "#365541" },
  { name: "Leg 6", anchor: [75, 62], direction: [0.96, 0.34], color: "#406248" },
];

const state = {
  motors: [...DEFAULT_POSE],
  displayMotors: [...DEFAULT_POSE],
  homedAxes: Array(MOTOR_COUNT).fill(false),
  animationFrame: 0,
  sequenceToken: 0,
  feedRate: 200,
};

const elements = {
  motorControls: document.getElementById("motorControls"),
  gcodeInput: document.getElementById("gcodeInput"),
  programFileInput: document.getElementById("programFileInput"),
  reloadProgramButton: document.getElementById("reloadProgramButton"),
  runProgramButton: document.getElementById("runProgramButton"),
  stopProgramButton: document.getElementById("stopProgramButton"),
  resetPoseButton: document.getElementById("resetPoseButton"),
  feedSlider: document.getElementById("feedSlider"),
  feedNumber: document.getElementById("feedNumber"),
  feedReadout: document.getElementById("feedReadout"),
  sourceNote: document.getElementById("sourceNote"),
  statusLine: document.getElementById("statusLine"),
  canvas: document.getElementById("simCanvas"),
};

const ctx = elements.canvas.getContext("2d");

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function setStatus(message) {
  elements.statusLine.textContent = message;
}

function createMoveCommand(pose, previousPose, feed = SIM_FEED_REFERENCE, extra = {}) {
  const maxDelta = Math.max(...pose.map((value, index) => Math.abs(value - previousPose[index])));
  const effectiveFeed = Math.max(1, feed * (state.feedRate / SIM_FEED_REFERENCE));
  const durationMs = Math.max(180, Math.min(2400, (maxDelta / effectiveFeed) * 60000));

  return {
    type: "move",
    pose,
    effectiveFeed,
    durationMs,
    ...extra,
  };
}

function updateFeedRate(nextValue) {
  const feed = clamp(Number.isFinite(nextValue) ? nextValue : 200, 10, 2000);
  state.feedRate = feed;
  elements.feedSlider.value = String(feed);
  elements.feedNumber.value = String(feed);
  elements.feedReadout.textContent = `F${feed}`;
}

function createPresetCommands(code, previousPose, homedAxes) {
  const commands = [];
  let pose = [...previousPose];
  let nextHomedAxes = [...homedAxes];

  if (!nextHomedAxes.every(Boolean)) {
    const homePose = Array(MOTOR_COUNT).fill(0);
    commands.push(createMoveCommand(homePose, pose, 100, { homeAxes: Array(MOTOR_COUNT).fill(true) }));
    pose = homePose;
    nextHomedAxes = Array(MOTOR_COUNT).fill(true);
  }

  if (code === "H")
    return { commands, pose, homedAxes: nextHomedAxes };

  const target = code === "P2" ? 115 : 50;
  const overshoot = code === "P2" ? 20 : 5;
  const reversePose = pose.map(value => clamp(value - overshoot, 0, MAX_ANGLE));
  if (reversePose.some((value, index) => value !== pose[index])) {
    commands.push(createMoveCommand(reversePose, pose, 100));
    pose = reversePose;
  }

  const targetPose = Array(MOTOR_COUNT).fill(target);
  commands.push(createMoveCommand(targetPose, pose, 100));
  return { commands, pose: targetPose, homedAxes: nextHomedAxes };
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
    slider.min = String(MIN_ANGLE);
    slider.max = String(MAX_ANGLE);
    slider.step = "1";
    slider.id = `motor-slider-${index}`;
    slider.dataset.index = String(index);

    const number = document.createElement("input");
    number.type = "number";
    number.min = String(MIN_ANGLE);
    number.max = String(MAX_ANGLE);
    number.step = "1";
    number.id = `motor-number-${index}`;
    number.dataset.index = String(index);

    slider.addEventListener("input", () => updateMotor(index, Number(slider.value), true));
    number.addEventListener("input", () => updateMotor(index, Number(number.value), true));

    inputs.append(slider, number);
    wrapper.append(row, inputs);
    fragment.append(wrapper);
  });

  elements.motorControls.append(fragment);
  syncControls();
}

function syncControls() {
  state.displayMotors.forEach((angle, index) => {
    const rounded = Math.round(angle);
    const slider = document.getElementById(`motor-slider-${index}`);
    const number = document.getElementById(`motor-number-${index}`);
    const readout = document.getElementById(`motor-readout-${index}`);

    slider.value = String(rounded);
    number.value = String(rounded);
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
    setStatus(`Manual control: ${legDefinitions[index].name} set to ${Math.round(angle)} at F${state.feedRate}`);
    animatePose(targetPose, createMoveCommand(targetPose, state.displayMotors, SIM_FEED_REFERENCE).durationMs, token);
    return;
  }

  state.motors[index] = angle;
  state.displayMotors[index] = angle;
  syncControls();
  drawScene();
}

function resizeCanvas() {
  const ratio = window.devicePixelRatio || 1;
  const bounds = elements.canvas.getBoundingClientRect();
  const width = Math.max(480, Math.floor(bounds.width * ratio));
  const height = Math.max(360, Math.floor(bounds.height * ratio));

  elements.canvas.width = width;
  elements.canvas.height = height;
  drawScene();
}

function poseToTarget(angle) {
  const t = clamp(angle, MIN_ANGLE, MAX_ANGLE) / MAX_ANGLE;

  return {
    x: lerp(148 * LEG_SCALE, 44 * LEG_SCALE, t),
    y: lerp(-84 * LEG_SCALE, 110 * LEG_SCALE, t),
  };
}

function solveLeg(target) {
  const distance = clamp(
    Math.hypot(target.x, target.y),
    8,
    LEG_LENGTH_1 + LEG_LENGTH_2 - 0.0001
  );

  const baseAngle = Math.atan2(target.y, target.x);
  const shoulderOffset = Math.acos(
    clamp(
      (LEG_LENGTH_1 ** 2 + distance ** 2 - LEG_LENGTH_2 ** 2) / (2 * LEG_LENGTH_1 * distance),
      -1,
      1
    )
  );
  const elbowInner = Math.acos(
    clamp(
      (LEG_LENGTH_1 ** 2 + LEG_LENGTH_2 ** 2 - distance ** 2) /
        (2 * LEG_LENGTH_1 * LEG_LENGTH_2),
      -1,
      1
    )
  );

  const shoulderAngle = baseAngle + shoulderOffset;
  const elbowAngle = shoulderAngle - (Math.PI - elbowInner);

  return {
    joint: {
      x: Math.cos(shoulderAngle) * LEG_LENGTH_1,
      y: Math.sin(shoulderAngle) * LEG_LENGTH_1,
    },
    foot: {
      x: target.x,
      y: target.y,
    },
    shadow: lerp(1, 0.56, clamp(target.y / 110, 0, 1)),
  };
}

function projectPoint(anchor, direction, point) {
  return {
    x: anchor[0] + direction[0] * point.x,
    y: anchor[1] + direction[1] * point.x - point.y,
  };
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

function drawLeg(definition, angle, centerX, centerY, scale) {
  const solved = solveLeg(poseToTarget(angle));
  const anchor = [
    definition.anchor[0] * BODY_SCALE,
    definition.anchor[1] * BODY_SCALE,
  ];
  const direction = definition.direction;
  const joint = projectPoint(anchor, direction, solved.joint);
  const foot = projectPoint(anchor, direction, solved.foot);

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
    ctx.fillText(`${LEG_AXES[index]} ${Math.round(state.displayMotors[index])}°`, leg.anchor[0], leg.anchor[1] - 18);
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

function animatePose(targetPose, durationMs, token) {
  const startPose = [...state.displayMotors];
  const startTime = performance.now();

  cancelAnimationFrame(state.animationFrame);

  return new Promise((resolve) => {
    function tick(now) {
      if (token !== undefined && state.sequenceToken !== token) {
        resolve(false);
        return;
      }

      const elapsed = now - startTime;
      const progress = durationMs <= 0 ? 1 : clamp(elapsed / durationMs, 0, 1);

      state.displayMotors = startPose.map((value, index) => lerp(value, targetPose[index], progress));
      state.motors = [...targetPose];
      syncControls();
      drawScene();

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
    const timeoutId = window.setTimeout(() => {
      resolve();
    }, ms);

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

    const command = parseLine(line, previousPose);
    if (!command) {
      return;
    }

    commands.push(command);

    if (command.type === "move") {
      previousPose = [...command.pose];
      if (command.homeAxes)
        homedAxes = homedAxes.map((value, index) => value || command.homeAxes[index]);
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
    elements.sourceNote.innerHTML = `Source file: <code>${file.name}</code>`;
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
        setStatus(
          `Dwelling for ${Math.round(command.ms)} ms${loopForever ? `, cycle ${cycle}` : ""}`
        );
        await sleep(command.ms, token);
        continue;
      }

      setStatus(
        `Move ${index + 1}/${commands.length}${loopForever ? `, cycle ${cycle}` : ""}: ${command.pose
          .map((v) => Math.round(v))
          .join(", ")}`
      );
      const completed = await animatePose(command.pose, command.durationMs, token);
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

function resetPose() {
  stopSequence();
  state.motors = [...DEFAULT_POSE];
  state.displayMotors = [...DEFAULT_POSE];
  state.homedAxes = Array(MOTOR_COUNT).fill(false);
  syncControls();
  drawScene();
  setStatus("Pose reset.");
}

elements.runProgramButton.addEventListener("click", runProgram);
elements.reloadProgramButton.addEventListener("click", openProgramPicker);
elements.programFileInput.addEventListener("change", handleProgramSelection);
elements.feedSlider.addEventListener("input", () => updateFeedRate(Number(elements.feedSlider.value)));
elements.feedNumber.addEventListener("change", () => updateFeedRate(Number(elements.feedNumber.value)));
elements.stopProgramButton.addEventListener("click", () => {
  stopSequence();
  state.motors = [...state.displayMotors];
  syncControls();
  drawScene();
  setStatus("Program stopped.");
});
elements.resetPoseButton.addEventListener("click", resetPose);

window.addEventListener("resize", resizeCanvas);

createMotorControls();
updateFeedRate(state.feedRate);
resizeCanvas();
drawScene();
