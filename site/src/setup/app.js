// The setup app for the reTerminal Sticky firmware: Web Serial (the USB-C
// cable, a WCH CH343 bridge) and Web Bluetooth (the board's setup mode).
//
// Adapted from the author's earlier e-paper project and speaking its
// wire protocol, version 1, so one protocol serves both boards.
//
// One module, no dependencies. The top half (protocol: framing, request
// client, the two transports, key table, time zones) is plain JS with no DOM
// access, so ../../test/run.mjs drives it from Node against mock.js. The
// bottom half is the UI and only runs in a browser.
//
// Protocol (see docs/device-provisioning-webapp.md):
//   info (read)   JSON {id, fw, commit, board, touch, wifi:{ssid,connected,rssi}, keys:[names]}
//   rx   (write)  request frames
//   tx   (notify) response frames
// Every write/notification is one frame: 1 header byte (bit7 = last
// fragment, bits0-6 = sequence number mod 128, 0 for each message's first
// fragment) + payload. A message's payloads concatenate into one UTF-8 JSON.

// ---------------------------------------------------------------- constants

// The one place the GATT UUIDs live. Must match the firmware.
export const UUIDS = {
  service: 'edb50001-3985-44d0-a3dd-fd2767d3a6dd',
  info:    'edb50002-3985-44d0-a3dd-fd2767d3a6dd',
  rx:      'edb50003-3985-44d0-a3dd-fd2767d3a6dd',
  tx:      'edb50004-3985-44d0-a3dd-fd2767d3a6dd',
};

// The name the board advertises in setup mode starts with this (second
// chooser filter, in case the 128-bit service UUID isn't in the advertising
// packet). The service UUID is the reliable filter.
export const NAME_PREFIX = 'Sticky';

// The protocol version this page speaks (info.proto).
export const PROTOCOL = 1;

// How long Bluetooth waits for pairing (someone reading a code and typing it).
export const PAIRING_WAIT_MS = 120000;

// Bytes per write, header included. Web Bluetooth doesn't expose the ATT
// MTU: try a large frame (fits once the OS has negotiated MTU >= 185, which
// macOS and Windows normally do), and drop to 20 bytes (the default MTU 23
// minus 3) after the first failed write.
export const FRAME_LARGE = 181;   // 1 header + 180 payload
export const FRAME_SMALL = 20;    // 1 header + 19 payload

// Per-op response timeouts (ms). The device answers a scan or a test join
// only when it's done.
export const TIMEOUTS = { default: 8000, wifi_scan: 20000, wifi_test: 35000, commit: 10000, factory_reset: 10000 };

// ------------------------------------------------------------------ framing

const enc = new TextEncoder();
const dec = new TextDecoder('utf-8', { fatal: true });

// Split one message (string or object) into frames of at most frameSize bytes.
export function encodeFrames(msg, frameSize = FRAME_LARGE) {
  if (frameSize < 2) throw new RangeError('frame size must be >= 2');
  const bytes = enc.encode(typeof msg === 'string' ? msg : JSON.stringify(msg));
  const per = frameSize - 1;
  const n = Math.max(1, Math.ceil(bytes.length / per));
  const frames = [];
  for (let i = 0; i < n; i++) {
    const chunk = bytes.subarray(i * per, (i + 1) * per);
    const f = new Uint8Array(chunk.length + 1);
    f[0] = (i % 128) | (i === n - 1 ? 0x80 : 0);
    f.set(chunk, 1);
    frames.push(f);
  }
  return frames;
}

// Collects frames into messages. Lenient on purpose: a sequence gap drops the
// partial message (reported through onError) instead of wedging the link.
export class Reassembler {
  constructor({ maxBytes = 65536, onError = () => {} } = {}) {
    this.maxBytes = maxBytes; this.onError = onError; this.reset();
  }
  reset() { this.parts = []; this.len = 0; this.next = 0; }
  // Returns the completed message string, or null while more frames are due.
  push(frame) {
    if (!frame || frame.length < 1) { this.onError('empty frame'); return null; }
    const last = (frame[0] & 0x80) !== 0, seq = frame[0] & 0x7f;
    // Mid-message, the next seq is expected (127 wraps to 0). A 0 where
    // something else was due starts a new message (the sender restarted);
    // any other mismatch is a lost frame. Between messages any seq starts
    // one, so a sender that numbers frames continuously also works.
    if (this.parts.length && seq !== this.next) {
      if (seq === 0) { this.onError('new message before the last one ended; dropped the old one'); this.reset(); }
      else { this.onError(`sequence gap (got ${seq}, expected ${this.next}); dropped the message`); this.reset(); return null; }
    }
    const payload = frame.slice(1);
    this.len += payload.length;
    if (this.len > this.maxBytes) { this.onError('message too long; dropped it'); this.reset(); return null; }
    this.parts.push(payload);
    this.next = (seq + 1) % 128;
    if (!last) return null;
    const all = new Uint8Array(this.len);
    let o = 0; for (const p of this.parts) { all.set(p, o); o += p.length; }
    this.reset();
    try { return dec.decode(all); } catch { this.onError('message is not valid UTF-8'); return null; }
  }
}

// ------------------------------------------------------------------- client

export class DeviceError extends Error {
  constructor(message, response) { super(message); this.name = 'DeviceError'; this.response = response; }
}

// Two kinds of transport sit under the client:
//  - frame transports (Bluetooth): {write(Uint8Array), onNotify(cb), onDisconnect(cb)};
//    the client wraps them in a FramedLink, which does the fragmenting.
//  - message transports (USB serial): {sendText(json), onText(cb), onDisconnect(cb)};
//    one JSON message per line, nothing to fragment.
// Every transport also has: kind, name, connected, choose(), connect(),
// readInfo(), close(), and optionally reconnectAfterRestart().

// Fragmenting for a frame transport, with the large → small write fallback.
export class FramedLink {
  constructor(transport, log = () => {}) {
    this.t = transport; this.log = log; this.frameSize = FRAME_LARGE; this._text = () => {};
    this.rx = new Reassembler({ onError: (e) => this.log('warn', `device → app: ${e}`) });
    transport.onNotify((frame) => { const t = this.rx.push(frame); if (t != null) this._text(t); });
  }
  onText(cb) { this._text = cb; }
  onDisconnect(cb) { this.t.onDisconnect(() => { this.rx.reset(); cb(); }); }
  async sendText(text) {
    for (;;) {
      const frames = encodeFrames(text, this.frameSize);
      try {
        for (const f of frames) await this.t.write(f);
        return;
      } catch (e) {
        if (this.frameSize > FRAME_SMALL && !this.t.isGone?.()) {
          this.log('warn', `write of ${this.frameSize} bytes failed (${e.message}); using ${FRAME_SMALL}-byte frames from now on`);
          this.frameSize = FRAME_SMALL;
          continue;   // resend from the first frame: its seq 0 restarts the device's buffer
        }
        throw e;
      }
    }
  }
}

// Requests over any transport. One request in flight at a time, so
// fragments of two requests never interleave and the device needs only one
// reassembly buffer.
export class Client {
  constructor(transport, { log = () => {}, redact = (r) => r, onDisconnect = () => {} } = {}) {
    this.t = transport; this.log = log; this.redact = redact;
    this.link = typeof transport.sendText === 'function' ? transport : new FramedLink(transport, log);
    this.nextId = 1;
    this.pending = new Map(); this.chain = Promise.resolve();
    this.link.onText((text) => this._onText(text));
    this.link.onDisconnect(() => { this._failAll(new DeviceError('Disconnected')); onDisconnect(); });
  }

  get frameSize() { return this.link.frameSize; }
  set frameSize(n) { this.link.frameSize = n; }

  // quiet: not in the activity log (a photo's chunks: dozens a photo).
  request(op, args = {}, { timeout, quiet = false } = {}) {
    const run = () => this._send(op, args, timeout ?? TIMEOUTS[op] ?? TIMEOUTS.default, quiet);
    const p = this.chain.then(run, run);
    this.chain = p.catch(() => {});
    return p;
  }

  async _send(op, args, timeout, quiet) {
    const id = this.nextId++; if (this.nextId > 1e9) this.nextId = 1;
    const req = { id, op, ...args };
    if (!quiet) this.log('out', JSON.stringify(shortData(this.redact(req))));
    const done = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new DeviceError(`No answer from the device to "${op}" within ${Math.round(timeout / 1000)} s`));
      }, timeout);
      this.pending.set(id, { resolve, reject, timer, op, quiet });
    });
    try {
      await this.link.sendText(JSON.stringify(req));
    } catch (e) {
      const p = this.pending.get(id);
      if (p) { clearTimeout(p.timer); this.pending.delete(id); }
      throw e;
    }
    return done;
  }

  _onText(text) {
    let msg;
    try { msg = JSON.parse(text); } catch { this.log('warn', `device sent text that isn't JSON: ${text.slice(0, 120)}`); return; }
    const p = this.pending.get(msg.id);
    if (!p?.quiet || msg.ok === false) this.log('in', JSON.stringify(shortData(msg)));
    if (msg.event) return;  // the device's own notices ("paired"), not answers
    if (!p) { this.log('warn', `unrequested message from the device (id ${msg.id})`); return; }
    clearTimeout(p.timer); this.pending.delete(msg.id);
    if (msg.ok === false) p.reject(new DeviceError(msg.error || `"${p.op}" failed`, msg));
    else p.resolve(msg);
  }

  _failAll(err) {
    for (const [, p] of this.pending) { clearTimeout(p.timer); p.reject(err); }
    this.pending.clear();
  }
}

// A photo's bytes (base64 "data") as their length, for the log.
function shortData(m) { return m && typeof m.data === 'string' && m.data.length > 64 ? { ...m, data: `<${m.data.length} base64 characters>` } : m; }

// Replace every secret in a request with dots before it reaches the log.
// `isSecret(key)` comes from the device's key list.
export function redactRequest(req, isSecret = () => false) {
  const r = { ...req };
  if (r.op === 'set' && r.key && (isSecret(r.key) || r.key === 'wifi.networks')) {
    r.value = r.key === 'wifi.networks' && Array.isArray(r.value)
      ? r.value.map((n) => ('password' in n ? { ...n, password: '•••' } : n))
      : '•••';
  }
  if ('password' in r) r.password = '•••';
  return r;
}

// ----------------------------------------------------- Web Bluetooth transport

export class BleTransport {
  static supported() { return typeof navigator !== 'undefined' && !!navigator.bluetooth; }

  constructor() { this.kind = 'ble'; this.device = null; this._notify = () => {}; this._disc = () => {}; this.connected = false; }
  onNotify(cb) { this._notify = cb; }
  onDisconnect(cb) { this._disc = cb; }
  isGone() { return !this.connected; }
  get name() { return this.device?.name || 'device'; }

  // Opens the browser's device chooser. Must run inside a click handler.
  async choose() {
    this.device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [UUIDS.service] }, { namePrefix: NAME_PREFIX }],
      optionalServices: [UUIDS.service],
    });
    this.device.addEventListener('gattserverdisconnected', () => { this.connected = false; this._disc(); });
  }

  // Connects (no chooser: reuses the chosen device, e.g. after a restart).
  // The first access to an encrypted characteristic is what makes the OS pair.
  async connect() {
    const server = await this.device.gatt.connect();
    this.connected = true;
    const svc = await server.getPrimaryService(UUIDS.service);
    [this.info, this.rx, this.tx] = await Promise.all(
      [UUIDS.info, UUIDS.rx, UUIDS.tx].map((u) => svc.getCharacteristic(u)));
    this.tx.addEventListener('characteristicvaluechanged', (e) => {
      const v = e.target.value;
      this._notify(new Uint8Array(v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength)));
    });
    // Nothing protected may be in flight while Android pairs (the user typing
    // the code): a read sent then hung in Chrome for good, though the pairing
    // succeeded. Notifications need no pairing, so turn them on and wait for
    // the device's {"event":"paired"} before the first protected request. A
    // phone already bonded gets it within a second or two.
    const paired = this.waitForPaired(PAIRING_WAIT_MS);
    await this.tx.startNotifications();
    await paired;
  }

  waitForPaired(ms) {
    return new Promise((resolve, reject) => {
      const onValue = (e) => {
        const v = e.target.value;
        const b = new Uint8Array(v.buffer, v.byteOffset, v.byteLength);
        if (b.length < 2 || !(b[0] & 0x80)) return;  // the notice is one whole frame
        if (new TextDecoder().decode(b.subarray(1)).includes('"event":"paired"')) { done(); resolve(); }
      };
      const timer = setTimeout(() => { done(); reject(new Error('Pairing did not finish. Enter the code shown on the device, then try again.')); }, ms);
      const done = () => { clearTimeout(timer); this.tx.removeEventListener('characteristicvaluechanged', onValue); };
      this.tx.addEventListener('characteristicvaluechanged', onValue);
    });
  }

  async readInfo() {
    const v = await this.info.readValue();
    return new TextDecoder().decode(v);
  }

  async write(frame) { await this.rx.writeValueWithResponse(frame); }

  close() { try { this.device?.gatt?.disconnect(); } catch { /* already gone */ } }
}

// After "commit" the device restarts. A transport can bring its own way
// back (serial does); otherwise wait for the link to drop and reconnect to
// the same device, without a chooser, for up to timeoutMs.
export async function reconnectAfterRestart(transport, { onStatus = () => {}, timeoutMs = 45000 } = {}) {
  if (typeof transport.reconnectAfterRestart === 'function') return transport.reconnectAfterRestart({ onStatus, timeoutMs });
  const t0 = Date.now();
  while (transport.connected && Date.now() - t0 < 8000) await sleep(250);
  await transport.close();
  let last;
  while (Date.now() - t0 < timeoutMs) {
    await sleep(2000);
    try { await transport.connect(); return; }
    catch (e) { last = e; onStatus(Math.round((Date.now() - t0) / 1000)); }
  }
  throw last || new Error('The device did not come back');
}

// ---------------------------------------------------- Web Serial transport
// The USB cable. 115200 baud; the port also carries the device's log. A
// config message is one line, "@cfg <json>\n", both ways; every other line
// is log output. No fragments, no pairing (a cable means physical access);
// secrets stay write-only as over Bluetooth.

// The Sticky's USB-C port is a WCH CH343 USB-to-UART bridge. The ESP32-S3's
// own USB (Espressif's VID) is listed too, for boards wired that way.
export const WCH_VID = 0x1a86;
export const ESPRESSIF_VID = 0x303a;
export const SERIAL_FILTERS = [{ usbVendorId: WCH_VID }, { usbVendorId: ESPRESSIF_VID }];
export const SERIAL_PREFIX = '@cfg ';
export const SERIAL_TIMEOUTS = { info: 3000, ready: 20000 };
const HELLO_ID = 0;   // the transport's own "info"; the client's ids start at 1

export class SerialTransport {
  static supported() { return typeof navigator !== 'undefined' && !!navigator.serial; }

  constructor({ serial = globalThis.navigator?.serial, baudRate = 115200, keepLines = 200, timeouts = SERIAL_TIMEOUTS } = {}) {
    this.kind = 'serial'; this.serial = serial; this.baudRate = baudRate; this.keepLines = keepLines; this.timeouts = timeouts;
    this.port = null; this.connected = false; this.info = null; this.usb = null;
    this.lines = []; this.lastReady = 0; this.readyWaiters = [];
    this._text = () => {}; this._disc = () => {}; this._log = () => {};
  }
  onText(cb) { this._text = cb; }
  onDisconnect(cb) { this._disc = cb; }
  onLog(cb) { this._log = cb; }
  isGone() { return !this.connected; }
  get name() {
    if (this.info?.id) return this.info.id;
    const u = this.usb; return u ? `USB ${hex4(u.usbVendorId)}:${hex4(u.usbProductId)}` : 'USB device';
  }

  // Opens the browser's port chooser (the CH343 bridge, or native ESP32 USB). Click handler only.
  async choose() {
    this.port = await this.serial.requestPort({ filters: SERIAL_FILTERS });
    this.usb = this.port.getInfo?.() || null;
  }

  // Opens the chosen port, or, if it was unplugged and came back as a new
  // SerialPort object, the one with the same VID:PID the browser already allows.
  async connect() {
    let last;
    for (const p of await this._candidates()) {
      try { await this._open(p); await this._hello(); return; }
      catch (e) { last = e; await this.close(); }
    }
    throw last || Object.assign(new Error('The USB device is not plugged in.'), { name: 'NotFoundError' });
  }

  async _candidates() {
    const out = this.port ? [this.port] : [];
    try {
      for (const p of await this.serial.getPorts()) {
        const u = p.getInfo?.() || {};
        if (!out.includes(p) && (!this.usb || (u.usbVendorId === this.usb.usbVendorId && u.usbProductId === this.usb.usbProductId))) out.push(p);
      }
    } catch { /* no getPorts */ }
    return out;
  }

  async _open(port) {
    this.closing = false;
    await port.open({ baudRate: this.baudRate });
    // Opening toggles DTR/RTS, which on the CH343 drive the ESP32's EN and
    // IO0 through the auto-reset circuit: the board restarts. Release both
    // straight away (both high-impedance: it runs); _hello() waits for the
    // boot banner if the open reset it.
    try { await port.setSignals({ dataTerminalReady: false, requestToSend: false }); } catch { /* not supported: fine */ }
    this.port = port; this.connected = true;
    this.writer = port.writable.getWriter();
    this._onLost = () => this._lost();
    port.addEventListener?.('disconnect', this._onLost);
    this.reading = this._readLoop(port);
  }

  async _readLoop(port) {
    const reader = port.readable.getReader();
    this.reader = reader;
    const dec = new TextDecoder();
    let buf = '';
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        buf += dec.decode(value, { stream: true });
        let i;
        while ((i = buf.indexOf('\n')) >= 0) { this._line(buf.slice(0, i).replace(/\r$/, '')); buf = buf.slice(i + 1); }
        if (buf.length > 16384) { this._line(buf); buf = ''; }   // a runaway line without a newline
      }
    } catch { /* port lost or cancelled */ }
    finally { try { reader.releaseLock(); } catch { /* gone */ } this._lost(); }
  }

  _line(line) {
    const at = line.indexOf(SERIAL_PREFIX);
    if (at === 0) {
      const json = line.slice(SERIAL_PREFIX.length);
      let msg;
      try { msg = JSON.parse(json); } catch { this._pushLog(`[unparsable @cfg] ${line}`); return; }
      if (msg && msg.ready === true) {
        this.lastReady = Date.now(); this.bootInfo = msg;
        this._pushLog(line);
        for (const w of this.readyWaiters.splice(0)) w(true);
        return;
      }
      if (msg && msg.id === HELLO_ID && this.hello) { this.hello(msg); return; }
      this._text(json);
      return;
    }
    this._pushLog(line);
  }

  _pushLog(line) {
    this.lines.push(line);
    if (this.lines.length > this.keepLines) this.lines.splice(0, this.lines.length - this.keepLines);
    this._log(line);
  }

  // "info" until the device answers. No answer within 3 s usually means
  // the open reset the board: wait for its boot banner, @cfg {"ready":true,...},
  // for up to 20 s, then ask again.
  async _hello() {
    const deadline = Date.now() + this.timeouts.info + this.timeouts.ready;
    for (;;) {
      const sentAt = Date.now();
      const r = await this._ask(this.timeouts.info);
      if (r) {
        if (r.ok === false) throw new Error(r.error || 'the device refused "info"');
        this.info = r.info || stripMeta(r);
        return;
      }
      if (Date.now() >= deadline) break;
      if (!(this.lastReady >= sentAt) && !(await this._waitReady(deadline - Date.now()))) break;
    }
    throw new Error('No answer on the USB port. Is the board on, and is its firmware new enough to have setup support? '
      + 'Close any other program using the port (a serial monitor, the Arduino IDE), then try again.');
  }

  _ask(ms) {
    return new Promise((resolve) => {
      const timer = setTimeout(() => { this.hello = null; resolve(null); }, ms);
      this.hello = (msg) => { clearTimeout(timer); this.hello = null; resolve(msg); };
      this._write(`${SERIAL_PREFIX}${JSON.stringify({ id: HELLO_ID, op: 'info' })}\n`).catch(() => {});
    });
  }

  _waitReady(ms) {
    return new Promise((resolve) => {
      const timer = setTimeout(() => { this.readyWaiters = this.readyWaiters.filter((w) => w !== done); resolve(false); }, Math.max(0, ms));
      const done = (v) => { clearTimeout(timer); resolve(v); };
      this.readyWaiters.push(done);
    });
  }

  async _write(text) {
    if (!this.connected || !this.writer) throw Object.assign(new Error('The USB port is closed.'), { name: 'NetworkError' });
    await this.writer.write(new TextEncoder().encode(text));
  }

  async sendText(json) { await this._write(`${SERIAL_PREFIX}${json}\n`); }

  async readInfo() { if (!this.info) await this._hello(); return JSON.stringify(this.info); }

  _lost() {
    if (!this.connected) return;
    this.connected = false;
    this.port?.removeEventListener?.('disconnect', this._onLost);
    for (const w of this.readyWaiters.splice(0)) w(false);
    if (!this.closing) this._disc();
  }

  async close() {
    const port = this.port;
    if (!port) return;
    this.closing = true;
    const wasOpen = this.connected;
    this.connected = false;
    try { await this.reader?.cancel(); } catch { /* gone */ }
    try { await this.reading; } catch { /* done */ }
    try { this.writer?.releaseLock(); } catch { /* gone */ }
    this.writer = null; this.reader = null;
    if (wasOpen || port.readable) { try { await port.close(); } catch { /* already closed or unplugged */ } }
  }

  // After "commit": the board restarts. Behind the CH343 bridge the port
  // stays open and the boot banner shows up on it (the bridge never
  // re-enumerates). On native USB the device drops off and comes back.
  async reconnectAfterRestart({ onStatus = () => {}, timeoutMs = 45000 } = {}) {
    const t0 = Date.now();
    const prevId = this.info?.id;
    while (this.connected && !(this.lastReady > t0) && Date.now() - t0 < this.timeouts.ready) await sleep(100);
    if (this.connected && this.lastReady > t0) { this.info = null; await this._hello(); return; }
    await this.close();
    let last;
    while (Date.now() - t0 < timeoutMs) {
      await sleep(1000);
      onStatus(Math.round((Date.now() - t0) / 1000));
      for (const p of await this._candidates()) {
        try {
          await this._open(p);
          this.info = null;
          await this._hello();
          if (prevId && this.info?.id && this.info.id !== prevId) { last = new Error(`found ${this.info.id}, not ${prevId}`); await this.close(); continue; }
          return;
        } catch (e) { last = e; await this.close(); }
      }
    }
    throw last || new Error('The device did not come back on USB');
  }
}

function hex4(n) { return n == null ? '????' : n.toString(16).padStart(4, '0'); }
function stripMeta(r) { const { id, ok, error, ...rest } = r || {}; return rest; }

// ------------------------------------------------------------ the key table
// Only labels, help, grouping and the "Advanced" fold. The board's "keys"
// answer decides which keys exist, their type and whether they're secret; a
// key missing here still shows up, labelled from its name, in its prefix's
// group (or a group of its own).

export const GROUPS = [
  { id: 'language', title: 'Language', prefixes: ['language'],
    blurb: 'The language of the board’s screens, its dates and its keyboard. The AI answers in it too (notes, recipes, Quiz and Adventure). This page stays in English for now.' },
  { id: 'wifi', title: 'WiFi', prefixes: ['wifi'],
    blurb: 'The networks the board joins, tried from the top. Passwords can be written, never read back.' },
  { id: 'device', title: 'Device', prefixes: ['device'],
    blurb: 'A name for the board, and the local time it shows.' },
  { id: 'weather', title: 'Weather', prefixes: ['weather'],
    blurb: 'Where the Weather app’s forecast is for. Forecasts come from Open-Meteo, which needs no key.' },
  { id: 'voice', title: 'Voice notes', prefixes: ['openai', 'voice', 'notes'],
    blurb: 'Notes turns what you say into tasks and notes with an AI model, using your own OpenAI key. Without a key, every other app works the same.' },
  { id: 'mqtt', title: 'MQTT broker', prefixes: ['mqtt'], optional: true,
    blurb: 'Optional. A broker you run, for the Claude usage gauge and for telemetry.' },
  { id: 'claude', title: 'Claude usage', prefixes: ['claude'], optional: true,
    blurb: 'Optional. The gauge shows numbers you publish to your MQTT broker as retained messages.' },
  { id: 'calendar', title: 'Calendar', prefixes: ['calendar', 'influx'], optional: true,
    blurb: 'Optional. The Calendar app shows your calendars: paste their secret iCal links (Google, iCloud, Outlook). The board reads them every 30 minutes, straight from your calendar service. An InfluxDB bucket you keep filled works too; both are merged.' },
  { id: 'telemetry', title: 'Telemetry', prefixes: ['telemetry'], optional: true,
    blurb: 'Optional. Readings from the sensors and the battery, sent to your own MQTT broker and nowhere else.' },
  { id: 'quotes', title: 'Quote of the Day', prefixes: ['quotes'], optional: true,
    blurb: 'The Quote app shows a new quote each day from ZenQuotes (zenquotes.io, free, no key). Off: only the board’s own list of public-domain quotes, and the board never asks ZenQuotes.' },
  { id: 'games', title: 'Games', prefixes: ['games'], optional: true,
    blurb: 'Quiz and Adventure can ask your AI model (the OpenAI key above) for a quiz on any theme and a new story each time. Off: they play the board’s own questions and story, and send nothing.' },
  { id: 'update', title: 'Software updates', prefixes: ['update'], optional: true,
    blurb: 'New versions come from the project’s GitHub releases. The board checks once a day and shows when there is one; it installs them by itself only if you turn that on.' },
];

// advanced: shown under a collapsed "Advanced" in its group.
export const KEY_INFO = {
  'wifi.networks':     { label: 'Networks' },
  'device.language':   { label: 'Language', type: 'lang', group: 'language', help: 'English, Spanish, Portuguese, French, German, Italian, Russian, Turkish, Polish or Romanian. Also in Settings → Language on the board.' },
  'device.name':       { label: 'Name', placeholder: 'e.g. Kitchen', help: 'Helps you tell boards apart.' },
  'device.timezone':   { label: 'Time zone', type: 'tz', help: 'The board gets the time from the internet. This sets the local time it shows.' },
  'weather.locate':    { label: 'Find my town automatically', help: 'Once after each start, the board asks ip-api.com which town its internet address is in. Off: it always uses the place below.' },
  'weather.place':     { label: 'Town', placeholder: 'e.g. Lisbon', help: 'The name the Weather app shows. Used until the automatic lookup answers, or always when it is off.' },
  'weather.lat':       { label: 'Latitude', placeholder: 'e.g. 38.72', inputmode: 'decimal', geo: 'lat', help: 'Two decimals are plenty: about a kilometre.' },
  'weather.lon':       { label: 'Longitude', placeholder: 'e.g. -9.14', inputmode: 'decimal', geo: 'lon' },
  'openai.api_key':    { label: 'OpenAI API key', placeholder: 'sk-…', help: 'From platform.openai.com, under API keys. What the board uses is billed to that account.' },
  'openai.relay_host': { label: 'Relay host', advanced: true, placeholder: 'e.g. homeserver.local', help: 'A relay on your own network that passes requests on. Empty: the board talks to OpenAI directly.' },
  'openai.relay_port': { label: 'Relay port', advanced: true },
  'voice.stt_local':   { label: 'Speech to text through the relay', advanced: true, help: 'Asks your relay to use its own speech-to-text engine instead of OpenAI’s. Only for a relay and a firmware build that agree on how to ask; release builds leave the choice to the relay.' },
  'notes.stt_model':   { label: 'Speech-to-text model', advanced: true, help: 'The OpenAI model that turns your voice into text. Leave it unless you want another.' },
  'notes.ai_model':    { label: 'List model', advanced: true, help: 'The OpenAI model that turns the text into tasks and notes.' },
  'mqtt.host':         { label: 'Broker host', placeholder: 'e.g. broker.local or mqtt.example.com' },
  'mqtt.port':         { label: 'Port', help: '8883 is the usual port for TLS, 1883 for plain MQTT.' },
  'mqtt.tls':          { label: 'Use TLS', help: 'Encrypts the connection. Keep it on unless the broker is plain MQTT on your own network.' },
  'mqtt.ca':           { label: 'CA certificate (PEM)', type: 'pem', advanced: true, placeholder: '-----BEGIN CERTIFICATE-----', help: 'Needed when your broker’s certificate is signed by your own certificate authority. Paste the whole text, BEGIN to END.' },
  'mqtt.user':         { label: 'Username' },
  'mqtt.password':     { label: 'Password' },
  'claude.topic':      { label: 'Topic prefix', placeholder: 'e.g. claude/limits', help: 'Where you publish the usage numbers. The board subscribes to everything under it.' },
  'calendar.ics':      { label: 'Calendar links', type: 'ics', help: 'Up to three. A label (Work, Family) is optional; the board shows it with the event. Each link is a password to that calendar: the board keeps it, and never sends it back.' },
  'influx.url':        { label: 'InfluxDB URL', advanced: true, placeholder: 'e.g. http://influx.local:8086' },
  'influx.org':        { label: 'Organisation', advanced: true },
  'influx.bucket':     { label: 'Bucket', advanced: true },
  'influx.token':      { label: 'Read-only token', advanced: true, help: 'A token that can only read this bucket is enough.' },
  'telemetry.enabled': { label: 'Send readings to my broker' },
  'quotes.online':     { label: 'Quotes from ZenQuotes', help: 'Once a day, and when you tap New quote, the board asks zenquotes.io for a quote. Without WiFi, or when ZenQuotes asks for a pause, the board’s own list stands in.' },
  'games.ai':          { label: 'AI in Quiz and Adventure', help: 'With an OpenAI key, Quiz asks the AI for eight questions on the theme you pick, and Adventure for each scene of your story. Off, or without a key or WiFi: the board’s own questions and story.' },
  'update.auto':       { label: 'Install updates automatically', help: 'Off at first. On: only when the board is idle, on USB power or above half its battery, and never while a timer runs. Off: Settings → Software update shows when there is one, and Install puts it on.' },
  'update.url':        { label: 'Release feed URL', advanced: true, placeholder: 'empty: GitHub', help: 'For a fork: the address of a releases/latest JSON like GitHub’s. Leave it empty for the official releases.' },
};

// What a first-time visitor needs, in order; shown on the overview.
export const ESSENTIALS = ['device.language', 'wifi.networks', 'device.timezone', 'weather', 'openai.api_key'];

// device.language's choices, as each calls itself (the board's i18n.h LANGS).
export const LANGUAGES = {
  en: 'English', es: 'Español', pt: 'Português', fr: 'Français', de: 'Deutsch',
  it: 'Italiano', ru: 'Русский', tr: 'Türkçe', pl: 'Polski', ro: 'Română',
};
export function languageName(code) { return LANGUAGES[code] || code; }
// The browser's language, if the board has it: what the overview suggests.
export function browserLanguage(list = globalThis.navigator?.languages || []) {
  for (const l of list) { const c = String(l).toLowerCase().split('-')[0]; if (LANGUAGES[c]) return c; }
  return 'en';
}

export function prefixOf(key) { return key.split('.')[0]; }

export function groupOf(key) {
  if (KEY_INFO[key]?.group) return KEY_INFO[key].group;
  const p = prefixOf(key);
  return GROUPS.find((g) => g.prefixes.includes(p))?.id || p;
}

export function humanize(s) {
  const t = s.replace(/[_-]+/g, ' ').replace(/([a-z])([A-Z])/g, '$1 $2').trim();
  return t.charAt(0).toUpperCase() + t.slice(1);
}

export function keyLabel(k) { return KEY_INFO[k.key]?.label || k.label || humanize(k.key.split('.').slice(1).join(' ') || k.key); }

export function isAdvanced(k) { return !!KEY_INFO[k.key]?.advanced; }

// The groups to show, in order: the known ones that have keys, then the rest
// alphabetically. Within a group, the device's order.
export function groupKeys(keys) {
  const by = new Map();
  for (const k of keys) { const g = groupOf(k.key); if (!by.has(g)) by.set(g, []); by.get(g).push(k); }
  const out = [];
  for (const g of GROUPS) if (by.has(g.id)) { out.push({ ...g, keys: by.get(g.id) }); by.delete(g.id); }
  for (const id of [...by.keys()].sort()) out.push({ id, title: humanize(id), blurb: '', optional: true, keys: by.get(id) });
  return out;
}

// calendar.ics: up to ICS_MAX links, one a line, "label https://..." or just
// the address. webcal:// is the same address over https.
export const ICS_MAX = 3;
export function icsSetValue(rows) {
  const lines = [];
  for (const [i, r] of (rows || []).entries()) {
    let url = String(r?.url ?? '').trim();
    const label = String(r?.label ?? '').trim().replace(/\s+/g, ' ');
    if (!url) { if (label) throw new Error(`Calendar ${i + 1}: paste its link, or clear the label.`); continue; }
    url = url.replace(/^webcals?:\/\//i, 'https://');
    if (!/^https:\/\//i.test(url)) throw new Error(`Calendar ${i + 1}: a calendar link starts with https:// or webcal://.`);
    if (/\s/.test(url)) throw new Error(`Calendar ${i + 1}: a link has no spaces.`);
    if (url.length > 400) throw new Error(`Calendar ${i + 1}: that link is too long (400 characters at most).`);
    if (new TextEncoder().encode(label).length > 24) throw new Error(`Calendar ${i + 1}: a label is 24 characters at most.`);
    lines.push(label ? `${label} ${url}` : url);
  }
  if (lines.length > ICS_MAX) throw new Error(`At most ${ICS_MAX} calendars.`);
  return lines.join('\n');
}
// What the board says about the links it has, never their addresses: the labels.
export function icsLabels(k) { return Array.isArray(k?.labels) ? k.labels.map((l) => (typeof l === 'string' ? l : '')) : []; }

export function isWifiList(k) { return k.key === 'wifi.networks' || ['wifi_list', 'networks'].includes(k.type); }

// The saved networks in a "keys" entry, without passwords. Firmware may send
// value: [{ssid}] (a public key), or ssids: ["…"] (wifi.networks marked
// secret, as this firmware does). Plain strings in value are accepted too.
export function wifiFromKey(k) {
  const src = Array.isArray(k?.value) ? k.value : Array.isArray(k?.ssids) ? k.ssids : [];
  return src.map((n) => (typeof n === 'string' ? n : n?.ssid)).filter((s) => typeof s === 'string' && s.length);
}

// The "set" value for wifi.networks: the whole list in order. An entry
// without "password" keeps the password the board has stored for that SSID;
// "" is an open network.
export function wifiSetValue(list) {
  return list.map((n) => (n.password === undefined ? { ssid: n.ssid } : { ssid: n.ssid, password: n.password }));
}

export function normalizeKey(k) {
  if (typeof k === 'string') k = { key: k };
  return { type: 'string', secret: false, set: false, ...k, key: k.key ?? k.name };
}

// --------------------------------------------------------------- time zones
// The board takes a POSIX TZ string (what setenv("TZ") wants); people pick
// a city. IANA name → POSIX rule, from the tz database's current rules.

export const TIMEZONES = [
  ['UTC', 'UTC0'],
  ['Europe/London', 'GMT0BST,M3.5.0/1,M10.5.0'],
  ['Europe/Dublin', 'IST-1GMT0,M10.5.0,M3.5.0/1'],
  ['Europe/Lisbon', 'WET0WEST,M3.5.0/1,M10.5.0'],
  ['Atlantic/Reykjavik', 'GMT0'],
  ['Europe/Amsterdam', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Berlin', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Brussels', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Budapest', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Copenhagen', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Madrid', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Oslo', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Paris', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Prague', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Rome', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Stockholm', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Vienna', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Warsaw', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Zurich', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Athens', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Bucharest', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Chisinau', 'EET-2EEST,M3.5.0,M10.5.0/3'],
  ['Europe/Helsinki', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Kyiv', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Riga', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Sofia', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Tallinn', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Vilnius', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Istanbul', '<+03>-3'],
  ['Europe/Moscow', 'MSK-3'],
  ['Africa/Cairo', 'EET-2EEST,M4.5.5/0,M10.5.4/24'],
  ['Africa/Johannesburg', 'SAST-2'],
  ['Africa/Lagos', 'WAT-1'],
  ['Africa/Nairobi', 'EAT-3'],
  ['Asia/Dubai', '<+04>-4'],
  ['Asia/Tehran', '<+0330>-3:30'],
  ['Asia/Jerusalem', 'IST-2IDT,M3.4.4/26,M10.5.0'],
  ['Asia/Karachi', 'PKT-5'],
  ['Asia/Kolkata', 'IST-5:30'],
  ['Asia/Dhaka', '<+06>-6'],
  ['Asia/Bangkok', '<+07>-7'],
  ['Asia/Jakarta', 'WIB-7'],
  ['Asia/Singapore', '<+08>-8'],
  ['Asia/Shanghai', 'CST-8'],
  ['Asia/Hong_Kong', 'HKT-8'],
  ['Asia/Taipei', 'CST-8'],
  ['Asia/Manila', 'PST-8'],
  ['Asia/Seoul', 'KST-9'],
  ['Asia/Tokyo', 'JST-9'],
  ['Australia/Perth', 'AWST-8'],
  ['Australia/Darwin', 'ACST-9:30'],
  ['Australia/Adelaide', 'ACST-9:30ACDT,M10.1.0,M4.1.0/3'],
  ['Australia/Brisbane', 'AEST-10'],
  ['Australia/Melbourne', 'AEST-10AEDT,M10.1.0,M4.1.0/3'],
  ['Australia/Sydney', 'AEST-10AEDT,M10.1.0,M4.1.0/3'],
  ['Pacific/Auckland', 'NZST-12NZDT,M9.5.0,M4.1.0/3'],
  ['Pacific/Honolulu', 'HST10'],
  ['America/Anchorage', 'AKST9AKDT,M3.2.0,M11.1.0'],
  ['America/Los_Angeles', 'PST8PDT,M3.2.0,M11.1.0'],
  ['America/Vancouver', 'PST8PDT,M3.2.0,M11.1.0'],
  ['America/Denver', 'MST7MDT,M3.2.0,M11.1.0'],
  ['America/Phoenix', 'MST7'],
  ['America/Chicago', 'CST6CDT,M3.2.0,M11.1.0'],
  ['America/Mexico_City', 'CST6'],
  ['America/New_York', 'EST5EDT,M3.2.0,M11.1.0'],
  ['America/Toronto', 'EST5EDT,M3.2.0,M11.1.0'],
  ['America/Bogota', '<-05>5'],
  ['America/Lima', '<-05>5'],
  ['America/Halifax', 'AST4ADT,M3.2.0,M11.1.0'],
  ['America/Santiago', '<-04>4<-03>,M9.1.6/24,M4.1.6/24'],
  ['America/Sao_Paulo', '<-03>3'],
  ['America/Argentina/Buenos_Aires', '<-03>3'],
  ['America/St_Johns', 'NST3:30NDT,M3.2.0,M11.1.0'],
];

// Several cities share one rule; `prefer` (a zone name) wins when it matches.
export function zoneForPosix(posix, prefer) {
  if (prefer && TIMEZONES.some(([n, p]) => n === prefer && p === posix)) return prefer;
  return TIMEZONES.find(([, p]) => p === posix)?.[0] || null;
}

export function cityName(zone) { return (zone.split('/').pop() || zone).replace(/_/g, ' '); }

export function looksLikePosixTz(v) { return /^(<[+-]?[\w]+>|[A-Za-z]{3,})[+-]?\d/.test(v); }

// What the board answered to commit: does it restart? The firmware
// answers {changed:false} without restarting when nothing was staged.
export function commitRestarts(r) { return !(r && (r.restart === false || r.changed === false)); }

// ------------------------------------------------------------------ photos
// The board's Photos app shows photos sent from here (info.photos, v1; v2 adds full screen).
// All the image work happens in this page, so the board does no decoding:
// each photo is cropped to the board's picture area, scaled, adjusted and
// dithered to its four greys (Floyd-Steinberg), then packed 2 bits a pixel
// into one file with a small header (the format is in the firmware's
// photostore.h). A file holds three pictures, four for a board with full screen:
//   0  the photo as cropped, for the orientation chosen (fills the screen)
//   1  the same crop, whole, fitted to the other orientation (white bars),
//      so the board shows it however it is held
//   2  a thumbnail, for the list on this page (photo_thumb)
//   3  the photo for full screen (a double tap on the board): the whole
//      screen held the orientation chosen, the crop grown to its shape
//      around the same middle. Only for a board whose info lists
//      photos.full; such a file is version 2, the rest stay version 1.
// The file's SHA-256 names it on the board: the same photo twice is one file.

export const PHOTO_VERSION = 1;
export const PHOTO_VERSION_FULL = 2;      // a file with a full-screen picture
export const PHOTO_HEAD = 96;
export const PHOTO_TITLE_BYTES = 47;
export const PHOTO_THUMB = 120;           // a thumbnail's long side
export const PHOTO_CHUNK = 3072;          // bytes per photo_chunk, before base64 (the board may ask for less)
// Where the Photos app draws, under the status bar, when info doesn't say.
export const PHOTO_AREA = { landscape: [800, 436], portrait: [480, 756] };
export const PHOTO_KIND = { photo: 0, other: 1, thumb: 2, full: 3 };
export const PHOTO_WORK_MAX = 2400;       // the page works on a copy at most this long a side (the board's area is 800 at most)

// The working copy's size for a picture of w x h.
export function workSize(w, h) { const s = Math.min(1, PHOTO_WORK_MAX / Math.max(w, h)); return [Math.max(1, Math.round(w * s)), Math.max(1, Math.round(h * s))]; }
// The four greys as the page shows them: the panel's black, two greys and its paper.
export const PANEL_GREYS = [[0x1f, 0x20, 0x23], [0x5c, 0x5e, 0x61], [0xa4, 0xa5, 0xa0], [0xe9, 0xe9, 0xe3]];

// The areas the board draws photos in; `full` (the whole screen, each way)
// only when the board has full screen, else null.
export function photoAreas(info) {
  const p = info?.photos || {};
  const ok = (a) => Array.isArray(a) && a.length === 2 && a.every((n) => Number.isInteger(n) && n > 0 && n <= 800);
  const full = p.full && ok(p.full.landscape) && ok(p.full.portrait) ? { landscape: p.full.landscape, portrait: p.full.portrait } : null;
  return { landscape: ok(p.landscape) ? p.landscape : PHOTO_AREA.landscape, portrait: ok(p.portrait) ? p.portrait : PHOTO_AREA.portrait, full };
}

// RGBA bytes to grey, 0..255, on white where transparent. The same weights
// as the board's JPEG path (77, 150, 29 / 256).
export function toGrey(rgba, w, h) {
  const g = new Float32Array(w * h);
  for (let i = 0, j = 0; i < g.length; i++, j += 4) {
    const a = rgba[j + 3] / 255;
    const y = (rgba[j] * 77 + rgba[j + 1] * 150 + rgba[j + 2] * 29) / 256;
    g[i] = y * a + 255 * (1 - a);
  }
  return g;
}

// For each of n output samples over [start, start + len) of a source of
// srcLen samples: the source indices and weights. Shrinking averages the
// area each output pixel covers; enlarging interpolates.
function taps(srcLen, start, len, n) {
  const step = len / n, out = [];
  for (let i = 0; i < n; i++) {
    const idx = [], wt = [];
    if (step >= 1) {
      const a = start + i * step, b = a + step;
      for (let s = Math.floor(a); s < Math.ceil(b); s++) {
        const w = Math.min(b, s + 1) - Math.max(a, s);
        if (w > 1e-6) { idx.push(Math.min(srcLen - 1, Math.max(0, s))); wt.push(w / step); }
      }
    } else {
      const c = start + (i + 0.5) * step - 0.5, s = Math.floor(c), f = c - s;
      idx.push(Math.min(srcLen - 1, Math.max(0, s)), Math.min(srcLen - 1, Math.max(0, s + 1))); wt.push(1 - f, f);
    }
    out.push([idx, wt]);
  }
  return out;
}

// The crop {x, y, w, h} (source pixels, may be fractional) of a grey image,
// scaled to dw x dh.
export function resample(grey, sw, sh, crop, dw, dh) {
  const tx = taps(sw, crop.x, crop.w, dw), ty = taps(sh, crop.y, crop.h, dh);
  const y0 = Math.max(0, Math.floor(crop.y) - 1), y1 = Math.min(sh, Math.ceil(crop.y + crop.h) + 1);
  const mid = new Float32Array(dw * (y1 - y0));   // the rows the crop touches, already narrowed
  for (let y = y0; y < y1; y++) {
    const row = y * sw, o = (y - y0) * dw;
    for (let x = 0; x < dw; x++) {
      const [idx, wt] = tx[x]; let v = 0;
      for (let k = 0; k < idx.length; k++) v += grey[row + idx[k]] * wt[k];
      mid[o + x] = v;
    }
  }
  const out = new Float32Array(dw * dh);
  for (let y = 0; y < dh; y++) {
    const [idx, wt] = ty[y];
    for (let x = 0; x < dw; x++) {
      let v = 0;
      for (let k = 0; k < idx.length; k++) v += mid[(Math.min(y1 - 1, Math.max(y0, idx[k])) - y0) * dw + x] * wt[k];
      out[y * dw + x] = v;
    }
  }
  return out;
}

// brightness and contrast, -100..100 (0: as it is). Contrast works around mid grey.
export function adjust(grey, { brightness = 0, contrast = 0 } = {}) {
  const k = contrast >= 0 ? 1 + contrast / 50 : 1 + contrast / 125, b = brightness * 1.28;
  const out = new Float32Array(grey.length);
  for (let i = 0; i < grey.length; i++) out[i] = Math.min(255, Math.max(0, (grey[i] - 128) * k + 128 + b));
  return out;
}

// Floyd-Steinberg to four levels (0 black .. 3 white, at 0, 85, 170, 255 as
// on the board), serpentine so the error doesn't drift one way.
export function dither4(grey, w, h) {
  const lv = new Uint8Array(w * h);
  let cur = new Float32Array(w + 2), next = new Float32Array(w + 2);
  for (let y = 0; y < h; y++) {
    next.fill(0);
    const ltr = (y & 1) === 0, d = ltr ? 1 : -1;
    for (let i = 0; i < w; i++) {
      const x = ltr ? i : w - 1 - i, at = y * w + x;
      const want = Math.min(255, Math.max(0, grey[at] + cur[x + 1]));
      const level = Math.min(3, Math.max(0, Math.round(want / 85)));
      const miss = want - level * 85;
      lv[at] = level;
      cur[x + 1 + d] += miss * 7 / 16;
      next[x + 1 - d] += miss * 3 / 16;
      next[x + 1] += miss * 5 / 16;
      next[x + 1 + d] += miss / 16;
    }
    [cur, next] = [next, cur];
  }
  return lv;
}

// Levels (0..3) to the board's bytes: rows of ceil(w / 4), leftmost pixel in the top bits.
export function pack2(levels, w, h) {
  const stride = Math.ceil(w / 4), out = new Uint8Array(stride * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) out[y * stride + (x >> 2)] |= levels[y * w + x] << ((3 - (x & 3)) * 2);
  return out;
}

export function unpack2(bytes, w, h) {
  const stride = Math.ceil(w / 4), lv = new Uint8Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) lv[y * w + x] = (bytes[y * stride + (x >> 2)] >> ((3 - (x & 3)) * 2)) & 3;
  return lv;
}

// Levels to RGBA in the panel's greys, for a canvas.
export function levelsToRgba(levels, greys = PANEL_GREYS) {
  const out = new Uint8ClampedArray(levels.length * 4);
  for (let i = 0; i < levels.length; i++) { const c = greys[levels[i]]; out[i * 4] = c[0]; out[i * 4 + 1] = c[1]; out[i * 4 + 2] = c[2]; out[i * 4 + 3] = 255; }
  return out;
}

// The size w x h scaled to fit inside bw x bh, keeping its shape.
export function fitSize(w, h, bw, bh) {
  const s = Math.min(bw / w, bh / h);
  return [Math.max(1, Math.min(bw, Math.round(w * s))), Math.max(1, Math.min(bh, Math.round(h * s)))];
}

// The crop of a sw x sh image with the shape of aw x ah: the largest that
// fits, divided by zoom (>= 1), centred on (cx, cy) as fractions of the
// image, moved in where it would stick out.
export function cropFor(sw, sh, aw, ah, { zoom = 1, cx = 0.5, cy = 0.5 } = {}) {
  const shape = aw / ah;
  let w = sw, h = sw / shape;
  if (h > sh) { h = sh; w = sh * shape; }
  w /= Math.max(1, zoom); h /= Math.max(1, zoom);
  const x = Math.min(sw - w, Math.max(0, cx * sw - w / 2)), y = Math.min(sh - h, Math.max(0, cy * sh - h / 2));
  return { x, y, w, h };
}

// The crop c grown to the shape aw x ah around its middle, for full screen:
// what was framed, and more above and below (or at the sides) to fill the
// screen. Kept inside the picture; where the picture ends, it is cut the
// other way instead.
export function fullCrop(c, sw, sh, aw, ah) {
  const shape = aw / ah;
  let w = Math.max(c.w, c.h * shape), h = w / shape;
  if (h > sh) { h = sh; w = h * shape; }
  if (w > sw) { w = sw; h = w / shape; }
  const mx = c.x + c.w / 2, my = c.y + c.h / 2;
  return { x: Math.min(sw - w, Math.max(0, mx - w / 2)), y: Math.min(sh - h, Math.max(0, my - h / 2)), w, h };
}

// A title of at most PHOTO_TITLE_BYTES bytes of UTF-8, cut at a character.
export function photoTitle(s) {
  let t = String(s ?? '').replace(/[\u0000-\u001f\u007f]/g, ' ').trim();
  while (enc.encode(t).length > PHOTO_TITLE_BYTES) t = Array.from(t).slice(0, -1).join('');
  return t;
}

// The title a file name suggests: no extension, no underscores.
export function titleFromName(name) { return photoTitle(String(name || '').replace(/\.[a-z0-9]{2,5}$/i, '').replace(/[_]+/g, ' ')); }

// One grey image and its choices into the board's file, plus the pictures
// for the preview. grey: Float32Array, sw x sh. orient: 'landscape' or
// 'portrait'. fit: show the whole picture with white bars instead of
// filling the area (then crop is the whole image unless given). With
// areas.full (photoAreas() of a board with full screen) the file also gets
// the full-screen picture, unless that would make it larger than maxBytes.
export function buildPhoto({ grey, sw, sh, orient = 'landscape', crop, fit = false, brightness = 0, contrast = 0, title = '', added, areas = PHOTO_AREA, maxBytes = Infinity } = {}) {
  if (!(grey && sw > 0 && sh > 0)) throw new Error('no picture');
  const other = orient === 'portrait' ? 'landscape' : 'portrait';
  const [aw, ah] = areas[orient], [bw, bh] = areas[other];
  const c = crop || (fit ? { x: 0, y: 0, w: sw, h: sh } : cropFor(sw, sh, aw, ah));
  const make = (kind, o, w, h, from = c) => {
    const levels = dither4(adjust(resample(grey, sw, sh, from, w, h), { brightness, contrast }), w, h);
    return { kind, orient: o, w, h, levels, bytes: pack2(levels, w, h) };
  };
  const mainSize = fit ? fitSize(c.w, c.h, aw, ah) : [aw, ah];
  const pics = [
    make(PHOTO_KIND.photo, orient, ...mainSize),
    make(PHOTO_KIND.other, other, ...fitSize(c.w, c.h, bw, bh)),
    make(PHOTO_KIND.thumb, orient, ...fitSize(c.w, c.h, PHOTO_THUMB, PHOTO_THUMB)),
  ];
  if (areas.full?.[orient]) {   // whole, with bars, when fitting; else the crop grown to fill the screen
    const [fw, fh] = areas.full[orient], fc = fit ? c : fullCrop(c, sw, sh, fw, fh);
    const full = make(PHOTO_KIND.full, orient, ...(fit ? fitSize(c.w, c.h, fw, fh) : [fw, fh]), fc);
    if (PHOTO_HEAD + [...pics, full].reduce((n, p) => n + p.bytes.length, 0) <= maxBytes) pics.push(full);
  }
  return { bytes: photoFile(pics, { title, added }), pics, crop: c };
}

export function photoFile(pics, { title = '', added = Math.floor(Date.now() / 1000) } = {}) {
  const size = PHOTO_HEAD + pics.reduce((n, p) => n + p.bytes.length, 0);
  const out = new Uint8Array(size), v = new DataView(out.buffer);
  out.set(enc.encode('STKP'), 0);
  out[4] = pics.some((p) => p.kind === PHOTO_KIND.full) ? PHOTO_VERSION_FULL : PHOTO_VERSION; out[5] = pics.length; v.setUint16(6, PHOTO_HEAD, true); v.setUint32(8, added >>> 0, true);
  pics.forEach((p, i) => { const o = 12 + i * 8; out[o] = p.kind; out[o + 1] = p.orient === 'portrait' ? 1 : 0; v.setUint16(o + 2, p.w, true); v.setUint16(o + 4, p.h, true); });
  out.set(enc.encode(photoTitle(title)), 44);
  let o = PHOTO_HEAD;
  for (const p of pics) { out.set(p.bytes, o); o += p.bytes.length; }
  return out;
}

// The header back (tests; a file the board sent).
export function parsePhotoHead(bytes) {
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes.length < PHOTO_HEAD || new TextDecoder().decode(bytes.subarray(0, 4)) !== 'STKP') throw new Error('not a photo file');
  const n = bytes[5], pics = [];
  let off = PHOTO_HEAD;
  for (let i = 0; i < n; i++) {
    const o = 12 + i * 8, w = v.getUint16(o + 2, true), h = v.getUint16(o + 4, true), len = Math.ceil(w / 4) * h;
    pics.push({ kind: bytes[o], orient: bytes[o + 1] ? 'portrait' : 'landscape', w, h, off, len }); off += len;
  }
  const t = bytes.subarray(44, 44 + 48); const z = t.indexOf(0);
  return { version: bytes[4], added: v.getUint32(8, true), pics, size: off, title: new TextDecoder().decode(z < 0 ? t : t.subarray(0, z)) };
}

export async function sha256Hex(bytes) {
  const d = new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', bytes));
  return Array.from(d, (b) => b.toString(16).padStart(2, '0')).join('');
}

export function toBase64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}
export function fromBase64(b64) { const s = atob(b64); const out = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i); return out; }

// Sends one photo file: photo_begin, photo_chunk until it's all there,
// photo_end (the board checks the SHA-256 and the header, then keeps it).
// A chunk whose answer was lost is sent again; the board says where it is
// ("next") and the upload goes on from there. Any other failure, or
// signal.aborted, sends photo_abort, so the board drops the part it has.
// Resolves to {id, exists, photo, storage}.
export async function uploadPhoto(client, bytes, { onProgress = () => {}, signal, chunk = PHOTO_CHUNK, retries = 3 } = {}) {
  const sha256 = await sha256Hex(bytes);
  const begin = await client.request('photo_begin', { size: bytes.length, sha256 });
  const id = begin.photo?.id;
  if (begin.exists) { onProgress(bytes.length, bytes.length); return { id, exists: true, photo: begin.photo }; }
  const per = Math.max(256, Math.min(chunk, begin.chunk || chunk));
  let at = 0;
  try {
    while (at < bytes.length) {
      if (signal?.aborted) throw Object.assign(new Error('Upload stopped.'), { name: 'AbortError' });
      const part = bytes.subarray(at, at + per);
      let r, tries = 0;
      for (;;) {
        try { r = await client.request('photo_chunk', { offset: at, data: toBase64(part) }, { quiet: true, timeout: 10000 }); break; }
        catch (e) {
          const next = e.response?.next;
          if (Number.isInteger(next) && next !== at && next <= bytes.length) { r = { next }; break; }   // the board is elsewhere: go on from there
          if (++tries > retries || e.response) throw e;   // the board refused it: no point sending it again
        }
      }
      at = Number.isInteger(r.next) ? r.next : at + part.length;
      onProgress(at, bytes.length);
    }
    const end = await client.request('photo_end', {}, { timeout: 15000 });
    return { id, exists: false, photo: end.photo, storage: end.storage };
  } catch (e) {
    try { await client.request('photo_abort', {}, { timeout: 4000 }); } catch { /* the link is gone: the board drops it after 30 s */ }
    throw e;
  }
}

// ======================================================================= UI

function boot() {
  const params = new URLSearchParams(location.search);
  const demo = (params.has('demo') && params.get('demo') !== '0') || (params.has('mock') && params.get('mock') !== '0');
  const ui = new App({ demo, mockOpts: { mtu: Number(params.get('mtu')) || undefined } });
  ui.start();
}

// tiny DOM helper: h('div.card#x', {onclick}, child, 'text', ...)
function h(sel, attrs, ...kids) {
  const [, tag = 'div', rest = ''] = sel.match(/^([a-z0-9]*)(.*)$/i);
  const el = document.createElement(tag || 'div');
  for (const m of rest.matchAll(/([.#])([\w-]+)/g)) m[1] === '.' ? el.classList.add(m[2]) : (el.id = m[2]);
  if (attrs && (typeof attrs !== 'object' || attrs instanceof Node || Array.isArray(attrs))) { kids.unshift(attrs); attrs = null; }
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'class') { if (v) el.className += ' ' + v; }
    else if (k in el && k !== 'list' && typeof v !== 'string') el[k] = v;
    else el.setAttribute(k, v === true ? '' : v);
  }
  const add = (c) => { if (c == null || c === false) return; if (Array.isArray(c)) c.forEach(add); else el.append(c instanceof Node ? c : String(c)); };
  kids.forEach(add);
  return el;
}

// Inline icons, drawn like the site's app icons (stroke, round joins).
const ICONS = {
  usb: '<path d="M12 3v13"/><path d="M9 6l3-3 3 3"/><path d="M12 16a2.2 2.2 0 1 0 0 4.4A2.2 2.2 0 0 0 12 16z"/><path d="M12 13l-4.5-2.5V8"/><rect x="6" y="6" width="3" height="2.4" rx=".4"/><path d="M12 11l4.5-2V7"/><circle cx="16.5" cy="6" r="1.2"/>',
  bt: '<path d="M7 7l10 10-5 4V3l5 4L7 17"/>',
  lock: '<rect x="5" y="11" width="14" height="9.5" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>',
  check: '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
  dot: '<circle cx="12" cy="12" r="3.5"/>',
  wifi: '<path d="M2.5 9a14 14 0 0 1 19 0"/><path d="M5.5 12.5a9.5 9.5 0 0 1 13 0"/><path d="M8.7 16a5 5 0 0 1 6.6 0"/><circle cx="12" cy="19.2" r=".9"/>',
  up: '<path d="M12 19V5M6 11l6-6 6 6"/>',
  down: '<path d="M12 5v14M6 13l6 6 6-6"/>',
  arrow: '<path d="M5 12h14M13 6l6 6-6 6"/>',
  photo: '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M3 16l5-5 4 4 3-3 6 6"/><circle cx="15.5" cy="9.5" r="1.5"/>',
};
function icon(name, cls = '') {
  const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  s.setAttribute('viewBox', '0 0 24 24'); s.setAttribute('aria-hidden', 'true'); s.setAttribute('class', 'ic ' + cls);
  s.innerHTML = ICONS[name] || '';
  return s;
}

const $ = (s) => document.querySelector(s);

function friendlyError(e, kind = 'ble') {
  const m = e?.message || String(e);
  if (e?.name === 'NotFoundError' && /cancel|no port selected/i.test(m)) {
    return kind === 'serial' ? 'No port chosen. If the board wasn’t in the list, check that the cable carries data (some only charge) and that the board is on.'
      : 'No board chosen. If it wasn’t in the list, check that it shows its setup screen: on the board, Settings → Connect to the setup app.';
  }
  if (kind === 'serial') {
    if (e?.name === 'NotFoundError') return 'The board isn’t plugged in, or this browser can’t see it. Is the cable a data cable, and is the board on? ' + m;
    if (e?.name === 'InvalidStateError' || /already open/i.test(m)) return 'The port is already open, maybe in another tab.';
    if (e?.name === 'NetworkError' || /failed to open/i.test(m)) return 'Couldn’t open the USB port. Close any other program using it (a serial monitor, the Arduino IDE, another tab), then try again.';
    return m;
  }
  if (e?.name === 'NotFoundError') return 'The board doesn’t offer the setup service. Is it in setup mode, with firmware that has setup support? ' + m;
  if (e?.name === 'SecurityError' || /auth|pair|encrypt|insufficient/i.test(m))
    return 'Pairing failed or was cancelled. Check the 6-digit code on the board and try again. '
      + 'If the board was factory-reset, first remove it from this computer’s or phone’s Bluetooth settings.';
  if (e?.name === 'NetworkError' || /disconnected|connection/i.test(m)) return 'Lost the Bluetooth connection. Is the board still in setup mode? Move closer and connect again.';
  return m;
}

// Which browser is this, for a kind "can't" message.
function browserKind() {
  const ua = navigator.userAgent || '';
  if (/iPhone|iPad|iPod/.test(ua) || (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1)) return 'ios';
  if (/Firefox\//.test(ua)) return 'firefox';
  if (/Safari\//.test(ua) && !/Chrome\/|Chromium\/|Edg\//.test(ua)) return 'safari';
  return 'other';
}

const SECRET_TAG = () => h('span.tag.secret', { title: 'Write-only: the board never sends it back' }, icon('lock'), 'secret');

class App {
  constructor({ demo, mockOpts }) {
    this.demo = demo; this.mockOpts = mockOpts;
    this.product = document.documentElement.dataset.product || 'the firmware';
    this.keys = []; this.info = null; this.edits = new Map(); this.errors = new Map();
    this.wifi = null; this.scan = null; this.section = 'overview'; this.openAdv = new Set();
    this.connected = false; this.busy = false;
    this.ph = { list: null, storage: null, thumbs: new Map(), queue: [], editor: null, loading: false, uploading: null, armed: null };
  }

  async start() {
    this.root = $('#root');
    this.renderShell();
    this.can = { ble: this.demo || BleTransport.supported(), serial: this.demo || SerialTransport.supported() };
    if (!this.can.ble && !this.can.serial) return this.renderUnsupported();
    if (!this.demo && !window.isSecureContext) return this.renderUnsupported('insecure');
    if (this.demo) {
      const m = await import('./mock.js');
      this.mockMod = m;
      this.mockDevice = m.sharedDevice({ storage: window.localStorage, ...this.mockOpts });
      this.mockSerial = m.sharedSerial(this.mockDevice);
      m.mountScreen(this.mockDevice, $('#board'));
    }
    this.renderConnect();
  }

  // -------------------------------------------------------------- shell
  renderShell() {
    this.root.replaceChildren(...[
      this.demo ? h('div.demo-note', { role: 'note' },
        h('p', h('b', 'Demo. '), 'This page is talking to a simulated board, not real hardware. USB and Bluetooth both reach it. In the WiFi test, any password of 8 or more characters joins, unless it contains “wrong”.'),
        h('p', h('a', { href: './' }, 'Leave the demo'))) : null,
      h('div.stage',
        h('div.layout#layout', h('nav.side#nav', { 'aria-label': 'Settings groups' }), h('div.panel#panel')),
        h('aside.board#board', { hidden: !this.demo, 'aria-label': 'Simulated board' })),
      h('div.savebar#savebar'),
      h('div.logs',
        h('details.log.devlog#devlog', { hidden: true }, h('summary', 'Board log (USB)'), h('pre#devlogText', { tabindex: 0 })),
        h('details.log#log', h('summary', 'Activity log'), h('p.muted.small', 'Every request and answer. Secrets are replaced with dots before they are written here.'), h('ol#logList'))),
      h('dialog.modal#modal'),
      h('div.toasts#toasts', { 'aria-live': 'polite', role: 'status' }),
    ].filter(Boolean));
    $('#modal').addEventListener('cancel', (e) => { if ($('#modal').dataset.locked) e.preventDefault(); });
    this.setStatus();
  }

  setStatus(text) {
    const s = $('#status'); if (!s) return;
    const name = this.info?.name || this.transport?.name;
    const via = this.transport?.kind === 'serial' ? 'USB' : 'Bluetooth';
    s.replaceChildren(...[
      h('span.dot', { class: this.connected ? 'on' : 'off', 'aria-hidden': 'true' }),
      h('span.status-text', text || (this.connected ? `${name} · ${via}` : 'Not connected')),
      this.connected ? h('button.btn.quiet.small', { onclick: () => this.disconnect() }, 'Disconnect') : null,
    ].filter(Boolean));
  }

  log(dir, text) {
    const list = $('#logList'); if (!list) return;
    const t = new Date().toLocaleTimeString([], { hour12: false });
    list.append(h('li', { class: dir }, h('time', t), h('span.dir', { 'aria-label': dir }, { in: '←', out: '→', warn: '!', info: '·' }[dir] || '·'), h('code', text)));
    while (list.children.length > 300) list.firstChild.remove();
    list.scrollTop = list.scrollHeight;
  }

  panel(...kids) { $('#panel').replaceChildren(...kids.filter(Boolean)); }

  renderUnsupported(why) {
    $('#nav').replaceChildren(); $('#layout').classList.add('solo');
    if (why === 'insecure') {
      return this.panel(h('div.card',
        h('h2', 'Open this page over HTTPS'),
        h('p', 'Browsers only let a page talk to USB and Bluetooth devices when it is secure: an https:// address, or http://localhost while testing.')));
    }
    const kind = browserKind();
    const reason = {
      ios: 'On iPhone and iPad, no browser can reach USB or Bluetooth devices from a web page. Apple requires every browser there to use Safari’s engine, which leaves both out.',
      firefox: 'Firefox doesn’t support Web Serial or Web Bluetooth, the two ways a web page can talk to the board.',
      safari: 'Safari doesn’t support Web Serial or Web Bluetooth, the two ways a web page can talk to the board.',
      other: 'This browser doesn’t support Web Serial or Web Bluetooth, the two ways a web page can talk to the board.',
    }[kind];
    this.panel(h('div.card.unsupported',
      h('h2', 'This browser can’t reach the board'),
      h('p', reason),
      h('h3', 'What works'),
      h('ul.plain',
        h('li', h('b', 'Chrome or Edge on a computer'), ' (Mac, Windows, Linux or ChromeOS): over the USB cable or Bluetooth.'),
        h('li', h('b', 'Chrome on Android'), ': over Bluetooth.')),
      h('h3', 'Only need WiFi?'),
      h('p', 'Set it on the board itself: ', h('b', 'Settings → WiFi'), '.'),
      h('p.actions', h('a.btn', { href: '?demo' }, 'Try the demo instead'), h('a', { href: '../' }, 'Back to the main page'))));
  }

  // ------------------------------------------------------------ connect
  renderConnect(message, tone = 'error') {
    this.connected = false; this.setStatus();
    $('#nav').replaceChildren(); $('#savebar').replaceChildren(); $('#layout').classList.add('solo');
    const kind = this.transport?.kind;
    const again = this.transport && (this.transport.device || this.transport.mockDevice || this.transport.port)
      ? h('button.btn.secondary', { onclick: () => this.connect({ kind, reuse: true }) }, `Reconnect to ${this.transport.name}`) : null;
    const option = (k, ic, title, lead, button, steps, note) => h('div.option', { class: this.can[k] ? '' : 'off' },
      h('div.option-head', h('span.badge-icon', icon(ic)), h('div', h('h3', title), h('p.lead', lead))),
      h('ol.steps', ...steps),
      this.can[k] ? h('button.btn.primary.wide', { onclick: () => this.connect({ kind: k }) }, button) : h('p.muted.small.off-note', note));
    this.panel(h('div.connect',
      message ? h('p.notice', { class: tone, role: tone === 'error' ? 'alert' : 'status' }, message) : null,
      h('h2', 'Connect to your board'),
      h('div.options',
        option('serial', 'usb', 'USB cable', 'The quickest way, from a computer.', 'Connect over USB', [
          h('li', 'Plug the board into this computer with a USB-C cable that carries data.'),
          h('li', 'Close any serial monitor or Arduino IDE that has the port open.'),
          h('li', 'Click ', h('b', 'Connect over USB'), ' and pick the port. It is usually called “USB Single Serial” (on a Mac, cu.usbmodem…; on Windows, a COM port).'),
          h('li', 'The board restarts when the port opens. That is expected and takes a few seconds.'),
        ], 'This browser has no Web Serial. Chrome or Edge on a computer has it.'),
        option('ble', 'bt', 'Bluetooth', 'From a computer or an Android phone.', 'Connect over Bluetooth', [
          h('li', 'On the board: ', h('b', 'Settings → Connect to the setup app'), '. It restarts into setup mode and shows its name.'),
          h('li', 'Click ', h('b', 'Connect over Bluetooth'), ' and pick the board by that name.'),
          h('li', 'When asked to pair, type the ', h('b', '6-digit code'), ' the board shows.'),
          h('li', 'Saving restarts the board normally, which ends setup mode.'),
        ], 'This browser has no Web Bluetooth. Chrome or Edge has it, on a computer or on Android.')),
      again ? h('p.actions', again) : null,
      h('p.muted.small.privacy', 'Nothing you type here goes to a server. This page talks to the board directly, and secrets can be written but never read back.')));
  }

  newTransport(kind) {
    if (kind === 'serial') return new SerialTransport(this.demo ? { serial: this.mockSerial } : {});
    return this.demo ? new this.mockMod.MockTransport(this.mockDevice, { passkeyPrompt: (d) => this.mockPairPrompt(d) }) : new BleTransport();
  }

  async connect({ kind = 'serial', reuse = false } = {}) {
    if (this.busy) return;
    this.busy = true;
    try {
      if (!reuse || !this.transport || this.transport.kind !== kind) {
        await this.transport?.close?.();
        this.transport = this.newTransport(kind);
        await this.transport.choose();
      }
      this.showConnecting();
      await this.transport.connect();
      this.attachTransport();
      this.log('info', `connected to ${this.transport.name} over ${kind === 'serial' ? 'USB' : 'Bluetooth'}`);
      this.section = 'overview';
      await this.loadAll();
    } catch (e) {
      this.log('warn', `${e.name || 'Error'}: ${e.message}`);
      await this.transport?.close?.();
      this.renderConnect(friendlyError(e, this.transport?.kind));
    } finally {
      this.busy = false; this.closeModal();
    }
  }

  // The one place a freshly (re)connected transport is wired to the app.
  attachTransport() {
    this.makeClient();
    this.connected = true;
    const panel = $('#devlog');
    if (this.transport.kind === 'serial') {
      panel.hidden = false;
      const pre = $('#devlogText');
      const draw = () => { pre.textContent = this.transport.lines.join('\n'); pre.scrollTop = pre.scrollHeight; };
      let queued = false;
      this.transport.onLog(() => { if (!queued) { queued = true; requestAnimationFrame(() => { queued = false; draw(); }); } });
      draw();
    } else panel.hidden = true;
  }

  makeClient() {
    this.client = new Client(this.transport, {
      log: (d, t) => this.log(d, t),
      redact: (r) => redactRequest(r, (k) => this.keys.find((x) => x.key === k)?.secret),
      onDisconnect: () => this.onDisconnected(),
    });
  }

  showConnecting() {
    $('#layout').classList.add('solo');
    if (this.transport?.kind === 'serial') {
      return this.panel(h('div.card.waiting', { role: 'status' },
        h('div.spinner', { 'aria-hidden': 'true' }),
        h('h2', 'Opening the USB port…'),
        h('p', 'The board restarts when the port opens. This page waits for it to start, which can take up to 20 seconds.'),
        h('p.muted', 'What the board prints appears under “Board log (USB)” at the bottom of the page.')));
    }
    this.panel(h('div.card.waiting', { role: 'status' },
      h('div.spinner', { 'aria-hidden': 'true' }),
      h('h2', 'Pairing…'),
      h('div.passkey-hint',
        h('div.code-sample', { 'aria-hidden': 'true' }, '123 456'),
        h('div',
          h('p', h('b', 'Look at the board.'), ' When it shows a 6-digit code, type it where your computer or phone asks to pair.'),
          h('p.muted', 'On a Mac the request is a system dialog, and it can open behind the browser window. Paired before? Then nothing is asked, and this takes a few seconds.')))));
  }

  async loadAll() {
    // info characteristic first (cheap, and the read that triggers pairing),
    // then the key list, which is the source of truth for what to show.
    try { this.info = JSON.parse(await this.transport.readInfo()); }
    catch (e) { this.log('warn', `info read: ${e.message}; asking with the info op`); this.info = null; }
    if (!this.info) this.info = stripResponse(await this.client.request('info'));
    if (this.info?.proto != null && Number(this.info.proto) > PROTOCOL) {
      this.log('warn', `the board speaks setup protocol ${this.info.proto}; this page speaks ${PROTOCOL}`);
      this.toast(`The board’s firmware is newer than this page (setup protocol ${this.info.proto}). Reload the page; some settings may not work until it is updated.`, 'error');
    }
    const r = await this.client.request('keys');
    this.keys = (r.keys || []).map(normalizeKey);
    this.edits.clear(); this.errors.clear(); this.scan = null;
    if (this.ph.boardId !== this.info?.id) { this.ph.list = null; this.ph.thumbs.clear(); this.ph.boardId = this.info?.id; }
    this.resetWifi();
    this.setStatus();
    $('#layout').classList.remove('solo');
    this.renderNav(); this.show(this.section);
  }

  onDisconnected() {
    if (!this.connected) return;
    this.connected = false; this.setStatus();
    this.log('warn', 'board disconnected');
    if (!this.expectRestart) this.renderConnect(this.transport?.kind === 'serial'
      ? 'The USB port closed: the cable came out, or the board restarted. Connect again to go on. Unsaved changes were not kept.'
      : 'The board disconnected. Is it still in setup mode? Connect again to go on. Unsaved changes were not kept.');
  }

  async disconnect() { this.connected = false; await this.transport?.close(); this.renderConnect(); }

  // ------------------------------------------------------------ layout
  groups() { return groupKeys(this.keys); }

  editsIn(g) { return g.keys.filter((k) => this.edits.has(k.key) || (isWifiList(k) && this.wifiDirty)).length; }

  renderNav() {
    const groups = this.groups();
    const link = (id, title, n) => h('li', h('a', {
      href: `#${id}`, 'data-id': id, 'aria-current': id === this.section ? 'page' : null,
      onclick: (e) => { e.preventDefault(); this.show(id, { focus: true }); },
    }, h('span', title), n ? h('span.count', { 'aria-label': `${n} unsaved` }, String(n)) : null));
    const main = groups.filter((g) => !g.optional), extra = groups.filter((g) => g.optional);
    $('#nav').replaceChildren(
      h('ul', link('overview', 'Overview'), ...main.map((g) => link(g.id, g.title, this.editsIn(g))),
        this.info?.photos ? link('photos', 'Photos', this.ph.queue.length) : null),
      extra.length ? h('p.nav-label', 'Optional') : null,
      extra.length ? h('ul', ...extra.map((g) => link(g.id, g.title, this.editsIn(g)))) : null,
      h('ul.nav-end', link('reset', 'Factory reset')));
    this.renderSavebar();
  }

  show(id, { focus = false } = {}) {
    this.section = id;
    for (const a of document.querySelectorAll('#nav a')) {
      if (a.dataset.id === id) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current');
    }
    let view;
    if (id === 'overview') view = this.overview();
    else if (id === 'reset') view = this.resetCard();
    else if (id === 'photos' && this.info?.photos) view = this.photosView();
    else {
      const g = this.groups().find((x) => x.id === id);
      if (!g) return this.show('overview', { focus });
      view = this.groupCard(g);
    }
    this.panel(view);
    if (focus) { const hd = $('#panel h2'); if (hd) { hd.tabIndex = -1; hd.focus({ preventScroll: false }); } }
  }

  // Re-render, keeping the focused control (by id) and the scroll position.
  rerender() {
    const a = document.activeElement, id = a && a !== document.body ? a.id : null;
    const y = window.scrollY;
    this.renderNav(); this.show(this.section);
    if (id) document.getElementById(id)?.focus({ preventScroll: true });
    window.scrollTo(0, y);
  }

  key(name) { return this.keys.find((k) => k.key === name); }
  value(name) { const k = this.key(name); if (!k) return undefined; const e = this.edits.get(name); return e?.kind === 'set' ? e.value : e?.kind === 'clear' ? undefined : k.value; }

  overview() {
    const i = this.info || {};
    const wifi = i.wifi || {};
    const setNames = new Set(i.keys || this.keys.filter((k) => k.set).map((k) => k.key));
    const isSet = (k) => k.set || setNames.has(k.key);
    const row = (k, v) => [h('dt', k), h('dd', v ?? '—')];

    const items = [];
    const go = (id) => (e) => { e.preventDefault(); this.show(id, { focus: true }); };
    const item = (id, title, state, done, optional) => items.push(h('li', h('a.step', { href: `#${id}`, onclick: go(id), class: done ? 'done' : '' },
      h('span.mark', { 'aria-hidden': 'true' }, done ? icon('check') : ''),
      h('span.step-text', h('b', title), h('span', state)),
      h('span.sr-only', done ? ' (done)' : optional ? ' (optional)' : ' (to do)'),
      icon('arrow', 'go'))));
    const lk = this.key('device.language');
    if (lk) {
      const v = this.value('device.language') || 'en';
      const chosen = isSet(lk) || this.edits.get(lk.key)?.kind === 'set';
      const b = browserLanguage();
      item('language', 'Language', chosen ? languageName(v) : `${languageName(v)} (the default)${b !== v ? `; your browser’s is ${languageName(b)}` : ''}`, chosen, true);
    }
    if (this.key('wifi.networks')) {
      const n = (this.wifi || []).length;
      item('wifi', 'WiFi', n ? `${n} network${n > 1 ? 's' : ''} saved` : 'No network yet', n > 0);
    }
    const tzk = this.key('device.timezone');
    if (tzk) {
      const v = this.value('device.timezone'); const z = v && zoneForPosix(v, this.tzName || hereZone());
      const chosen = isSet(tzk) || this.edits.get(tzk.key)?.kind === 'set';
      item('device', 'Time zone', chosen ? (z ? cityName(z) : v) : 'Not chosen yet', chosen);
    }
    if (this.keys.some((k) => prefixOf(k.key) === 'weather')) {
      const loc = this.value('weather.locate'), place = this.value('weather.place');
      item('weather', 'Weather', loc ? 'Finds your town by itself' : place ? `For ${place}` : 'No place yet', !!(loc || place));
    }
    const ai = this.key('openai.api_key');
    if (ai) {
      const e = this.edits.get(ai.key)?.kind, has = e ? e === 'set' : isSet(ai);
      item('voice', 'OpenAI key', e === 'set' ? 'New key, sent when you save' : e === 'clear' ? 'Will be cleared' : has ? 'Set' : 'Optional: only for voice notes', has, true);
    }

    const other = this.groups().filter((g) => g.optional);
    return h('div.card',
      h('div.card-head', h('h2', 'Overview'), h('button.btn.quiet.small', { onclick: () => this.refreshInfo() }, 'Refresh')),
      items.length ? [h('h3', 'Start here'), h('ol.start', ...items)] : null,
      h('h3', 'This board'),
      h('dl.facts',
        row('Name', i.name || this.value('device.name') || null),
        row('Board', i.board), row('ID', i.id),
        i.photos ? row('Photos', h('a', { href: '#photos', onclick: go('photos') }, `${i.photos.count} on the board`)) : null,
        row('Firmware', [i.fw, i.commit ? h('span.muted', ` (${String(i.commit).slice(0, 10)})`) : null]),
        row('WiFi', wifi.ssid ? [wifi.ssid, ' · ', wifi.connected ? h('span.ok', `connected${wifi.rssi != null ? `, ${wifi.rssi} dBm` : ''}`) : h('span.warn', 'not connected')] : h('span.muted', 'not connected'))),
      other.length ? [h('h3', 'Optional'), h('ul.more', ...other.map((g) => h('li', h('a', { href: `#${g.id}`, onclick: go(g.id) }, g.title),
        h('span.muted', ` ${g.keys.filter(isSet).length} of ${g.keys.length} set`))))] : null);
  }

  async refreshInfo() {
    try { this.info = stripResponse(await this.client.request('info')); this.show('overview'); this.toast('Refreshed.', 'ok'); }
    catch (e) { this.toast(friendlyError(e, this.transport?.kind), 'error'); }
  }

  groupCard(g) {
    const basic = g.keys.filter((k) => !isAdvanced(k)), adv = g.keys.filter(isAdvanced);
    const card = h('div.card', h('div.card-head', h('h2', g.title)), g.blurb ? h('p.blurb', g.blurb) : null);
    for (const k of basic) card.append(isWifiList(k) ? this.wifiEditor(k) : this.field(k));
    if (adv.length) {
      const open = this.openAdv.has(g.id) || adv.some((k) => this.edits.has(k.key) || this.errors.has(k.key));
      const d = h('details.advanced', { open },
        h('summary', 'Advanced', h('span.muted', ` · ${adv.length} setting${adv.length > 1 ? 's' : ''}`)),
        ...adv.map((k) => (isWifiList(k) ? this.wifiEditor(k) : this.field(k))));
      d.addEventListener('toggle', () => { d.open ? this.openAdv.add(g.id) : this.openAdv.delete(g.id); });
      card.append(d);
    }
    return card;
  }

  // ------------------------------------------------------------ fields
  field(k) {
    const info = KEY_INFO[k.key] || {};
    const type = info.type || k.type;
    const edit = this.edits.get(k.key);
    const err = this.errors.get(k.key);
    const id = 'f-' + k.key.replace(/\W/g, '-');
    const help = info.help || (k.help && k.help !== keyLabel(k) ? k.help : null);
    const helpId = help ? id + '-help' : null, errId = err ? id + '-err' : null;
    const describedBy = [helpId, errId].filter(Boolean).join(' ') || null;
    const tags = [k.secret ? SECRET_TAG() : null,
      edit ? h('span.tag.pending', edit.kind === 'clear' ? 'will be cleared' : 'changed') : null];
    let control, labelFor = id;

    if (type === 'ics') { control = this.icsControl(k, id, describedBy); labelFor = null; }
    else if (k.secret) { control = this.secretControl(k, id, info, describedBy); labelFor = null; }
    else if (type === 'bool') {
      const v = edit?.kind === 'set' ? edit.value : !!k.value;
      control = h('label.switch', h('input', { id, type: 'checkbox', role: 'switch', checked: v, 'aria-describedby': describedBy, onchange: (e) => this.stage(k, e.target.checked) }),
        h('span.track', { 'aria-hidden': 'true' }), h('span.switch-text', v ? 'On' : 'Off'));
    } else if (type === 'tz') control = this.tzControl(k, id, describedBy);
    else if (type === 'lang') {
      const cur = (edit?.kind === 'set' ? edit.value : k.value) || 'en';
      const codes = Array.isArray(k.options) && k.options.length ? k.options : Object.keys(LANGUAGES);
      control = h('select', { id, lang: cur, 'aria-describedby': describedBy, onchange: (e) => this.stage(k, e.target.value) },
        ...codes.map((o) => h('option', { value: o, lang: o, selected: o === cur }, languageName(o))));
    }
    else if (Array.isArray(k.options)) {
      const cur = edit?.kind === 'set' ? edit.value : k.value;
      control = h('select', { id, 'aria-describedby': describedBy, onchange: (e) => this.stage(k, e.target.value) },
        ...k.options.map((o) => h('option', { value: o, selected: o === cur }, String(o))));
    } else if (type === 'pem') {
      const cur = edit?.kind === 'set' ? edit.value : (k.value ?? '');
      control = h('textarea.pem', { id, rows: 6, spellcheck: false, placeholder: info.placeholder || '', 'aria-describedby': describedBy, maxlength: k.max_len,
        onchange: (e) => {
          const raw = e.target.value.trim();
          if (raw && !/^-----BEGIN [A-Z ]+-----[\s\S]+-----END [A-Z ]+-----$/.test(raw)) return this.fieldError(k, 'Paste the whole certificate, from -----BEGIN CERTIFICATE----- to -----END CERTIFICATE-----.');
          this.stage(k, raw ? raw + '\n' : '');
        } }, String(cur));
    } else {
      const isInt = type === 'int', isNum = type === 'float' || type === 'number';
      const cur = edit?.kind === 'set' ? edit.value : (k.value ?? '');
      control = h('input', {
        id, type: isInt || isNum ? 'number' : 'text', value: String(cur), placeholder: info.placeholder || '',
        min: k.min, max: k.max, step: isNum ? 'any' : null, maxlength: k.max_len, inputmode: info.inputmode,
        autocomplete: 'off', spellcheck: false, 'aria-describedby': describedBy, 'aria-invalid': err ? 'true' : null,
        onchange: (e) => {
          const raw = e.target.value.trim();
          if (isInt || isNum) {
            if (raw === '') return this.stage(k, '');
            const n = Number(raw);
            if (!Number.isFinite(n) || (isInt && !Number.isInteger(n))) return this.fieldError(k, isInt ? 'Whole numbers only.' : 'A number, please.');
            if ((k.min != null && n < k.min) || (k.max != null && n > k.max)) return this.fieldError(k, `Must be between ${k.min ?? '…'} and ${k.max ?? '…'}.`);
            this.stage(k, n);
          } else if (info.geo && raw && !(Math.abs(Number(raw)) <= (info.geo === 'lat' ? 90 : 180))) {
            this.fieldError(k, info.geo === 'lat' ? 'A latitude between -90 and 90, like 38.72.' : 'A longitude between -180 and 180, like -9.14.');
          } else this.stage(k, raw);
        },
      });
    }
    const unset = !k.secret && !k.set && !edit && type !== 'bool'
      ? h('span.muted.small.state-note', k.value != null && k.value !== '' ? 'board default' : 'not set') : null;
    const geoBtn = info.geo === 'lat' && this.key('weather.lon') && 'geolocation' in navigator
      ? h('button.btn.quiet.small', { id: id + '-geo', type: 'button', onclick: () => this.fillLocation() }, 'Use this device’s location') : null;
    const labelEl = labelFor ? h('label.label', { for: labelFor }, keyLabel(k), ...tags) : h('span.label', { id: id + '-label' }, keyLabel(k), ...tags);
    return h('div.field', { class: err ? 'has-error' : '' }, labelEl,
      h('div.control', control, unset, geoBtn,
        edit ? h('button.btn.link.small', { id: id + '-undo', onclick: () => this.unstage(k.key) }, 'Undo') : null),
      help ? h('p.help', { id: helpId }, help) : null, err ? h('p.error', { id: errId, role: 'alert' }, err) : null);
  }

  secretControl(k, id, info, describedBy) {
    const edit = this.edits.get(k.key);
    const labelled = id + '-label';
    if (edit?.kind === 'clear') return h('div.secret', h('span.state.unset', 'Will be cleared when you save'));
    if (edit?.kind === 'set') return h('div.secret', h('span.state.set', icon('dot'), 'New value typed, sent when you save'));
    if (this.openSecret === k.key) {
      const inp = h('input', { id, type: 'password', placeholder: info.placeholder || 'New value', autocomplete: 'off', spellcheck: false, 'aria-labelledby': labelled, 'aria-describedby': describedBy, maxlength: k.max_len });
      const showBtn = h('button.btn.quiet.small', { type: 'button', 'aria-pressed': 'false', onclick: () => {
        const show = inp.type === 'password'; inp.type = show ? 'text' : 'password';
        showBtn.textContent = show ? 'Hide' : 'Show'; showBtn.setAttribute('aria-pressed', String(show));
      } }, 'Show');
      const ok = () => { const v = inp.value; if (!v) return this.fieldError(k, 'Type a value, or press Cancel.'); this.openSecret = null; this.stage(k, v); queueMicrotask(() => document.getElementById(id + '-set')?.focus()); };
      const cancel = () => { this.openSecret = null; this.rerender(); document.getElementById(id + '-set')?.focus(); };
      inp.addEventListener('keydown', (e) => { if (e.key === 'Enter') ok(); if (e.key === 'Escape') cancel(); });
      queueMicrotask(() => inp.focus());
      return h('div.secret.open', inp, showBtn, h('button.btn.secondary.small', { onclick: ok }, 'Use'), h('button.btn.quiet.small', { onclick: cancel }, 'Cancel'));
    }
    return h('div.secret',
      k.set ? h('span.state.set', icon('dot'), 'Set') : h('span.state.unset', 'Not set'),
      h('button.btn.secondary.small', { id: id + '-set', 'aria-describedby': labelled, onclick: () => { this.openSecret = k.key; this.rerender(); } }, k.set ? 'Replace' : 'Set'),
      k.set ? h('button.btn.quiet.small.danger', { id: id + '-clear', 'aria-describedby': labelled, onclick: () => this.stageClear(k) }, 'Clear') : null);
  }

  // calendar.ics: the links are secret, so this only ever writes them all at once.
  icsControl(k, id, describedBy) {
    const edit = this.edits.get(k.key);
    const labelled = id + '-label';
    const where = h('details.ics-where',
      h('summary', 'Where do I find the link?'),
      h('ul.small',
        h('li', h('b', 'Google Calendar: '), 'on a computer, Settings → under “Settings for my calendars” pick the calendar → Integrate calendar → ', h('b', 'Secret address in iCal format'), '.'),
        h('li', h('b', 'iCloud: '), 'in the Calendar app, the calendar’s share button → turn on ', h('b', 'Public Calendar'), ' → copy the link (it starts with webcal://).'),
        h('li', h('b', 'Outlook / Microsoft 365: '), 'Settings → Calendar → Shared calendars → ', h('b', 'Publish a calendar'), ', “Can view all details” → Publish → copy the ICS link.')),
      h('p.small.muted', 'Anyone with a secret link can read that calendar. If one leaks, reset it in your calendar service and paste the new one here.'));
    if (edit?.kind === 'clear') return h('div.secret', h('span.state.unset', 'Will be cleared when you save'));
    if (edit?.kind === 'set') {
      const n = edit.value ? edit.value.split('\n').length : 0;
      return h('div.secret', h('span.state.set', icon('dot'), `${n} calendar link${n === 1 ? '' : 's'} typed, sent when you save`));
    }
    if (this.openSecret === k.key) {
      const rows = [];
      const inputs = [];
      for (let i = 0; i < ICS_MAX; i++) {
        const lab = h('input', { id: `${id}-${i}-name`, type: 'text', placeholder: 'Label (optional)', maxlength: 24, autocomplete: 'off', 'aria-label': `Calendar ${i + 1} label` });
        const url = h('input', { id: `${id}-${i}-url`, type: 'password', placeholder: 'https://… or webcal://…', autocomplete: 'off', spellcheck: false, 'aria-label': `Calendar ${i + 1} link`, 'aria-describedby': describedBy });
        inputs.push([lab, url]);
        rows.push(h('div.ics-row', h('span.prio', { 'aria-hidden': 'true' }, String(i + 1)), lab, url));
      }
      const showBtn = h('button.btn.quiet.small', { type: 'button', 'aria-pressed': 'false', onclick: () => {
        const show = inputs[0][1].type === 'password';
        for (const [, u] of inputs) u.type = show ? 'text' : 'password';
        showBtn.textContent = show ? 'Hide links' : 'Show links'; showBtn.setAttribute('aria-pressed', String(show));
      } }, 'Show links');
      const ok = () => {
        let v;
        try { v = icsSetValue(inputs.map(([l, u]) => ({ label: l.value, url: u.value }))); } catch (e) { return this.fieldError(k, e.message); }
        if (!v) return this.fieldError(k, 'Paste at least one link, or press Cancel.');
        this.openSecret = null; this.stage(k, v); queueMicrotask(() => document.getElementById(id + '-set')?.focus());
      };
      const cancel = () => { this.openSecret = null; this.rerender(); document.getElementById(id + '-set')?.focus(); };
      for (const [l, u] of inputs) for (const el of [l, u]) el.addEventListener('keydown', (e) => { if (e.key === 'Enter') ok(); if (e.key === 'Escape') cancel(); });
      queueMicrotask(() => inputs[0][1].focus());
      return h('div.ics', ...rows,
        h('div.row', showBtn, h('button.btn.secondary.small', { onclick: ok }, 'Use'), h('button.btn.quiet.small', { onclick: cancel }, 'Cancel')),
        k.set ? h('p.small.muted', 'This replaces every link the board has.') : null, where);
    }
    const labels = icsLabels(k);
    const n = labels.length;
    const what = !k.set ? 'Not set'
      : n ? `${n} calendar${n === 1 ? '' : 's'}${labels.some(Boolean) ? ': ' + labels.map((l, i) => l || `Calendar ${i + 1}`).join(', ') : ''}` : 'Set';
    return h('div.ics-wrap',
      h('div.secret',
        k.set ? h('span.state.set', icon('dot'), what) : h('span.state.unset', what),
        h('button.btn.secondary.small', { id: id + '-set', 'aria-describedby': labelled, onclick: () => { this.openSecret = k.key; this.rerender(); } }, k.set ? 'Replace' : 'Add links'),
        k.set ? h('button.btn.quiet.small.danger', { id: id + '-clear', 'aria-describedby': labelled, onclick: () => this.stageClear(k) }, 'Clear') : null),
      where);
  }

  tzControl(k, id, describedBy) {
    const edit = this.edits.get(k.key);
    const cur = edit?.kind === 'set' ? edit.value : (k.value || '');
    const here = hereZone();
    const zone = zoneForPosix(cur, this.tzName || here);
    const sel = h('select', { id, 'aria-describedby': describedBy, onchange: (e) => {
      if (e.target.value === '__custom') { this.customTz = true; this.rerender(); return; }
      this.customTz = false;
      const z = TIMEZONES.find(([n]) => n === e.target.value);
      if (!z) return;
      this.tzName = z[0];
      this.stage(k, z[1]);
    } });
    const regions = new Map();
    for (const [name, posix] of TIMEZONES) {
      const r = name.includes('/') ? name.split('/')[0] : 'Universal';
      if (!regions.has(r)) regions.set(r, []);
      regions.get(r).push([name, posix]);
    }
    if (!zone) sel.append(h('option', { value: '', selected: true }, cur ? `Custom: ${cur}` : 'Choose a city…'));
    for (const [r, list] of regions) sel.append(h('optgroup', { label: r },
      ...list.sort((a, b) => cityName(a[0]).localeCompare(cityName(b[0]))).map(([name]) => h('option', { value: name, selected: name === zone }, `${name === 'UTC' ? 'UTC' : cityName(name)} (${offsetLabel(name)})`))));
    sel.append(h('optgroup', { label: 'Elsewhere' }, h('option', { value: '__custom' }, 'Type a POSIX TZ string…')));
    const herePosix = TIMEZONES.find(([n]) => n === here)?.[1];
    const preview = zone ? h('p.tz-now', `Now in ${zone === 'UTC' ? 'UTC' : cityName(zone)}: `, h('b', new Date().toLocaleTimeString([], { timeZone: zone, hour: '2-digit', minute: '2-digit' }))) : null;
    const custom = this.customTz ? h('input', { id: id + '-posix', type: 'text', 'aria-label': 'POSIX TZ string', placeholder: 'e.g. EET-2EEST,M3.5.0/3,M10.5.0/4', value: zone ? '' : cur, spellcheck: false,
      onchange: (e) => { const v = e.target.value.trim(); if (!looksLikePosixTz(v)) return this.fieldError(k, 'That doesn’t look like a POSIX TZ string, such as CET-1CEST,M3.5.0,M10.5.0/3.'); this.stage(k, v); } }) : null;
    return h('div.tz', h('div.row', sel,
      herePosix && herePosix !== cur ? h('button.btn.quiet.small', { id: id + '-here', onclick: () => { this.tzName = here; this.stage(k, herePosix); } }, `Use this computer’s (${cityName(here)})`) : null),
      custom, preview);
  }

  fillLocation() {
    navigator.geolocation.getCurrentPosition((p) => {
      const lat = this.key('weather.lat'), lon = this.key('weather.lon');
      const val = (k, n) => (k.type === 'float' || k.type === 'number' ? Math.round(n * 100) / 100 : n.toFixed(2));
      this.stage(lat, val(lat, p.coords.latitude), { quiet: true });
      this.stage(lon, val(lon, p.coords.longitude));
      this.toast('Filled in from this device’s location, to two decimals. It goes only to the board.', 'ok');
    }, (e) => this.toast(`No location: ${e.message || 'permission denied'}.`, 'error'), { maximumAge: 600000, timeout: 15000 });
  }

  stage(k, value, { quiet = false } = {}) {
    this.errors.delete(k.key);
    const same = !k.secret && JSON.stringify(value) === JSON.stringify(k.value ?? (k.type === 'bool' ? false : null));
    if (value === '') {   // emptied: back to the board's default
      if (k.set) this.edits.set(k.key, { kind: 'clear' }); else this.edits.delete(k.key);
    } else if (same) this.edits.delete(k.key);
    else this.edits.set(k.key, { kind: 'set', value });
    if (!quiet) this.rerender();
  }
  stageClear(k) { this.errors.delete(k.key); this.edits.set(k.key, { kind: 'clear' }); this.rerender(); }
  unstage(key) { this.edits.delete(key); this.errors.delete(key); this.rerender(); }
  fieldError(k, msg) { this.errors.set(k.key, msg); this.rerender(); }

  // ------------------------------------------------------------- WiFi
  get wifiDirty() {
    const w = this.keys.find(isWifiList); if (!w || !this.wifi) return false;
    return this.wifi.some((n) => n.password !== undefined) || JSON.stringify(wifiFromKey(w)) !== JSON.stringify(this.wifi.map((n) => n.ssid));
  }

  resetWifi() {
    const w = this.keys.find(isWifiList);
    this.wifi = w ? wifiFromKey(w).map((ssid) => ({ ssid, stored: true, password: undefined })) : null;
    if (w) this.errors.delete(w.key);
  }

  wifiEditor(k) {
    const list = this.wifi || [];
    const err = this.errors.get(k.key);
    const cur = this.info?.wifi?.ssid;
    const max = k.max_items || k.max || 8;
    const rows = list.length ? list.map((n, i) => h('li.net',
      h('span.prio', { 'aria-hidden': 'true' }, String(i + 1)),
      h('div.net-main', h('b', n.ssid), n.ssid === cur && this.info?.wifi?.connected ? h('span.tag.on', 'connected now') : null,
        h('div.muted.small', n.password !== undefined ? (n.password ? 'New password, sent when you save' : 'Open network, no password') : n.stored ? 'Password stored on the board' : '')),
      h('div.net-actions',
        h('button.btn.icon-btn', { id: `net-${i}-up`, 'aria-label': `Move ${n.ssid} up`, disabled: i === 0, onclick: () => this.moveNet(i, -1) }, icon('up')),
        h('button.btn.icon-btn', { id: `net-${i}-down`, 'aria-label': `Move ${n.ssid} down`, disabled: i === list.length - 1, onclick: () => this.moveNet(i, 1) }, icon('down')),
        h('button.btn.quiet.small', { onclick: () => this.netDialog(n.ssid, i) }, 'Change password'),
        h('button.btn.quiet.small.danger', { 'aria-label': `Remove ${n.ssid}`, onclick: () => { this.wifi.splice(i, 1); this.rerender(); } }, 'Remove')),
    )) : [h('li.empty', 'No networks yet. Scan for one, or add it by name.')];

    return h('div.field.wifi', { class: err ? 'has-error' : '' },
      h('div.card-head', h('h3', 'Saved networks', this.wifiDirty ? h('span.tag.pending', 'changed') : null),
        this.wifiDirty ? h('button.btn.link.small', { onclick: () => { this.resetWifi(); this.rerender(); } }, 'Undo') : null),
      h('p.help', `The board tries these in order, top first, and keeps up to ${max}. It never sends a stored password back; you can only replace it.`),
      h('ol.nets', ...rows),
      err ? h('p.error', { role: 'alert' }, err) : null,
      h('div.row', h('button.btn.primary', { id: 'wifi-scan', disabled: !!this.scanning, onclick: () => this.doScan() }, icon('wifi'), this.scanning ? 'Scanning…' : 'Scan for networks'),
        h('button.btn.secondary', { id: 'wifi-add', disabled: list.length >= max, onclick: () => this.netDialog('') }, 'Add by name')),
      this.scan ? h('div.scan',
        h('h3', 'Nearby'),
        this.scan.length ? h('ul.scanlist', ...this.scan.map((s) => h('li',
          h('span.bars', { role: 'img', 'aria-label': `signal ${bars(s.rssi)} of 4` }, ...[1, 2, 3, 4].map((b) => h('i', { class: b <= bars(s.rssi) ? 'on' : '' }))),
          h('b.ssid', s.ssid || h('i', '(hidden)')),
          s.secure ? h('span.muted.small', { title: 'Needs a password' }, icon('lock', 'tiny'), h('span.sr-only', 'secured')) : h('span.muted.small', 'open'),
          list.some((n) => n.ssid === s.ssid) ? h('span.muted.small', 'saved')
            : h('button.btn.secondary.small', { disabled: list.length >= max, 'aria-label': `Add ${s.ssid}`, onclick: () => this.netDialog(s.ssid, undefined, s.secure) }, 'Add…'))))
          : h('p.muted', 'No networks found.')) : null,
    );
  }

  moveNet(i, d) { const [n] = this.wifi.splice(i, 1); this.wifi.splice(i + d, 0, n); this.rerender(); document.getElementById(`net-${i + d}-${d < 0 ? 'up' : 'down'}`)?.focus(); }

  async doScan() {
    if (this.scanning) return;
    this.scanning = true; this.rerender();
    try { this.scan = ((await this.client.request('wifi_scan')).networks || []).sort((a, b) => b.rssi - a.rssi); this.toast(`Found ${this.scan.length} network${this.scan.length === 1 ? '' : 's'}.`); }
    catch (e) { this.toast('The scan failed: ' + friendlyError(e, this.transport?.kind), 'error'); }
    finally { this.scanning = false; if (this.connected) this.rerender(); }
  }

  // Add a network, or change an existing one's password (index given).
  netDialog(ssid, index, secure = true) {
    const editing = index !== undefined;
    const ssidIn = h('input#nd-ssid', { type: 'text', value: ssid, placeholder: 'Network name (SSID)', disabled: editing, spellcheck: false, autocomplete: 'off' });
    const openBox = h('input#nd-open', { type: 'checkbox', checked: !secure });
    const pwIn = h('input#nd-pw', { type: 'password', placeholder: 'Password', autocomplete: 'off', disabled: !secure });
    openBox.addEventListener('change', () => { pwIn.disabled = openBox.checked; });
    const result = h('div.test-result', { role: 'status' });
    const testBtn = h('button.btn.secondary', { onclick: async () => {
      const s = ssidIn.value.trim(); if (!s) return result.replaceChildren(h('p.error', 'Type the network name.'));
      testBtn.disabled = true; result.replaceChildren(h('p.muted', h('span.spinner.inline', { 'aria-hidden': 'true' }), ` Asking the board to join “${s}”… (up to 30 seconds)`));
      try {
        const r = await this.client.request('wifi_test', { ssid: s, password: openBox.checked ? '' : pwIn.value });
        result.replaceChildren(r.ok !== false ? h('p.ok', icon('check'), ` Joined. The board got ${r.ip || 'an address'}.`) : h('p.error', r.error || 'Could not join.'));
      } catch (e) { result.replaceChildren(h('p.error', friendlyError(e, this.transport?.kind))); }
      finally { testBtn.disabled = false; }
    } }, 'Test connection');
    const save = () => {
      const s = ssidIn.value.trim(); const pw = openBox.checked ? '' : pwIn.value;
      if (!s) return result.replaceChildren(h('p.error', 'Type the network name.'));
      if (enc.encode(s).length > 32) return result.replaceChildren(h('p.error', 'Network names are at most 32 bytes.'));
      if (!openBox.checked && (pw.length < 8 || pw.length > 63)) return result.replaceChildren(h('p.error', 'WPA passwords are 8 to 63 characters. Tick “Open network” if it has none.'));
      if (editing) this.wifi[index] = { ...this.wifi[index], password: pw };
      else if (this.wifi.some((n) => n.ssid === s)) return result.replaceChildren(h('p.error', 'That network is already in the list. Use “Change password” on it.'));
      else this.wifi.push({ ssid: s, stored: false, password: pw });
      this.closeModal(); this.rerender();
    };
    this.modal(h('div',
      h('h2', editing ? `Password for ${ssid}` : 'Add a network'),
      h('label.label', { for: 'nd-ssid' }, 'Network name'), ssidIn,
      h('label.check', openBox, ' Open network (no password)'),
      h('label.label', { for: 'nd-pw' }, 'Password ', SECRET_TAG()), pwIn,
      h('p.help', 'Test connection makes the board try to join right now with this password, without saving it.'),
      result,
      h('div.row.end', h('button.btn.quiet', { onclick: () => this.closeModal() }, 'Cancel'), testBtn, h('button.btn.primary', { onclick: save }, editing ? 'Use this password' : 'Add to the list'))));
    queueMicrotask(() => (editing || ssid ? pwIn : ssidIn).focus());
  }

  // ------------------------------------------------------------- save
  pendingOps() {
    const ops = [];
    const w = this.keys.find(isWifiList);
    if (w && this.wifiDirty) ops.push({ key: w.key, op: this.wifi.length ? 'set' : 'clear', value: wifiSetValue(this.wifi) });
    for (const [key, e] of this.edits) ops.push(e.kind === 'clear' ? { key, op: 'clear' } : { key, op: 'set', value: e.value });
    return ops;
  }

  renderSavebar() {
    const ops = this.pendingOps();
    const bar = $('#savebar');
    if (!this.connected) return bar.replaceChildren();
    const ble = this.transport?.kind === 'ble';
    bar.replaceChildren(h('div.savebar-inner',
      h('span.savebar-text', { role: 'status' }, ops.length ? h('b', `${ops.length} unsaved change${ops.length > 1 ? 's' : ''}`) : 'No unsaved changes',
        ops.length ? h('span.muted.small', ble ? ' · Saving restarts the board and ends setup mode' : ' · Saving restarts the board') : null),
      h('span.savebar-actions',
        ops.length ? h('button.btn.quiet', { onclick: () => { this.edits.clear(); this.errors.clear(); this.resetWifi(); this.rerender(); } }, 'Discard') : null,
        ops.length ? h('button.btn.primary', { onclick: () => this.save() }, 'Save and restart')
          : ble ? h('button.btn.secondary', { onclick: () => this.save() }, 'Leave setup mode') : null)));
  }

  async save() {
    const ops = this.pendingOps();
    const steps = h('ol.progress');
    const status = h('p', { role: 'status' });
    this.modal(h('div', h('h2', ops.length ? 'Saving' : 'Restarting'), steps, status), { locked: true });
    const step = (t) => { const li = h('li.run', t); steps.append(li); return li; };
    try {
      for (const o of ops) {
        const k = this.keys.find((x) => x.key === o.key);
        const li = step(`${o.op === 'clear' ? 'Clear' : 'Set'} ${k ? keyLabel(k) : o.key}`);
        try {
          await this.client.request(o.op, o.op === 'set' ? { key: o.key, value: o.value } : { key: o.key });
          li.className = 'done';
        } catch (e) {
          li.className = 'fail'; li.append(h('div.error', e.message));
          this.errors.set(o.key, `The board refused this: ${e.message}`);
          throw new Error('Stopped: the board refused a value. Nothing has been saved yet. Fix it and save again.');
        }
      }
      const li = step('Save on the board and restart');
      this.expectRestart = true;
      const r = await this.client.request('commit');
      li.className = 'done';
      if (this.transport.kind === 'ble') return this.finishBle(r, ops.length);
      if (!commitRestarts(r)) {
        this.expectRestart = false;
        await this.loadAll(); this.closeModal(); this.toast('Nothing new to save; the board keeps running.', 'ok');
        return;
      }
      status.replaceChildren(h('span.spinner.inline', { 'aria-hidden': 'true' }), ' Saved. The board is restarting; reconnecting…');
      await this.reconnectAfterRestart(status);
    } catch (e) {
      this.expectRestart = false;
      status.replaceChildren(h('span.error', friendlyError(e, this.transport?.kind)));
      steps.after(h('div.row.end', h('button.btn.secondary', { onclick: () => { this.closeModal(); if (this.connected) this.rerender(); else this.renderConnect(); } }, 'Close')));
      this.unlockModal();
    }
  }

  // Over Bluetooth the board restarts normally after commit, which ends
  // setup mode: there is nothing to reconnect to.
  async finishBle(r, changed) {
    await sleep(1000);   // let the reply's last notifications land
    await this.transport.close();
    this.connected = false; this.expectRestart = false;
    this.keys = []; this.info = null; this.edits.clear();
    this.closeModal();
    this.renderConnect(changed && commitRestarts(r)
      ? 'Saved. The board restarts with the new settings and leaves setup mode. To change anything else, choose Settings → Connect to the setup app on the board again.'
      : 'Done. The board left setup mode.', 'ok');
  }

  async reconnectAfterRestart(status) {
    try {
      await reconnectAfterRestart(this.transport, {
        onStatus: (sec) => status.replaceChildren(h('span.spinner.inline', { 'aria-hidden': 'true' }), ` Waiting for the board to come back… (${sec} s)`),
      });
      this.attachTransport();
      this.expectRestart = false;
      await this.loadAll();
      this.closeModal(); this.toast('Saved. The board restarted with the new settings.', 'ok');
    } catch (e) {
      this.expectRestart = false;
      this.closeModal();
      this.renderConnect(`Saved, but the board didn’t answer on USB within 45 seconds after restarting. Connect again to check. (${e.message})`);
    }
  }

  // ------------------------------------------------------------- reset
  resetCard() {
    const name = this.info?.name || this.info?.id || '';
    const confirmIn = h('input#reset-confirm', { type: 'text', autocomplete: 'off', spellcheck: false, 'aria-describedby': 'reset-hint' });
    const btn = h('button.btn.danger-fill', { disabled: true, onclick: () => this.factoryReset() }, 'Erase settings and restart');
    confirmIn.addEventListener('input', () => { btn.disabled = confirmIn.value.trim().toUpperCase() !== 'RESET'; });
    return h('div.card.danger-zone',
      h('div.card-head', h('h2', 'Factory reset')),
      h('p', 'Erases every setting on this page from ', h('b', name || 'the board'), ': the WiFi networks, the keys and passwords, the MQTT and calendar settings, the name and the time zone. Then the board restarts as if new.'),
      h('p.muted', 'If you ever connected over Bluetooth, also remove the board from that computer’s or phone’s Bluetooth settings afterwards. The board forgets the pairing, and the old one would block the next connection.'),
      h('label.label', { for: 'reset-confirm' }, 'Type RESET to confirm'), h('p.help', { id: 'reset-hint' }, 'This can’t be undone.'), confirmIn, h('div.row', btn));
  }

  async factoryReset() {
    const status = h('p', { role: 'status' }, h('span.spinner.inline', { 'aria-hidden': 'true' }), ' Erasing…');
    this.modal(h('div', h('h2', 'Factory reset'), status), { locked: true });
    try {
      this.expectRestart = true;
      await this.client.request('factory_reset');
      const serial = this.transport.kind === 'serial';
      await sleep(serial ? 0 : 1000);
      await this.transport.close(); this.connected = false; this.expectRestart = false;
      this.keys = []; this.info = null; this.edits.clear();
      this.closeModal();
      this.renderConnect(serial
        ? 'The board was erased and is restarting. Give it a few seconds, then connect again to set it up.'
        : 'The board was erased and is restarting. Remove it from this device’s Bluetooth settings, then set it up again.', 'ok');
    } catch (e) {
      this.expectRestart = false;
      status.replaceChildren(h('span.error', friendlyError(e, this.transport?.kind)));
      this.unlockModal();
      status.after(h('div.row.end', h('button.btn.secondary', { onclick: () => this.closeModal() }, 'Close')));
    }
  }

  // ------------------------------------------------------------- photos
  // Pick pictures, crop each to the board's shape, see it in its four
  // greys, send it. The board's own photos are listed under, with delete.
  photosView() {
    const ph = this.ph, p = this.info.photos;
    if (!ph.list && !ph.loading) queueMicrotask(() => this.loadPhotos());
    const st = ph.storage || this.info.storage;
    const count = ph.list ? ph.list.length : p.count;
    const pick = h('input#ph-files.sr-only', { type: 'file', accept: 'image/*', multiple: true,
      onchange: (e) => { this.addPhotoFiles([...e.target.files]); e.target.value = ''; } });
    const drop = h('label.ph-drop', { for: 'ph-files' }, icon('photo'),
      h('span', h('b', 'Choose photos'), h('span.muted', ' or drop them here')),
      h('span.muted.small', 'JPEG, PNG, WebP or GIF. Each is cropped to the board’s screen and turned into its four greys here, in this page.'));
    drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('over'); });
    drop.addEventListener('dragleave', () => drop.classList.remove('over'));
    drop.addEventListener('drop', (e) => { e.preventDefault(); drop.classList.remove('over'); this.addPhotoFiles([...e.dataTransfer.files]); });
    const full = count >= p.max;
    const used = st ? `${fmtBytes(st.used)} of ${fmtBytes(st.total)} used` : '';
    const items = (ph.list || []).map((e) => this.photoItem(e));
    return h('div.card.photos',
      h('div.card-head', h('h2', 'Photos'), ph.list ? h('button.btn.quiet.small', { onclick: () => this.loadPhotos(true) }, 'Refresh') : null),
      h('p.blurb', 'Photos you send here show in the board’s Photos app, before any on its microSD card. They stay on the board until you delete them.'),
      ph.editor ? ph.editor.el : [full ? h('p.notice.error', `The board holds ${p.max} photos at most. Delete some to add more.`) : null, pick, drop,
        this.transport?.kind === 'ble' ? h('p.muted.small', 'Over Bluetooth a photo takes longer to send than over the USB cable, up to a minute or so.') : null],
      h('div.card-head.ph-head', h('h3', 'On the board'),
        h('span.muted.small', `${count} of ${p.max} photos${used ? ' · ' + used : ''}`)),
      st ? h('div.meter', { role: 'img', 'aria-label': `Storage: ${used}` }, h('i', { style: `width:${Math.min(100, Math.round(st.used / st.total * 100))}%` })) : null,
      ph.list == null ? h('p.muted', h('span.spinner.inline', { 'aria-hidden': 'true' }), ' Asking the board for its photos…')
        : items.length ? h('ul.ph-grid', ...items) : h('p.muted.ph-empty', 'No photos on the board yet. The ones you send appear here.'));
  }

  photoItem(e) {
    const thumb = this.ph.thumbs.get(e.id);
    const armed = this.ph.armed === e.id;
    return h('li.ph-item',
      h('div.ph-thumb', { class: e.orient }, thumb ? levelsCanvas(thumb.levels, thumb.w, thumb.h, 'ph-thumb-img') : h('span.muted.small', '…')),
      h('div.ph-meta', h('b', e.title || e.id), h('span.muted.small', `${e.orient === 'portrait' ? 'Portrait' : 'Landscape'} · ${fmtBytes(e.bytes)}`)),
      h('button.btn.small', { id: `ph-del-${e.id}`, class: armed ? 'danger-fill' : 'quiet danger', disabled: !!this.ph.uploading,
        'aria-label': armed ? `Really delete ${e.title || e.id}` : `Delete ${e.title || e.id}`,
        onclick: () => (armed ? this.deletePhoto(e) : this.armDelete(e.id)) }, armed ? 'Delete it' : 'Delete'));
  }

  armDelete(id) {
    this.ph.armed = id; this.rerender();
    clearTimeout(this.ph.armTimer);
    this.ph.armTimer = setTimeout(() => { if (this.ph.armed === id) { this.ph.armed = null; if (this.section === 'photos') this.rerender(); } }, 4000);
  }

  async loadPhotos(force = false) {
    const ph = this.ph;
    if (ph.loading || (!force && ph.list) || !this.connected) return;
    ph.loading = true;
    try {
      const r = await this.client.request('photo_list');
      ph.list = r.photos || []; ph.storage = r.storage || ph.storage;
      if (this.info?.photos) this.info.photos.count = ph.list.length;
      if (this.section === 'photos') this.rerender();
      for (const e of ph.list) {   // thumbnails one at a time, after the list is up
        if (ph.thumbs.has(e.id) || !this.connected) continue;
        try {
          const t = await this.client.request('photo_thumb', { photo: e.id }, { quiet: true });
          ph.thumbs.set(e.id, { w: t.w, h: t.h, levels: unpack2(fromBase64(t.data), t.w, t.h) });
          const box = document.querySelector(`#ph-del-${e.id}`)?.closest('.ph-item')?.querySelector('.ph-thumb');
          box?.replaceChildren(levelsCanvas(ph.thumbs.get(e.id).levels, t.w, t.h, 'ph-thumb-img'));
        } catch (err) { this.log('warn', `thumbnail of ${e.id}: ${err.message}`); }
      }
    } catch (e) {
      ph.list = ph.list || [];
      this.toast('Couldn’t list the board’s photos: ' + friendlyError(e, this.transport?.kind), 'error');
      if (this.section === 'photos') this.rerender();
    } finally { ph.loading = false; }
  }

  async deletePhoto(e) {
    this.ph.armed = null;
    try {
      const r = await this.client.request('photo_delete', { photo: e.id });
      this.ph.list = this.ph.list.filter((x) => x.id !== e.id); this.ph.thumbs.delete(e.id);
      this.ph.storage = r.storage || this.ph.storage;
      if (this.info?.photos) this.info.photos.count = this.ph.list.length;
      this.toast(`Deleted “${e.title || e.id}” from the board.`, 'ok');
    } catch (err) { this.toast('Couldn’t delete it: ' + friendlyError(err, this.transport?.kind), 'error'); }
    this.rerender();
  }

  addPhotoFiles(files) {
    const pics = files.filter((f) => /^image\//.test(f.type) || /\.(jpe?g|png|webp|gif|bmp|avif)$/i.test(f.name));
    if (!pics.length) return this.toast('Those aren’t pictures this page can read.', 'error');
    this.ph.queue.push(...pics);
    if (!this.ph.editor) this.nextPhoto(); else this.rerender();
  }

  // Opens the next picture of the queue in the editor (or closes it).
  async nextPhoto() {
    const ph = this.ph;
    ph.editor = null;
    const file = ph.queue.shift();
    if (!file) return this.rerender();
    try {
      const work = await decodePicture(file);
      ph.editor = this.photoEditor(file, work);
    } catch (e) {
      this.toast(`Can’t open ${file.name}: this browser can’t read that kind of picture. JPEG, PNG, WebP and GIF work.`, 'error');
      this.log('warn', `decode ${file.name}: ${e.message}`);
      return this.nextPhoto();
    }
    this.rerender();
    ph.editor.draw();
  }

  // The editor for one picture: crop (drag, zoom), orientation, whole
  // picture or fill, brightness, contrast, title; the board's screen as a
  // live preview; Send.
  photoEditor(file, work) {
    const areas = photoAreas(this.info);
    const st = { orient: work.w >= work.h ? 'landscape' : 'portrait', fit: false, zoom: 1, cx: 0.5, cy: 0.5, brightness: 0, contrast: 0, title: titleFromName(file.name) };
    const ed = { file, work, st };
    const crop = () => (st.fit ? { x: 0, y: 0, w: work.w, h: work.h } : cropFor(work.w, work.h, ...areas[st.orient], st));
    const left = this.ph.queue.length;

    // the picture, with the crop on it
    const srcBox = h('div.ph-src');
    const view = h('canvas.ph-crop', { tabindex: 0, role: 'img', 'aria-label': 'The crop: drag it, or use the arrow keys; + and - zoom' });
    const scale = Math.min(1, 640 / Math.max(work.w, work.h));
    view.width = Math.round(work.w * scale); view.height = Math.round(work.h * scale);
    srcBox.append(view);
    const drawCrop = () => {
      const g = view.getContext('2d');
      g.drawImage(work.bitmap, 0, 0, view.width, view.height);
      const c = crop();
      g.fillStyle = 'rgba(20,21,23,.55)';
      const [x, y, w, hh] = [c.x * scale, c.y * scale, c.w * scale, c.h * scale];
      g.fillRect(0, 0, view.width, y); g.fillRect(0, y + hh, view.width, view.height - y - hh);
      g.fillRect(0, y, x, hh); g.fillRect(x + w, y, view.width - x - w, hh);
      g.strokeStyle = '#f2cf3a'; g.lineWidth = 3; g.strokeRect(x + 1.5, y + 1.5, w - 3, hh - 3);
    };
    let drag = null;
    view.addEventListener('pointerdown', (e) => { if (st.fit) return; drag = { x: e.clientX, y: e.clientY, cx: st.cx, cy: st.cy }; view.setPointerCapture(e.pointerId); });
    view.addEventListener('pointermove', (e) => {
      if (!drag) return;
      const r = view.getBoundingClientRect();
      moveTo(drag.cx + (e.clientX - drag.x) / r.width, drag.cy + (e.clientY - drag.y) / r.height);
    });
    view.addEventListener('pointerup', () => { drag = null; });
    view.addEventListener('wheel', (e) => { if (st.fit) return; e.preventDefault(); zoomTo(st.zoom * (e.deltaY < 0 ? 1.1 : 1 / 1.1)); }, { passive: false });
    view.addEventListener('keydown', (e) => {
      const step = 0.02, k = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] }[e.key];
      if (k && !st.fit) { e.preventDefault(); moveTo(st.cx + k[0], st.cy + k[1]); }
      if ((e.key === '+' || e.key === '=') && !st.fit) zoomTo(st.zoom * 1.1);
      if (e.key === '-' && !st.fit) zoomTo(st.zoom / 1.1);
    });
    const moveTo = (cx, cy) => {   // keep the centre where the crop can actually be
      const c = cropFor(work.w, work.h, ...areas[st.orient], { ...st, cx, cy });
      st.cx = (c.x + c.w / 2) / work.w; st.cy = (c.y + c.h / 2) / work.h; changed();
    };
    const zoomTo = (z) => { st.zoom = Math.min(4, Math.max(1, z)); zoomIn.value = String(st.zoom); moveTo(st.cx, st.cy); };

    // the board's screen
    const screen = h('div.screen');
    const device = h('figure.device.ph-device', screen);
    const out = h('canvas.ph-out');
    const bar = h('div.ph-bar', { 'aria-hidden': 'true' });
    screen.append(bar, out);
    const drawPreview = () => {
      const [aw, ah] = areas[st.orient], c = crop();
      const [w, hh] = st.fit ? fitSize(c.w, c.h, aw, ah) : [aw, ah];
      const levels = dither4(adjust(resample(work.grey, work.w, work.h, c, w, hh), st), w, hh);
      out.width = aw; out.height = ah;
      const g = out.getContext('2d');
      g.fillStyle = `rgb(${PANEL_GREYS[3].join(',')})`; g.fillRect(0, 0, aw, ah);
      g.putImageData(new ImageData(levelsToRgba(levels), w, hh), Math.floor((aw - w) / 2), Math.floor((ah - hh) / 2));
      device.classList.toggle('portrait', st.orient === 'portrait');
      bar.textContent = st.orient === 'portrait' ? 'Board  1 / 1' : 'Board photos  1 / 1';
    };
    let queued = false;
    const changed = () => {
      drawCrop();
      if (queued) return;
      queued = true;
      requestAnimationFrame(() => { queued = false; drawPreview(); });
    };
    ed.draw = () => { drawCrop(); drawPreview(); };

    // the choices
    const seg = (name, value, label) => h('label.seg-opt', h('input', { type: 'radio', name: 'ph-orient', value, checked: st.orient === value,
      onchange: () => { st.orient = value; st.zoom = 1; zoomIn.value = '1'; moveTo(st.cx, st.cy); } }), h('span', label));
    const slider = (id, label, min, max, step, key, fmt = (v) => (v > 0 ? `+${v}` : String(v))) => {
      const val = h('output', { for: id }, fmt(st[key]));
      const inp = h('input', { id, type: 'range', min, max, step, value: String(st[key]),
        oninput: (e) => { st[key] = Number(e.target.value); val.textContent = fmt(st[key]); key === 'zoom' ? moveTo(st.cx, st.cy) : changed(); } });
      return [h('label.ph-slider', { for: id }, h('span', label), val), inp];
    };
    const [zoomLabel, zoomIn] = slider('ph-zoom', 'Zoom', 1, 4, 0.05, 'zoom', (v) => `${Math.round(v * 100)}%`);
    const fitBox = h('input#ph-fit', { type: 'checkbox', checked: st.fit, onchange: (e) => {
      st.fit = e.target.checked; zoomIn.disabled = st.fit; view.classList.toggle('fixed', st.fit); changed();
    } });
    const titleIn = h('input#ph-title', { type: 'text', value: st.title, maxlength: 60, autocomplete: 'off', spellcheck: false,
      oninput: (e) => { st.title = e.target.value; } });
    const status = h('div.ph-status', { role: 'status' });
    const sendBtn = h('button.btn.primary', { onclick: () => this.sendPhoto(ed) }, 'Send to the board');
    const skipBtn = h('button.btn.quiet', { onclick: () => { if (!this.ph.uploading) this.nextPhoto(); } }, left ? 'Skip' : 'Cancel');
    ed.status = status; ed.sendBtn = sendBtn; ed.skipBtn = skipBtn;

    ed.el = h('div.ph-editor',
      h('div.card-head', h('h3', left ? `${file.name} (${left} more after this)` : file.name)),
      h('div.ph-cols',
        h('div.ph-col',
          srcBox,
          h('p.muted.small', st.fit ? '' : 'Drag the frame to choose what shows; zoom to show less.'),
          h('fieldset.seg', h('legend.label', 'Made for the board held'), h('div.seg-row', seg('l', 'landscape', 'Landscape'), seg('p', 'portrait', 'Portrait'))),
          h('p.help', 'Held the other way, the board shows the same picture whole, smaller, with white above and below.'),
          photoAreas(this.info).full ? h('p.help', 'Full screen (double-tap the photo on the board) shows a little more above and below, to fill the whole screen.') : null,
          h('label.check', fitBox, ' Show the whole picture, with white bars, instead of filling the screen'),
          h('div.ph-sliders', zoomLabel, zoomIn, ...slider('ph-bright', 'Brightness', -100, 100, 1, 'brightness'), ...slider('ph-contrast', 'Contrast', -100, 100, 1, 'contrast')),
          h('button.btn.link.small', { type: 'button', onclick: () => {
            st.brightness = 0; st.contrast = 0;
            for (const [id, v] of [['ph-bright', 0], ['ph-contrast', 0]]) { const i = ed.el.querySelector('#' + id); i.value = String(v); i.dispatchEvent(new Event('input')); }
          } }, 'Reset brightness and contrast')),
        h('div.ph-col',
          h('p.label', 'On the board'),
          device,
          h('p.muted.small', 'In the board’s four greys, dithered as it will show them.'),
          h('label.label', { for: 'ph-title' }, 'Title'), titleIn,
          h('p.help', 'Shown on this page; the board shows it if the photo can’t be drawn.'))),
      status,
      h('div.row', sendBtn, skipBtn));
    return ed;
  }

  async sendPhoto(ed) {
    if (this.ph.uploading || !this.connected) return;
    const st = ed.st, work = ed.work, areas = photoAreas(this.info);
    const crop = st.fit ? undefined : cropFor(work.w, work.h, ...areas[st.orient], st);
    const built = buildPhoto({ grey: work.grey, sw: work.w, sh: work.h, orient: st.orient, crop, fit: st.fit,
      brightness: st.brightness, contrast: st.contrast, title: st.title, areas, maxBytes: this.info.photos.max_bytes || Infinity });
    const bar = h('progress', { max: built.bytes.length, value: 0 });
    const text = h('span.muted.small', `0 of ${fmtBytes(built.bytes.length)}`);
    const ctl = new AbortController();
    const stop = h('button.btn.quiet.small', { onclick: () => ctl.abort() }, 'Stop');
    ed.status.replaceChildren(h('div.ph-progress', bar, text, stop));
    ed.sendBtn.disabled = true; ed.skipBtn.disabled = true;
    this.ph.uploading = ed;
    const t0 = Date.now();
    const leave = (e) => { e.preventDefault(); e.returnValue = ''; };
    window.addEventListener('beforeunload', leave);
    this.log('info', `sending “${photoTitle(st.title)}”, ${built.bytes.length} bytes`);
    try {
      const r = await uploadPhoto(this.client, built.bytes, {
        signal: ctl.signal, chunk: this.info.photos.chunk || PHOTO_CHUNK,
        onProgress: (at, n) => {
          bar.value = at;
          const secs = (Date.now() - t0) / 1000, rate = at / Math.max(secs, 0.1);
          text.textContent = `${fmtBytes(at)} of ${fmtBytes(n)}${at < n && secs > 2 ? ` · about ${Math.ceil((n - at) / rate)} s left` : ''}`;
        },
      });
      const thumb = built.pics.find((p) => p.kind === PHOTO_KIND.thumb);
      if (r.id) this.ph.thumbs.set(r.id, { w: thumb.w, h: thumb.h, levels: thumb.levels });
      if (r.photo && this.ph.list && !this.ph.list.some((x) => x.id === r.id)) this.ph.list.push(r.photo);
      if (r.storage) this.ph.storage = r.storage;
      if (this.info?.photos && this.ph.list) this.info.photos.count = this.ph.list.length;
      this.log('info', `sent in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
      this.toast(r.exists ? `That photo is on the board already.` : `Sent “${photoTitle(st.title) || 'the photo'}”. The board shows it in Photos.`, 'ok');
      this.ph.uploading = null;
      window.removeEventListener('beforeunload', leave);
      return this.nextPhoto();
    } catch (e) {
      ed.status.replaceChildren(h('p.error', { role: 'alert' }, e.name === 'AbortError' ? 'Stopped. The board dropped what it had of this photo.'
        : `Not sent: ${friendlyError(e, this.transport?.kind)}`));
      ed.sendBtn.disabled = false; ed.skipBtn.disabled = false; ed.sendBtn.textContent = 'Try again';
    }
    this.ph.uploading = null;
    window.removeEventListener('beforeunload', leave);
  }

  // --------------------------------------------------- modal, toast, demo
  modal(content, { locked = false } = {}) {
    const m = $('#modal');
    this.lastFocus = document.activeElement;
    m.replaceChildren(h('div.modal-box', content));
    m.dataset.locked = locked ? '1' : '';
    if (!m.open) m.showModal();
    m.onclick = (e) => { if (e.target === m && !m.dataset.locked) this.closeModal(); };
  }
  unlockModal() { $('#modal').dataset.locked = ''; }
  closeModal() {
    const m = $('#modal'); if (!m || !m.open) return;
    m.close(); m.replaceChildren();
    if (this.lastFocus?.isConnected) this.lastFocus.focus();
  }

  toast(text, kind = 'info') {
    const t = h('div.toast', { class: kind }, text);
    $('#toasts').append(t);
    setTimeout(() => t.remove(), 6000);
  }

  // Demo only: stands in for the OS pairing dialog.
  mockPairPrompt(device) {
    return new Promise((resolve) => {
      const inp = h('input.code#pair-code', { type: 'text', inputmode: 'numeric', maxlength: 6, placeholder: '000000', autocomplete: 'off', 'aria-label': 'Pairing code' });
      const done = (v) => { resolve(v); this.showConnecting(); };
      inp.addEventListener('keydown', (e) => { if (e.key === 'Enter') done(inp.value.trim()); });
      this.panel(h('div.card.waiting',
        h('h2', 'Pairing…'),
        h('div.os-prompt',
          h('p.muted.small', 'Simulated system dialog'),
          h('h3', `Bluetooth pairing request from “${device.name}”`),
          h('p', 'Enter the code shown on the board.'),
          inp,
          h('div.row.end', h('button.btn.quiet', { onclick: () => done(null) }, 'Cancel'), h('button.btn.primary', { onclick: () => done(inp.value.trim()) }, 'Pair'))),
        h('p.muted', 'The code is on the simulated board’s screen.')));
      queueMicrotask(() => inp.focus());
    });
  }
}

// ---------------------------------------------------------------- helpers

function stripResponse(r) { const { id, ok, error, ...rest } = r || {}; return rest.info || rest; }

function hereZone() { try { return Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC'; } catch { return 'UTC'; } }

function offsetLabel(zone) {
  try {
    const p = new Intl.DateTimeFormat('en-GB', { timeZone: zone, timeZoneName: 'shortOffset' }).formatToParts(new Date());
    return (p.find((x) => x.type === 'timeZoneName')?.value || '').replace('GMT', 'UTC') || 'UTC';
  } catch { return ''; }
}

function fmtBytes(n) { return n >= 1048576 ? `${(n / 1048576).toFixed(1)} MB` : `${Math.max(1, Math.round(n / 1024))} KB`; }

// A canvas showing four-grey levels in the panel's greys.
function levelsCanvas(levels, w, hgt, cls) {
  const c = h('canvas.' + cls); c.width = w; c.height = hgt;
  c.getContext('2d').putImageData(new ImageData(levelsToRgba(levels), w, hgt), 0, 0);
  return c;
}

// A picture file to the page's working copy: at most PHOTO_WORK_MAX a side
// (the browser scales it), turned upright by its EXIF, as greys.
async function decodePicture(file) {
  const bmp = await createImageBitmap(file, { imageOrientation: 'from-image' });
  const [w, hgt] = workSize(bmp.width, bmp.height);
  const c = document.createElement('canvas'); c.width = w; c.height = hgt;
  const g = c.getContext('2d', { willReadFrequently: true });
  g.imageSmoothingQuality = 'high';
  g.fillStyle = '#fff'; g.fillRect(0, 0, w, hgt);
  g.drawImage(bmp, 0, 0, w, hgt);
  bmp.close?.();
  return { w, h: hgt, grey: toGrey(g.getImageData(0, 0, w, hgt).data, w, hgt), bitmap: c };
}

function bars(rssi) { return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : 1; }

function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }

// Last, so every class above is initialised before the UI starts.
if (typeof document !== 'undefined' && document.getElementById('root')) boot();
