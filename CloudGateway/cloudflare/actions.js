const types = new Set(['home', 'pos1', 'pos2', 'pos3', 'random_position', 'random_loop', 'pause', 'resume',
  'stop', 'standby', 'light_on', 'light_off', 'set_brightness', 'set_feed', 'move', 'reset_octopus']);
const fail = message => { throw Object.assign(new Error(message), { status: 400 }); };
const integer = (value, min, max) => Number.isInteger(value) && value >= min && value <= max;
export function validateAction(body) {
  if (!types.has(body.type)) fail('Unsupported action');
  const payload = {};
  if (body.type === 'set_brightness') {
    if (!integer(body.brightness, 0, 255)) fail('Brightness must be from 0 to 255');
    payload.brightness = body.brightness;
  }
  if (body.type === 'set_feed' || body.type === 'move') {
    if (!integer(body.feed, 10, 600)) fail('Feed must be from 10 to 600 mm/min');
    payload.feed = body.feed;
  }
  if (body.type === 'move') {
    if (!body.axes || typeof body.axes !== 'object' || Array.isArray(body.axes) ||
        !Object.keys(body.axes).length || Object.keys(body.axes).length > 6) fail('Choose an axis to move');
    payload.axes = {};
    for (const [axis, value] of Object.entries(body.axes)) {
      if (!'XYZABC'.includes(axis) || axis.length !== 1 || !Number.isFinite(value) || value < 0 || value > 120)
        fail('Axis positions must be from 0 to 120 mm');
      payload.axes[axis] = value;
    }
  }
  return payload;
}
