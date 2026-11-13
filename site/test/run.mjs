// End-to-end check of the setup app's protocol: src/setup/app.js's framing,
// request client and serial transport against src/setup/mock.js's
// independent board-side implementation. Ported from the author's earlier
// e-paper project's setup app tests, plus the Sticky's differences
// (CH343 USB bridge, Bluetooth only in setup mode, its key groups).
//   node test/run.mjs        (from devices/reterminal-sticky/site)
import assert from 'node:assert/strict';
import {
  encodeFrames, Reassembler, Client, DeviceError, redactRequest, groupKeys, keyLabel, isAdvanced,
  FRAME_LARGE, FRAME_SMALL, UUIDS, NAME_PREFIX, PROTOCOL, TIMEZONES, zoneForPosix, looksLikePosixTz,
  SerialTransport, SERIAL_FILTERS, WCH_VID, ESPRESSIF_VID, reconnectAfterRestart,
  wifiFromKey, wifiSetValue, normalizeKey, commitRestarts, GROUPS, KEY_INFO, icsSetValue, icsLabels, ICS_MAX, groupOf,
  toGrey, resample, adjust, dither4, pack2, unpack2, cropFor, fitSize, buildPhoto, photoFile, parsePhotoHead, photoTitle, titleFromName,
  uploadPhoto, sha256Hex, toBase64, fromBase64, photoAreas, workSize, fullCrop, PHOTO_AREA, PHOTO_HEAD, PHOTO_KIND, PHOTO_THUMB,
} from '../src/setup/app.js';
import { MockDevice, MockTransport, MockSerial, CH343, parsePhoto, PHOTO_LIMITS } from '../src/setup/mock.js';

let passed = 0;
const tests = [];
const test = (name, fn) => tests.push([name, fn]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const te = new TextEncoder();

// A board in setup mode whose "OS pairing prompt" answers with the right code (or a given one).
function rig({ mtu = 185, code, storage } = {}) {
  const device = new MockDevice({ mtu, bootMs: 200, storage });
  const prompts = [];
  const transport = new MockTransport(device, { passkeyPrompt: async (d) => { prompts.push(d.passkey); return code ?? d.passkey; } });
  const logs = [];
  return { device, transport, prompts, logs, client: () => new Client(transport, { log: (d, t) => logs.push([d, t]) }) };
}

// On the board: Settings → Connect to the setup app, then wait for the restart.
async function backToSetup(device) { device.enterSetup(); await sleep(device.bootMs + 50); }

// ---------------------------------------------------------------- framing
test('frames: header bits, sizes, round trip', () => {
  const msg = { id: 1, op: 'set', key: 'device.name', value: 'x'.repeat(500) };
  const frames = encodeFrames(msg, FRAME_LARGE);
  assert.equal(frames.length, Math.ceil(te.encode(JSON.stringify(msg)).length / 180));
  frames.forEach((f, i) => {
    assert.ok(f.length <= FRAME_LARGE);
    assert.equal(f[0] & 0x7f, i);
    assert.equal(!!(f[0] & 0x80), i === frames.length - 1);
  });
  const r = new Reassembler();
  let out = null; for (const f of frames) out = r.push(f);
  assert.deepEqual(JSON.parse(out), msg);
});

test('frames: a short message is one frame with the last bit set', () => {
  const [f, ...rest] = encodeFrames('{}', FRAME_SMALL);
  assert.equal(rest.length, 0); assert.equal(f[0], 0x80); assert.equal(f.length, 3);
});

test('frames: multi-byte UTF-8 split across frame borders', () => {
  const msg = { v: '🦊ăîșțâ€'.repeat(40) };
  const frames = encodeFrames(msg, 7);
  const r = new Reassembler(); let out;
  for (const f of frames) out = r.push(f);
  assert.deepEqual(JSON.parse(out), msg);
});

test('frames: sequence number wraps at 128', () => {
  const msg = 'a'.repeat(19 * 300);
  const frames = encodeFrames(msg, FRAME_SMALL);
  assert.equal(frames.length, 300);
  assert.equal(frames[127][0] & 0x7f, 127); assert.equal(frames[128][0] & 0x7f, 0); assert.equal(frames[299][0], 0x80 | (299 % 128));
  const r = new Reassembler({ maxBytes: 1e6 }); let out;
  for (const f of frames) out = r.push(f);
  assert.equal(out, msg);
});

test('frames: a gap drops the message, the next one still arrives', () => {
  const errs = [];
  const r = new Reassembler({ onError: (e) => errs.push(e) });
  const a = encodeFrames('x'.repeat(60), 20);
  assert.equal(r.push(a[0]), null); assert.equal(r.push(a[2]), null);
  assert.match(errs[0], /gap/);
  let out; for (const f of encodeFrames('{"ok":true}', 20)) out = r.push(f);
  assert.equal(out, '{"ok":true}');
});

test('frames: a new first fragment mid-message restarts (sender retried)', () => {
  const errs = [];
  const r = new Reassembler({ onError: (e) => errs.push(e) });
  const a = encodeFrames('x'.repeat(60), 20);
  r.push(a[0]); r.push(a[1]);
  let out; for (const f of encodeFrames('{"id":2}', 20)) out = r.push(f);
  assert.equal(out, '{"id":2}'); assert.match(errs[0], /new message/);
});

test('frames: a request longer than 128 frames reaches the board (seq wraps)', async () => {
  const r = rig({ mtu: 23 });
  await r.transport.connect();
  const c = r.client();
  c.frameSize = FRAME_SMALL;
  const res = await c.request('info', { pad: 'p'.repeat(2600) });
  assert.ok(res.info.board);
  assert.ok(r.device.stats.framesIn > 128, `framesIn ${r.device.stats.framesIn}`);
});

test('constants: the shared wire identity (protocol 1, one UUID block)', () => {
  assert.equal(PROTOCOL, 1);
  assert.deepEqual(UUIDS, {
    service: 'edb50001-3985-44d0-a3dd-fd2767d3a6dd', info: 'edb50002-3985-44d0-a3dd-fd2767d3a6dd',
    rx: 'edb50003-3985-44d0-a3dd-fd2767d3a6dd', tx: 'edb50004-3985-44d0-a3dd-fd2767d3a6dd',
  });
  assert.equal(NAME_PREFIX, 'Sticky');
});

// --------------------------------------------------------------- pairing
test('pairing: right passkey bonds once, wrong passkey fails', async () => {
  const bad = rig({ code: '000000' });
  bad.device.random = () => 0.5;
  await assert.rejects(bad.transport.connect(), { name: 'SecurityError' });
  assert.equal(bad.device.flash.bonded, false);
  const ok = rig();
  await ok.transport.connect();
  assert.equal(ok.prompts.length, 1); assert.match(ok.prompts[0], /^\d{6}$/);
  ok.transport.close();
  await ok.transport.connect();
  assert.equal(ok.prompts.length, 1, 'bonded: no second prompt');
});

// ------------------------------------------------------------ requests
test('info characteristic and info op agree', async () => {
  const { transport, client } = rig();
  await transport.connect();
  const c = client();
  const fromChar = JSON.parse(await transport.readInfo());
  const fromOp = await c.request('info');
  for (const f of ['proto', 'id', 'fw', 'commit', 'board', 'touch', 'wifi', 'keys']) assert.ok(f in fromChar, f);
  assert.equal(fromChar.board, 'reterminal-sticky');
  assert.equal(fromOp.ok, true);
  assert.deepEqual(fromOp.info, fromChar);
});

test('keys: the Sticky key list, grouped; secrets never come back', async () => {
  const { transport, client } = rig();
  await transport.connect();
  const c = client();
  await c.request('set', { key: 'openai.api_key', value: 'sk-test-SECRET-123' });
  await c.request('set', { key: 'wifi.networks', value: [{ ssid: 'HomeNet', password: 'hunter2hunter2' }] });
  const { keys } = await c.request('keys');
  const api = keys.find((k) => k.key === 'openai.api_key');
  assert.deepEqual([api.secret, api.set, 'value' in api], [true, true, false]);
  const w = keys.find((k) => k.key === 'wifi.networks');
  assert.equal(w.secret, true); assert.ok(!('value' in w));
  assert.deepEqual(wifiFromKey(w), ['HomeNet'], 'SSIDs come from "ssids"');
  const raw = JSON.stringify(keys);
  assert.ok(!raw.includes('SECRET') && !raw.includes('hunter2'), 'no secret value in the key list');
  const groups = groupKeys(keys.map(normalizeKey));
  assert.deepEqual(groups.map((g) => g.id), ['wifi', 'device', 'weather', 'voice', 'mqtt', 'claude', 'calendar', 'telemetry', 'quotes', 'games', 'update']);
  assert.deepEqual(groups.filter((g) => !g.optional).map((g) => g.id), ['wifi', 'device', 'weather', 'voice']);
  const voice = groups.find((g) => g.id === 'voice');
  assert.deepEqual(voice.keys.filter((k) => !isAdvanced(k)).map((k) => k.key), ['openai.api_key'], 'a first-time visitor sees only the key');
  const adv = keys.filter(isAdvanced).map((k) => k.key).sort();
  assert.deepEqual(adv, ['influx.bucket', 'influx.org', 'influx.token', 'influx.url', 'mqtt.ca', 'notes.ai_model', 'notes.stt_model',
    'openai.relay_host', 'openai.relay_port', 'update.url', 'voice.stt_local']);
  assert.equal(keys.find((k) => k.key === 'update.auto').value, false, 'updates install by themselves only when asked to');
  // every key the mock reports has a label in the table
  for (const k of keys) assert.ok(KEY_INFO[k.key]?.label, `no label for ${k.key}`);
});

test('calendar links: the value the app sends; webcal is https; bad links are refused with a reason', () => {
  assert.equal(icsSetValue([{ label: 'Work', url: ' webcal://p01-calendars.example.com/published/2/abc ' }, { url: '' }, { label: ' ', url: 'https://cal.example.com/x.ics' }]),
    'Work https://p01-calendars.example.com/published/2/abc\nhttps://cal.example.com/x.ics');
  assert.equal(icsSetValue([{ label: 'My  family', url: 'https://a.example/x' }]), 'My family https://a.example/x', 'spaces in a label squeezed');
  assert.equal(icsSetValue([]), '');
  assert.throws(() => icsSetValue([{ url: 'http://plain.example/x.ics' }]), /https:\/\/ or webcal/);
  assert.throws(() => icsSetValue([{ url: 'https://a.example/has space' }]), /no spaces/);
  assert.throws(() => icsSetValue([{ label: 'Orphan', url: '' }]), /paste its link/);
  assert.throws(() => icsSetValue([{ label: 'x'.repeat(25), url: 'https://a.example/x' }]), /24 characters/);
  assert.throws(() => icsSetValue(Array.from({ length: ICS_MAX + 1 }, (_, i) => ({ url: `https://a.example/${i}` }))), /At most 3/);
  assert.deepEqual(icsLabels({ labels: ['Work', '', 7] }), ['Work', '', '']);
  assert.deepEqual(icsLabels({}), []);
  assert.equal(KEY_INFO['calendar.ics'].type, 'ics');
  assert.ok(!isAdvanced({ key: 'calendar.ics' }), 'the links show without opening Advanced');
});

test('calendar links: the board keeps them secret, lists only the labels, and checks them', async () => {
  const { transport, client } = rig();
  await transport.connect();
  const c = client();
  const value = icsSetValue([{ label: 'Work', url: 'https://calendar.example.com/ical/TOKEN-SECRET-1/basic.ics' }, { url: 'webcal://p02.example.com/published/TOKEN-SECRET-2' }]);
  await c.request('set', { key: 'calendar.ics', value });
  const { keys } = await c.request('keys');
  const k = keys.find((x) => x.key === 'calendar.ics');
  assert.deepEqual([k.secret, k.set, 'value' in k], [true, true, false]);
  assert.deepEqual(icsLabels(k), ['Work', '']);
  assert.ok(!JSON.stringify(keys).includes('TOKEN-SECRET'), 'no address in the key list');
  assert.equal(groupOf('calendar.ics'), 'calendar');
  await assert.rejects(c.request('set', { key: 'calendar.ics', value: 'http://plain.example/x.ics' }), /https/);
  await assert.rejects(c.request('set', { key: 'calendar.ics', value: ['https://a.example/1', 'https://a.example/2', 'https://a.example/3', 'https://a.example/4'].join('\n') }), /at most 3/);
  assert.equal((await c.request('set', { key: 'calendar.ics', value: 'webcal://p03.example.com/published/x' })).ok, true);
  assert.deepEqual(icsLabels((await c.request('keys')).keys.find((x) => x.key === 'calendar.ics')), ['']);
  await c.request('clear', { key: 'calendar.ics' });
  assert.equal((await c.request('keys')).keys.find((x) => x.key === 'calendar.ics').set, false);
});

test('keys: an unknown key still shows, in its own group, labelled from its name', () => {
  const keys = ['wifi.networks', 'display.refresh_min', 'weather.place', 'zz.thing'].map(normalizeKey);
  const groups = groupKeys(keys);
  assert.deepEqual(groups.map((g) => g.id), ['wifi', 'weather', 'display', 'zz']);
  assert.equal(groups[2].optional, true);
  assert.equal(keyLabel(keys[1]), 'Refresh min');
  assert.equal(keyLabel({ key: 'x.y', label: 'From the board' }), 'From the board');
  assert.equal(keyLabel({ key: 'weather.place', label: 'From the board' }), 'Town', 'the table wins');
  assert.ok(GROUPS.every((g) => g.prefixes.length));
});

test('wifi value shapes: value [{ssid}], value [ssid] or ssids [...]; set keeps passwords by omission', () => {
  assert.deepEqual(wifiFromKey({ value: [{ ssid: 'A' }, { ssid: 'B' }] }), ['A', 'B']);
  assert.deepEqual(wifiFromKey({ value: ['A'] }), ['A']);
  assert.deepEqual(wifiFromKey({ ssids: ['A', 'B'] }), ['A', 'B']);
  assert.deepEqual(wifiFromKey({ value: null }), []);
  assert.deepEqual(wifiSetValue([{ ssid: 'A', password: undefined }, { ssid: 'B', password: 'pw-12345' }, { ssid: 'C', password: '' }]),
    [{ ssid: 'A' }, { ssid: 'B', password: 'pw-12345' }, { ssid: 'C', password: '' }]);
});

test('set: board-side validation errors reach the caller', async () => {
  const { transport, client } = rig();
  await transport.connect();
  const c = client();
  await assert.rejects(c.request('set', { key: 'mqtt.port', value: 70000 }), (e) => e instanceof DeviceError && /1\.\.65535/.test(e.message));
  await assert.rejects(c.request('set', { key: 'openai.api_key', value: 'nope' }), /sk-/);
  await assert.rejects(c.request('set', { key: 'weather.lat', value: 'north' }), /38\.72/);
  await assert.rejects(c.request('set', { key: 'wifi.networks', value: [{ ssid: 'NeverSaved' }] }), /no stored password/);
  await assert.rejects(c.request('set', { key: 'no.such', value: 1 }), /unknown key/);
  await assert.rejects(c.request('bogus'), /unknown op/);
  assert.equal((await c.request('set', { key: 'mqtt.port', value: 1883 })).ok, true);
});

test('wifi: scan, test join ok and failing', async () => {
  const { transport, client } = rig();
  await transport.connect();
  const c = client();
  const { networks } = await c.request('wifi_scan');
  assert.ok(networks.length >= 3 && networks.every((n) => 'ssid' in n && 'rssi' in n && 'secure' in n));
  const good = await c.request('wifi_test', { ssid: 'HomeNet', password: 'correct horse' });
  assert.equal(good.ok, true); assert.match(good.ip, /^192\.0\.2\./);
  await assert.rejects(c.request('wifi_test', { ssid: 'HomeNet', password: 'wrong password' }), /auth failed/);
  await assert.rejects(c.request('wifi_test', { ssid: 'Nowhere', password: '12345678' }), /no network/);
});

test('bluetooth: commit saves, restarts and ends setup mode; back in setup mode the settings are there', async () => {
  const mem = new Map(); const storage = { getItem: (k) => mem.get(k) ?? null, setItem: (k, v) => mem.set(k, v) };
  const r = rig({ storage });
  await r.transport.connect();
  let dropped = 0;
  const c = new Client(r.transport, { onDisconnect: () => dropped++ });
  await c.request('set', { key: 'wifi.networks', value: [{ ssid: 'HomeNet', password: 'first-password' }, { ssid: 'CafeFree', password: '' }] });
  await c.request('set', { key: 'device.name', value: 'Küche 🦊' });
  await c.request('set', { key: 'device.timezone', value: 'EET-2EEST,M3.5.0/3,M10.5.0/4' });
  await c.request('set', { key: 'weather.locate', value: false });
  await c.request('set', { key: 'mqtt.password', value: 'mqtt-secret' });
  const done = await c.request('commit');
  assert.equal(commitRestarts(done), true); assert.equal(done.changed, true);
  await sleep(400);
  assert.equal(dropped, 1, 'link dropped by the restart');
  await sleep(300);
  assert.equal(r.device.setupMode, false, 'booted normally');
  await assert.rejects(r.transport.connect(), /failed/, 'not advertising outside setup mode');
  await assert.rejects(new MockTransport(r.device).choose(), { name: 'NotFoundError' }, 'the chooser finds nothing');
  await backToSetup(r.device);
  await r.transport.connect();
  const c2 = new Client(r.transport);
  const { keys } = await c2.request('keys');
  const v = Object.fromEntries(keys.map((k) => [k.key, k]));
  assert.equal(v['device.name'].value, 'Küche 🦊');
  assert.equal(v['weather.locate'].value, false);
  assert.equal(v['mqtt.password'].set, true);
  assert.deepEqual(wifiFromKey(v['wifi.networks']), ['HomeNet', 'CafeFree']);
  assert.equal(r.device.name, 'Sticky Küche 🦊');
  // reorder, keep HomeNet's password by omitting it
  await c2.request('set', { key: 'wifi.networks', value: wifiSetValue([{ ssid: 'CafeFree' }, { ssid: 'HomeNet' }]) });
  await c2.request('commit');
  await sleep(600);
  const stored = JSON.parse(mem.get('sticky-demo-board-v1')).values['wifi.networks'];
  assert.deepEqual(stored, [{ ssid: 'CafeFree', password: '' }, { ssid: 'HomeNet', password: 'first-password' }]);
  assert.equal(zoneForPosix(v['device.timezone'].value), 'Europe/Athens');
  assert.equal(zoneForPosix(v['device.timezone'].value, 'Europe/Bucharest'), 'Europe/Bucharest');
});

test('bluetooth: commit with nothing staged ends setup mode without changing anything', async () => {
  const r = rig();
  await r.transport.connect();
  const res = await r.client().request('commit');
  assert.equal(res.changed, false); assert.equal(commitRestarts(res), false);
  await sleep(600);
  assert.equal(r.device.setupMode, false);
});

test('clear, and changes dropped by a disconnect without commit', async () => {
  const r = rig();
  await r.transport.connect();
  await r.client().request('set', { key: 'openai.api_key', value: 'sk-abc' });
  await r.client().request('commit'); await sleep(600);
  await backToSetup(r.device); await r.transport.connect();
  const c2 = r.client();
  await c2.request('clear', { key: 'openai.api_key' });
  assert.equal((await c2.request('keys')).keys.find((k) => k.key === 'openai.api_key').set, false);
  r.transport.close();
  await r.transport.connect();
  assert.equal((await r.client().request('keys')).keys.find((k) => k.key === 'openai.api_key').set, true);
});

test('small MTU: the client falls back to 20-byte writes and still works', async () => {
  const r = rig({ mtu: 23 });
  await r.transport.connect();
  const c = r.client();
  await c.request('set', { key: 'openai.api_key', value: 'sk-' + 'k'.repeat(160) });
  assert.equal(c.frameSize, FRAME_SMALL);
  assert.ok(r.device.stats.rejectedWrites >= 1);
  assert.ok(r.logs.some(([d, t]) => d === 'warn' && /20-byte/.test(t)));
  const { keys } = await c.request('keys');
  assert.ok(keys.length > 20);
  assert.ok(r.device.stats.framesOut > 50, `framesOut ${r.device.stats.framesOut}`);
});

test('concurrent requests are serialised and matched by id', async () => {
  const r = rig({ mtu: 23 });
  r.device.latency = 1;
  await r.transport.connect();
  const c = r.client();
  const [a, b, d] = await Promise.all([c.request('info'), c.request('keys'), c.request('wifi_scan')]);
  assert.ok(a.info.board && b.keys && d.networks);
  assert.ok(a.id < b.id && b.id < d.id);
});

test('timeouts and disconnects reject pending requests', async () => {
  const r = rig();
  await r.transport.connect();
  const c = r.client();
  r.device.handle = () => new Promise(() => {});
  await assert.rejects(c.request('info', {}, { timeout: 50 }), /No answer/);
  const p = c.request('info', {}, { timeout: 5000 });
  await sleep(10);
  r.device.reboot();
  await assert.rejects(p, /Disconnected/);
});

test('factory reset wipes settings and the bond', async () => {
  const r = rig();
  await r.transport.connect();
  await r.client().request('set', { key: 'mqtt.host', value: 'broker.example.com' });
  await r.client().request('commit'); await sleep(600);
  await backToSetup(r.device); await r.transport.connect();
  await r.client().request('factory_reset'); await sleep(600);
  await backToSetup(r.device); await r.transport.connect();
  assert.equal(r.prompts.length, 2, 'pairs again after the reset');
  const { keys } = await r.client().request('keys');
  assert.ok(keys.every((k) => !k.set));
});

test('redaction: secrets never reach the log', async () => {
  const isSecret = (k) => k === 'openai.api_key';
  assert.equal(redactRequest({ id: 1, op: 'set', key: 'openai.api_key', value: 'sk-x' }, isSecret).value, '•••');
  assert.equal(redactRequest({ id: 1, op: 'set', key: 'mqtt.host', value: 'h' }, isSecret).value, 'h');
  assert.equal(redactRequest({ id: 1, op: 'wifi_test', ssid: 's', password: 'p' }).password, '•••');
  assert.deepEqual(redactRequest({ id: 1, op: 'set', key: 'wifi.networks', value: [{ ssid: 'a', password: 'p' }, { ssid: 'b' }] }).value,
    [{ ssid: 'a', password: '•••' }, { ssid: 'b' }]);
  const logs = [];
  const fake = { onNotify() {}, onDisconnect() {}, write: async () => {} };
  const c = new Client(fake, { log: (d, t) => logs.push(t), redact: (q) => redactRequest(q, isSecret) });
  await c.request('set', { key: 'openai.api_key', value: 'sk-LEAK' }, { timeout: 10 }).catch(() => {});
  assert.ok(logs.length === 1 && !logs[0].includes('LEAK'));
});

test('time zones: table is well-formed', () => {
  const names = new Set();
  for (const [n, p] of TIMEZONES) {
    assert.ok(!names.has(n), `duplicate ${n}`); names.add(n);
    assert.match(p, /^(<[+-]?\w+>|[A-Z]{3,4})[+-]?\d/, `${n}: ${p}`);
    assert.ok(looksLikePosixTz(p), p);
    new Intl.DateTimeFormat('en', { timeZone: n });   // catches typos in IANA names
  }
  assert.ok(!looksLikePosixTz('Europe/Lisbon')); assert.ok(!looksLikePosixTz('+3'));
});

// ------------------------------------------------------------ USB serial
// app.js's real SerialTransport against mock.js's navigator.serial stand-in.
// Default: the Sticky's CH343 bridge (resets on open, stays on USB through a restart).
function serialRig({ device, timeouts = { info: 150, ready: 1500 }, ...opts } = {}) {
  device ??= new MockDevice({ bootMs: 200 });
  const serial = new MockSerial(device, { logEveryMs: 40, ...opts });
  const t = new SerialTransport({ serial, timeouts });
  const logs = [];
  t.onLog((l) => logs.push(l));
  return { device, serial, t, logs };
}

test('serial: the CH343 port resets the board on open; waits for the boot banner, then answers', async () => {
  const { serial, t, logs } = serialRig();
  await t.choose();
  assert.deepEqual(t.usb, CH343);
  assert.equal(t.usb.usbVendorId, WCH_VID);
  const t0 = Date.now();
  await t.connect();
  assert.equal(serial.stats.resets, 1);
  assert.deepEqual(serial.port.signals[0], { dataTerminalReady: false, requestToSend: false }, 'DTR/RTS released right after open');
  assert.ok(serial.stats.cfgIn >= 2, 'first info lost while booting, then asked again');
  assert.ok(Date.now() - t0 >= 200);
  assert.ok(logs.some((l) => l.includes('@cfg {"ready":true')), 'banner kept in the board log');
  assert.ok(logs.some((l) => l.startsWith('ESP-ROM')), 'ROM boot lines are log, not protocol');
  const info = JSON.parse(await t.readInfo());
  assert.equal(info.board, 'reterminal-sticky'); assert.equal(info.proto, 1);
  assert.equal(t.name, info.id);
  await t.close();
});

test('serial: no reset on open answers at once; requests work, log lines are not messages', async () => {
  const { t, logs, serial } = serialRig({ resetOnOpen: false });
  await t.choose();
  const t0 = Date.now();
  await t.connect();
  assert.ok(Date.now() - t0 < 150, 'no waiting without a reset');
  const clientLog = [];
  const c = new Client(t, { log: (d, x) => clientLog.push([d, x]) });
  await c.request('set', { key: 'openai.api_key', value: 'sk-serial-SECRET' });
  await c.request('set', { key: 'device.name', value: 'Küche 🦊' });
  await sleep(150);
  const { keys } = await c.request('keys');
  assert.ok(!JSON.stringify(keys).includes('SECRET'));
  assert.equal(keys.find((k) => k.key === 'device.name').value, 'Küche 🦊');
  await assert.rejects(c.request('set', { key: 'mqtt.port', value: 0 }), /1\.\.65535/);
  assert.ok(logs.filter((l) => l.startsWith('[')).length >= 3, 'log lines reach onLog');
  assert.ok(!clientLog.some(([d]) => d === 'warn'), 'no log line was mistaken for a message');
  assert.ok(serial.stats.logLines > 0);
  await t.close();
});

test('serial: lines split across arbitrary reads, log kept to the last 200 lines', async () => {
  const device = new MockDevice({ bootMs: 50 });
  const serial = new MockSerial(device, { resetOnOpen: false, chunk: 3, logEveryMs: 1 });
  const t = new SerialTransport({ serial, keepLines: 200, timeouts: { info: 300, ready: 500 } });
  await t.choose(); await t.connect();
  const { keys } = await new Client(t).request('keys');   // ~2 KB answer in 1-3 byte chunks
  assert.ok(keys.length > 20);
  await sleep(400);
  assert.equal(t.lines.length, 200);
  await t.close();
});

test('serial: commit on the CH343, the port stays open and the board comes back on its banner', async () => {
  const { t, serial } = serialRig();
  await t.choose(); await t.connect();
  const c = new Client(t);
  await c.request('set', { key: 'device.timezone', value: 'CET-1CEST,M3.5.0,M10.5.0/3' });
  await c.request('set', { key: 'mqtt.password', value: 'pw-SECRET' });
  const port = t.port;
  const res = await c.request('commit');
  assert.equal(commitRestarts(res), true);
  await reconnectAfterRestart(t, { timeoutMs: 5000 });
  assert.equal(t.port, port); assert.equal(t.connected, true); assert.equal(serial.stats.opens, 1, 'no reopen needed');
  const { keys } = await new Client(t).request('keys');
  const v = Object.fromEntries(keys.map((k) => [k.key, k]));
  assert.equal(v['device.timezone'].value, 'CET-1CEST,M3.5.0,M10.5.0/3');
  assert.equal(v['mqtt.password'].set, true); assert.ok(!('value' in v['mqtt.password']));
  await t.close();
});

test('serial: commit with nothing staged does not restart the board', async () => {
  const { t, serial, logs } = serialRig({ resetOnOpen: false });
  await t.choose(); await t.connect();
  const res = await new Client(t).request('commit');
  assert.equal(res.changed, false); assert.equal(commitRestarts(res), false);
  await sleep(400);
  assert.equal(t.connected, true);
  assert.ok(!logs.some((l) => l.startsWith('ESP-ROM')), 'no restart');
  assert.equal(serial.stats.opens, 1);
  await t.close();
});

test('serial: native USB, the port drops on commit and comes back as a new port; settings kept', async () => {
  const { t, serial } = serialRig({ dropOnRestart: true, usb: { usbVendorId: ESPRESSIF_VID, usbProductId: 0x1001 } });
  await t.choose(); await t.connect();
  let dropped = 0;
  const c = new Client(t, { onDisconnect: () => dropped++ });
  await c.request('set', { key: 'mqtt.host', value: 'broker.example.com' });
  const oldPort = t.port;
  await c.request('commit');
  await reconnectAfterRestart(t, { timeoutMs: 8000 });
  assert.equal(dropped, 1);
  assert.notEqual(t.port, oldPort, 'reopened the re-enumerated port');
  assert.equal(serial.stats.lost, 1);
  const { keys } = await new Client(t).request('keys');
  assert.equal(keys.find((k) => k.key === 'mqtt.host').value, 'broker.example.com');
  await t.close();
});

test('serial: reconnect after an unplug finds the re-enumerated port', async () => {
  const { t, serial } = serialRig({ resetOnOpen: false });
  await t.choose(); await t.connect();
  const old = t.port;
  old._lose(); serial.present = false;
  await sleep(10);
  await assert.rejects(t.connect());
  serial.port = new (old.constructor)(serial); serial.present = true;
  await t.connect();
  assert.notEqual(t.port, old); assert.equal(t.connected, true);
  await t.close();
});

test('serial: firmware that never answers gives a clear error and closes the port', async () => {
  const { t, serial } = serialRig({ silent: true, timeouts: { info: 100, ready: 300 } });
  await t.choose();
  await assert.rejects(t.connect(), /No answer on the USB port/);
  assert.equal(t.connected, false); assert.equal(serial.open, null);
  serial.silent = false; serial.resetOnOpen = false;
  await t.connect(); assert.equal(t.connected, true);
  await t.close();
});

test('serial: unplugging rejects the pending request and reports the disconnect', async () => {
  const { t, serial, device } = serialRig({ resetOnOpen: false });
  await t.choose(); await t.connect();
  let dropped = 0;
  const c = new Client(t, { onDisconnect: () => dropped++ });
  device.handle = () => new Promise(() => {});
  const p = c.request('info', {}, { timeout: 5000 });
  await sleep(20);
  serial.port._lose();
  await assert.rejects(p, /Disconnected/);
  assert.equal(dropped, 1); assert.equal(t.connected, false);
  await assert.rejects(c.request('info'), /closed/);
});

test('serial: the chooser lists the CH343 (WCH) and native Espressif USB', async () => {
  assert.deepEqual(SERIAL_FILTERS, [{ usbVendorId: 0x1a86 }, { usbVendorId: 0x303a }]);
  for (const usb of [CH343, { usbVendorId: 0x303a, usbProductId: 0x1001 }]) {
    const serial = new MockSerial(new MockDevice(), { usb });
    let seen;
    const spy = { requestPort: async (o) => { seen = o; return serial.requestPort(o); }, getPorts: () => serial.getPorts() };
    const t = new SerialTransport({ serial: spy });
    await t.choose();
    assert.deepEqual(seen, { filters: SERIAL_FILTERS });
    assert.deepEqual(t.usb, usb);
  }
  const other = new MockSerial(new MockDevice(), { usb: { usbVendorId: 0x10c4, usbProductId: 0xea60 } });
  await assert.rejects(new SerialTransport({ serial: other }).choose(), { name: 'NotFoundError' }, 'a CP210x board is not offered');
});


// ------------------------------------------------------------- photos
// A test picture: a diagonal gradient with a dark square, w x h, as grey.
function gradient(w, h) {
  const g = new Float32Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) g[y * w + x] = (x / (w - 1) * 0.6 + y / (h - 1) * 0.4) * 255;
  for (let y = h >> 2; y < h >> 1; y++) for (let x = w >> 2; x < w >> 1; x++) g[y * w + x] = 20;
  return g;
}
const mean = (a) => a.reduce((s, v) => s + v, 0) / a.length;

test('photos: dithering keeps four levels and the average grey', () => {
  for (const v of [0, 40, 128, 200, 255]) {
    const lv = dither4(new Float32Array(64 * 48).fill(v), 64, 48);
    assert.ok(lv.every((l) => l >= 0 && l <= 3));
    assert.ok(Math.abs(mean(Array.from(lv, (l) => l * 85)) - v) < 3, `grey ${v}`);
  }
  assert.ok(dither4(new Float32Array(100).fill(0), 10, 10).every((l) => l === 0));
  assert.ok(dither4(new Float32Array(100).fill(255), 10, 10).every((l) => l === 3));
});

test('photos: packing is 2 bits a pixel, leftmost in the top bits, rows padded to a byte', () => {
  const lv = Uint8Array.from([3, 2, 1, 0, 3, 1, 2, 0, 0, 1, 2, 3, 3, 3]);   // 7 x 2
  const packed = pack2(lv, 7, 2);
  assert.equal(packed.length, 4);
  assert.equal(packed[0], 0b11100100); assert.equal(packed[1], 0b11011000);
  assert.deepEqual(unpack2(packed, 7, 2), lv);
});

test('photos: resampling averages, keeps flat areas flat, and crops', () => {
  const flat = resample(new Float32Array(300 * 200).fill(77), 300, 200, { x: 10.5, y: 3.25, w: 250, h: 150 }, 80, 48);
  assert.ok(flat.every((v) => Math.abs(v - 77) < 1e-3));
  const g = gradient(400, 300);
  const left = resample(g, 400, 300, { x: 0, y: 0, w: 100, h: 300 }, 20, 20), right = resample(g, 400, 300, { x: 300, y: 0, w: 100, h: 300 }, 20, 20);
  assert.ok(mean(right) > mean(left) + 60);
  const up = resample(g, 400, 300, { x: 0, y: 0, w: 40, h: 30 }, 160, 120);   // enlarging interpolates
  assert.equal(up.length, 160 * 120); assert.ok(up.every((v) => v >= 0 && v <= 255));
  assert.deepEqual(adjust(Float32Array.from([0, 128, 255]), { contrast: 100 }), Float32Array.from([0, 128, 255]));
  assert.ok(adjust(Float32Array.from([100]), { brightness: 50 })[0] > 160);
});

test('photos: crops have the screen\'s shape, stay inside the picture, zoom and move', () => {
  for (const [sw, sh] of [[4000, 3000], [1080, 1920], [800, 436], [5000, 500]]) {
    for (const [aw, ah] of [PHOTO_AREA.landscape, PHOTO_AREA.portrait]) {
      for (const o of [{}, { zoom: 2.5, cx: 0.9, cy: 0.05 }, { zoom: 1, cx: 0, cy: 1 }]) {
        const c = cropFor(sw, sh, aw, ah, o);
        assert.ok(Math.abs(c.w / c.h - aw / ah) < 1e-6);
        assert.ok(c.x >= 0 && c.y >= 0 && c.x + c.w <= sw + 1e-6 && c.y + c.h <= sh + 1e-6, JSON.stringify([sw, sh, aw, ah, o, c]));
      }
    }
  }
  const whole = cropFor(4000, 3000, 800, 436), zoomed = cropFor(4000, 3000, 800, 436, { zoom: 2 });
  assert.equal(whole.w, 4000); assert.ok(Math.abs(zoomed.w - 2000) < 1e-6);
  assert.deepEqual(fitSize(2400, 1308, 480, 756), [480, 262]);
  assert.deepEqual(workSize(6000, 4000), [2400, 1600]); assert.deepEqual(workSize(640, 480), [640, 480]);
});

test('photos: titles fit 47 bytes of UTF-8 and come from file names', () => {
  assert.equal(titleFromName('IMG_2041.JPG'), 'IMG 2041');
  const t = photoTitle('Ștefan cel Mare și Sfânt 🦊🦊🦊 la Putna, vara lui 2025');
  assert.ok(te.encode(t).length <= 47); assert.ok(t.startsWith('Ștefan'));
  assert.ok(!t.includes('�'));
  assert.equal(photoTitle('a\nb\tc'), 'a b c');
});

test('photos: the file holds the photo, the other orientation fitted, and a thumbnail', () => {
  const sw = 1200, sh = 900, grey = gradient(sw, sh);
  const r = buildPhoto({ grey, sw, sh, orient: 'landscape', title: 'Gradient', added: 1794500000 });
  const head = parsePhotoHead(r.bytes), board = parsePhoto(r.bytes);   // the app's reader and the mock board's
  assert.equal(head.title, 'Gradient'); assert.equal(board.title, 'Gradient'); assert.equal(head.added, 1794500000);
  assert.equal(head.size, r.bytes.length);
  assert.deepEqual(head.pics.map((p) => [p.kind, p.orient, p.w, p.h]), [[0, 'landscape', 800, 436], [1, 'portrait', 480, 262], [2, 'landscape', 120, 65]]);
  assert.ok(r.bytes.length <= PHOTO_LIMITS.maxBytes);
  // portrait, whole picture with bars
  const p = buildPhoto({ grey, sw, sh, orient: 'portrait', fit: true });
  const ph = parsePhotoHead(p.bytes);
  assert.deepEqual(ph.pics[0], { ...ph.pics[0], kind: 0, orient: 'portrait', w: 480, h: 360 });
  assert.equal(ph.pics[1].orient, 'landscape'); assert.ok(ph.pics[1].h <= 436 && ph.pics[1].w <= 800);
  assert.ok(Math.max(ph.pics[2].w, ph.pics[2].h) === PHOTO_THUMB);
  // the dark square is dark on the board too
  const lv = unpack2(r.bytes.subarray(PHOTO_HEAD), 800, 436);
  assert.ok(mean(Array.from(lv.subarray(436 / 2 * 800 - 800 * 60 + 300, 436 / 2 * 800 - 800 * 60 + 340))) < 0.6);
  // the areas the board reports win over the defaults; nonsense doesn't
  assert.deepEqual(photoAreas({ photos: { landscape: [800, 440], portrait: [480, 760] } }), { landscape: [800, 440], portrait: [480, 760], full: null });
  assert.deepEqual(photoAreas({ photos: { landscape: [8000, 1] } }).landscape, PHOTO_AREA.landscape);
  assert.equal(head.version, 1, 'no full-screen picture: still version 1, for boards before full screen');
});

test('photos: a board with full screen also gets the photo for the whole screen, in a version 2 file', () => {
  const areas = photoAreas({ photos: { landscape: [800, 436], portrait: [480, 756], full: { landscape: [800, 480], portrait: [480, 800] } } });
  assert.deepEqual(areas.full, { landscape: [800, 480], portrait: [480, 800] });
  assert.equal(photoAreas({ photos: { full: { landscape: [800, 480] } } }).full, null, 'half a full is none');
  const sw = 1200, sh = 900, grey = gradient(sw, sh);
  for (const orient of ['landscape', 'portrait']) {
    const r = buildPhoto({ grey, sw, sh, orient, title: 'Full', added: 1794500000, areas });
    const head = parsePhotoHead(r.bytes), board = parsePhoto(r.bytes);
    assert.equal(head.version, 2); assert.equal(board.pics.length, 4);
    const full = head.pics.find((p) => p.kind === PHOTO_KIND.full);
    assert.deepEqual([full.orient, full.w, full.h], orient === 'landscape' ? ['landscape', 800, 480] : ['portrait', 480, 800]);
    assert.deepEqual(head.pics.slice(0, 3).map((p) => p.kind), [0, 1, 2], 'the first three as before');
    assert.ok(r.bytes.length <= PHOTO_LIMITS.maxBytes, `${orient}: ${r.bytes.length} bytes`);
    assert.ok(r.bytes.length > 200000, 'larger than the old limit: why it went up');
  }
  // the full-screen crop holds what was framed, and has the screen's shape
  const c = cropFor(sw, sh, 800, 436, { zoom: 1.5, cx: 0.3, cy: 0.6 }), f = fullCrop(c, sw, sh, 800, 480);
  assert.ok(Math.abs(f.w / f.h - 800 / 480) < 1e-9);
  assert.ok(f.x <= c.x + 1e-9 && f.y <= c.y + 1e-9 && f.x + f.w >= c.x + c.w - 1e-9 && f.y + f.h >= c.y + c.h - 1e-9);
  assert.ok(Math.abs(f.w - c.w) < 1e-9, 'same width: the same scale, more above and below');
  // the picture ends: cut the other way, still inside it
  const pano = fullCrop({ x: 0, y: 0, w: 2000, h: 500 }, 2000, 500, 800, 480);
  assert.ok(pano.h <= 500 && pano.x >= 0 && pano.x + pano.w <= 2000 && Math.abs(pano.w / pano.h - 800 / 480) < 1e-9);
  // fit: the whole picture, fitted to the screen
  const fit = parsePhotoHead(buildPhoto({ grey, sw, sh, orient: 'portrait', fit: true, areas }).bytes).pics.find((p) => p.kind === 3);
  assert.deepEqual([fit.w, fit.h], [480, 360]);
  // a board that takes less leaves it out (and the file stays version 1)
  const small = buildPhoto({ grey, sw, sh, orient: 'landscape', areas, maxBytes: 200000 });
  assert.equal(parsePhotoHead(small.bytes).version, 1); assert.equal(parsePhotoHead(small.bytes).pics.length, 3);
  // a board before full screen refuses kind 3 in a version 1 file
  const bad = buildPhoto({ grey, sw, sh, orient: 'landscape', areas }).bytes.slice(); bad[4] = 1;
  assert.throws(() => parsePhoto(bad), /damaged/);
});

function photoBytes(seed = 1, orient = 'landscape', areas = PHOTO_AREA) {
  const g = gradient(640, 480); for (let i = 0; i < g.length; i += 97) g[i] = (g[i] + seed * 37) % 256;
  return buildPhoto({ grey: g, sw: 640, sh: 480, orient, title: `Photo ${seed}`, added: 1794500000 + seed, areas }).bytes;
}

test('photos: upload over USB, list, thumbnail, the same photo again, delete', async () => {
  const { device, t } = serialRig({ resetOnOpen: false });
  await t.choose(); await t.connect();
  const logs = [];
  const c = new Client(t, { log: (d, x) => logs.push([d, x]) });
  const before = (await c.request('info')).info;
  assert.equal(before.photos.v, 2); assert.equal(before.photos.count, 0);
  assert.deepEqual(photoAreas(before).full, { landscape: [800, 480], portrait: [480, 800] });
  const bytes = photoBytes(1, 'landscape', photoAreas(before)), steps = [];   // with the full-screen picture, as the page sends it
  assert.equal(parsePhotoHead(bytes).version, 2);
  const r = await uploadPhoto(c, bytes, { onProgress: (at, n) => steps.push([at, n]) });
  assert.equal(r.exists, false); assert.equal(r.id, (await sha256Hex(bytes)).slice(0, 8));
  assert.equal(r.photo.title, 'Photo 1'); assert.equal(r.photo.w, 800);
  assert.deepEqual(steps.at(-1), [bytes.length, bytes.length]); assert.ok(steps.length >= Math.ceil(bytes.length / 3072));
  assert.ok(r.storage.used > before.storage.used);
  assert.ok(!logs.some(([, x]) => x.includes('photo_chunk')), 'chunks stay out of the activity log');
  const again = await uploadPhoto(c, bytes);
  assert.equal(again.exists, true); assert.equal(device.photos.size, 1);
  const list = await c.request('photo_list');
  assert.deepEqual(list.photos.map((p) => p.id), [r.id]);
  const th = await c.request('photo_thumb', { photo: r.id });
  const want = parsePhotoHead(bytes).pics.find((p) => p.kind === PHOTO_KIND.thumb);
  assert.deepEqual(fromBase64(th.data), bytes.subarray(want.off, want.off + want.len));
  await c.request('photo_delete', { photo: r.id });
  assert.equal((await c.request('photo_list')).photos.length, 0);
  await assert.rejects(c.request('photo_delete', { photo: r.id }), /no photo/);
  await t.close();
});

test('photos: upload over Bluetooth with 20-byte frames', async () => {
  const { device, client } = rig({ mtu: 23 });
  const t = client().t;
  await t.choose(); await t.connect();
  const c = new Client(t);
  const r = await uploadPhoto(c, photoBytes(2, 'portrait'));
  assert.equal(r.photo.orient, 'portrait'); assert.equal(device.photos.size, 1);
  t.close();
});

test('photos: an aborted upload leaves nothing; so do a restart and a damaged one', async () => {
  const { device, t } = serialRig({ resetOnOpen: false });
  await t.choose(); await t.connect();
  const c = new Client(t);
  const ctl = new AbortController();
  await assert.rejects(uploadPhoto(c, photoBytes(3), { signal: ctl.signal, onProgress: (at) => { if (at > 10000) ctl.abort(); } }), { name: 'AbortError' });
  assert.equal(device.upload, null); assert.equal(device.photos.size, 0); assert.equal(device.lastDrop, 'aborted');
  // cut short by a restart: the board drops the part (the firmware deletes the .part at boot)
  await c.request('photo_begin', { size: 5000, sha256: 'ab'.repeat(32) });
  await c.request('photo_chunk', { offset: 0, data: toBase64(new Uint8Array(3000)) });
  device.reboot('sw'); await sleep(device.bootMs + 50);
  await assert.rejects(c.request('photo_chunk', { offset: 3000, data: toBase64(new Uint8Array(2000)) }), /no upload on/);
  // all the bytes, but not the ones promised
  const good = photoBytes(4);
  await c.request('photo_begin', { size: good.length, sha256: 'cd'.repeat(32) });
  for (let at = 0; at < good.length; at += 3072) await c.request('photo_chunk', { offset: at, data: toBase64(good.subarray(at, at + 3072)) });
  await assert.rejects(c.request('photo_end'), /damaged/);
  // the right hash of something that isn't a photo file
  const junk = new Uint8Array(2000).fill(7);
  await assert.rejects(uploadPhoto(c, junk), /Not a photo this board can show/);
  assert.equal(device.photos.size, 0); assert.equal(device.upload, null);
  await t.close();
});

test('photos: a lost answer to a chunk: the upload goes on from where the board is', async () => {
  const { device, t, serial } = serialRig({ resetOnOpen: false });
  await t.choose(); await t.connect();
  const c = new Client(t);
  const orig = device.handle.bind(device);
  let dropped = 0;
  device.handle = async (r, via) => {
    const out = await orig(r, via);
    if (r.op === 'photo_chunk' && r.offset === 6144 && !dropped++) return new Promise(() => {});   // done, but the answer never comes
    return out;
  };
  const origTimeout = Client.prototype.request;
  c.request = (op, args, o = {}) => origTimeout.call(c, op, args, op === 'photo_chunk' ? { ...o, timeout: 150 } : o);
  const bytes = photoBytes(5);
  const r = await uploadPhoto(c, bytes);
  assert.equal(dropped, 1); assert.equal(r.photo.bytes, bytes.length);
  assert.deepEqual(device.photos.get(r.id).bytes, bytes);
  await t.close();
});

test('photos: bad requests, the 100-photo cap and a full board are refused with a reason', async () => {
  const device = new MockDevice({ bootMs: 50 });
  const ask = (r) => device.handle(r);
  await assert.rejects(ask({ op: 'photo_chunk', offset: 0, data: 'AAAA' }), /no upload on/);
  await assert.rejects(ask({ op: 'photo_end' }), /no upload on/);
  await assert.rejects(ask({ op: 'photo_begin', size: 1000, sha256: 'xyz' }), /64 hex/);
  await assert.rejects(ask({ op: 'photo_begin', size: 10, sha256: 'ab'.repeat(32) }), /bytes/);
  await assert.rejects(ask({ op: 'photo_begin', size: 300000, sha256: 'ab'.repeat(32) }), /bytes/);
  await ask({ op: 'photo_begin', size: 1000, sha256: 'ab'.repeat(32) });
  await assert.rejects(ask({ op: 'photo_chunk', offset: 10, data: 'AAAA' }), (e) => e.extra.next === 0);
  await assert.rejects(ask({ op: 'photo_chunk', offset: 0, data: '!!!' }), /base64/);
  await assert.rejects(ask({ op: 'photo_chunk', offset: 0, data: toBase64(new Uint8Array(4000)) }), /base64 of 1 to 3072/);
  await ask({ op: 'photo_abort' });
  device.photoTotal = 300 * 1024;   // a nearly full board
  await assert.rejects(ask({ op: 'photo_begin', size: 120000, sha256: 'ef'.repeat(32) }), /Not enough room/);
  device.photoTotal = PHOTO_LIMITS.total;
  for (let i = 0; i < PHOTO_LIMITS.max; i++) {
    const id = i.toString(16).padStart(8, '0');
    device.photos.set(id, { id, bytes: new Uint8Array(1), entry: { id, added: i } });
  }
  await assert.rejects(ask({ op: 'photo_begin', size: 1000, sha256: '12'.repeat(32) }), /100 photos at most/);
  await assert.rejects(ask({ op: 'photo_thumb', photo: 'ffffffff' }), /no photo/);
});

// ------------------------------------------------------------- the files
test('published files hold no private data and point at real files', async () => {
  const { readFile } = await import('node:fs/promises');
  const here = new URL('../src/setup/', import.meta.url);
  const files = ['index.html', 'app.js', 'mock.js', 'setup.css'];
  for (const f of files) {
    const s = await readFile(new URL(f, here), 'utf8');
    assert.ok(!/192\.168\.\d+\.\d+|100\.\d+\.\d+\.\d+|10\.\d+\.\d+\.\d+|\.ts\.net\b/i.test(s), `${f}: a private address`);
  }
  const html = await readFile(new URL('index.html', here), 'utf8');
  assert.match(html, /data-product="\{\{NAME\}\}"/);
  for (const [, ref] of html.matchAll(/(?:href|src)="([^"#?:]+)"/g)) {   // throws if a file is missing
    await readFile(new URL(ref.endsWith('/') ? ref + 'index.html' : ref, here));
  }
});

test('site: links to other websites open in a new tab, without opener or referrer', async () => {
  const { readFile } = await import('node:fs/promises');
  for (const f of ['index.html', 'setup/index.html', ...BLOG_PAGES]) {
    const html = await readFile(new URL('../src/' + f, import.meta.url), 'utf8');
    for (const [tag] of html.matchAll(/<a\s[^>]*href="https?:\/\/[^"]*"[^>]*>/g)) {
      assert.match(tag, /target="_blank"/, `${f}: ${tag}`);
      assert.match(tag, /rel="[^"]*noopener[^"]*noreferrer|rel="[^"]*noreferrer[^"]*noopener/, `${f}: ${tag}`);
    }
  }
});

const BLOG_PAGES = ['blog/index.html', 'blog/introducing-sticky-os/index.html'];

test('blog: pages pass the CSP, point at real files, and say the board is not Seeed\'s', async () => {
  const { readFile } = await import('node:fs/promises');
  for (const f of BLOG_PAGES) {
    const page = new URL('../src/' + f, import.meta.url);
    const html = await readFile(page, 'utf8');
    assert.ok(!/<script(?![^>]*\ssrc=)[^>]*>/i.test(html), `${f}: an inline script`);
    assert.ok(!/<style[\s>]|\sstyle="|\son[a-z]+="/i.test(html), `${f}: inline style or an event handler`);
    assert.ok(!/192\.168\.\d+\.\d+|100\.\d+\.\d+\.\d+|\.ts\.net\b|relay|mac studio/i.test(html), `${f}: private detail`);
    assert.match(html, /Not made by or affiliated with Seeed Studio\./, `${f}: the disclaimer`);
    for (const [, ref] of html.matchAll(/(?:href|src)="([^"#?:]+)"/g)) {   // throws if a file is missing
      await readFile(new URL(ref.endsWith('/') ? ref + 'index.html' : ref, page));
    }
  }
  const post = await readFile(new URL('../src/blog/introducing-sticky-os/index.html', import.meta.url), 'utf8');
  assert.match(post, /\{\{NAME\}\} is an independent, open-source firmware for the Seeed reTerminal Sticky\. Not made by or affiliated with Seeed Studio\./);
  const og = post.match(/<meta property="og:image" content="https:\/\/sticky-os\.space\.fast\/(shots\/[^"]+\.png)">/);
  assert.ok(og, 'the post: an og:image on the site');
  await readFile(new URL('../src/' + og[1], import.meta.url));   // the picture is published with the site
  const home = await readFile(new URL('../src/index.html', import.meta.url), 'utf8');
  assert.match(home, /href="blog\/"/, 'index.html: a Blog link');
});

// ------------------------------------------------------------------ run
for (const [name, fn] of tests) {
  try { await fn(); passed++; console.log(`ok   ${name}`); }
  catch (e) { console.log(`FAIL ${name}\n     ${e.stack.split('\n').slice(0, 3).join('\n     ')}`); process.exitCode = 1; }
}
console.log(`\n${passed}/${tests.length} passed`);
