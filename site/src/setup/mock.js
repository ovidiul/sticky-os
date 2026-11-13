// A simulated reTerminal Sticky for ?demo and for ../../test/run.mjs: the
// setup protocol in memory, framing included. It deliberately doesn't import
// app.js, so the tests check the app against a second, independent
// implementation.
//
// What it simulates:
//  - Bluetooth setup mode: the board advertises only after "Settings →
//    Connect to the setup app", which restarts it into setup mode. A commit
//    or factory reset restarts it normally, which ends setup mode.
//  - pairing: a random 6-digit passkey on its "screen", asked for once per
//    bond (a factory reset forgets the bond)
//  - the ATT MTU (default 185, ?mtu=23 for the worst case): writes longer
//    than MTU-3 fail, and responses are split into MTU-3 byte notifications
//  - staged settings ("set"/"clear") that only reach "flash" on "commit",
//    which then restarts the board
//  - board-side validation errors, a WiFi scan and a test join
//  - the USB-C port: a CH343 bridge whose open resets the board (DTR/RTS)
//    and which stays enumerated while the ESP32 restarts
//  - the board's photo store (photo_* ops): chunked uploads checked by
//    SHA-256 and header, the 100-photo and size caps, a full disk, an
//    upload dropped when it's aborted, restarted or goes quiet
//
// All values here are demo data: no real networks, names or places.

const KEYS = [
  // wifi.networks is secret: "keys" lists its SSIDs (ssids), never a value.
  { key: 'wifi.networks', type: 'wifi_list', secret: true, max_items: 5 },
  { key: 'device.name', type: 'string', secret: false, max_len: 24 },
  { key: 'device.timezone', type: 'string', secret: false, default: 'UTC0', max_len: 64 },
  { key: 'weather.locate', type: 'bool', secret: false, default: true },
  { key: 'weather.place', type: 'string', secret: false, max_len: 40 },
  { key: 'weather.lat', type: 'string', secret: false, max_len: 12 },
  { key: 'weather.lon', type: 'string', secret: false, max_len: 12 },
  { key: 'openai.api_key', type: 'string', secret: true, max_len: 256 },
  { key: 'openai.relay_host', type: 'string', secret: false, max_len: 253 },
  { key: 'openai.relay_port', type: 'int', secret: false, min: 1, max: 65535 },
  { key: 'voice.stt_local', type: 'bool', secret: false, default: false },
  { key: 'notes.stt_model', type: 'string', secret: false, default: 'gpt-4o-mini-transcribe', max_len: 64 },
  { key: 'notes.ai_model', type: 'string', secret: false, default: 'gpt-4.1-mini', max_len: 64 },
  { key: 'mqtt.host', type: 'string', secret: false, max_len: 253 },
  { key: 'mqtt.port', type: 'int', secret: false, min: 1, max: 65535, default: 8883 },
  { key: 'mqtt.tls', type: 'bool', secret: false, default: true },
  { key: 'mqtt.ca', type: 'string', secret: false, max_len: 4000 },
  { key: 'mqtt.user', type: 'string', secret: false, max_len: 64 },
  { key: 'mqtt.password', type: 'string', secret: true, max_len: 128 },
  { key: 'claude.topic', type: 'string', secret: false, default: 'claude/limits', max_len: 128 },
  { key: 'influx.url', type: 'string', secret: false, max_len: 200 },
  { key: 'influx.org', type: 'string', secret: false, max_len: 64 },
  { key: 'influx.bucket', type: 'string', secret: false, max_len: 64 },
  { key: 'influx.token', type: 'string', secret: true, max_len: 200 },
  { key: 'telemetry.enabled', type: 'bool', secret: false, default: false },
  { key: 'update.auto', type: 'bool', secret: false, default: false },
  { key: 'update.url', type: 'string', secret: false, max_len: 200 },
  { key: 'quotes.online', type: 'bool', secret: false, default: true },
  { key: 'games.ai', type: 'bool', secret: false, default: true },
];

const AIR = [   // what the "scan" sees
  { ssid: 'HomeNet', rssi: -48, secure: true },
  { ssid: 'HomeNet-5G', rssi: -61, secure: true },
  { ssid: 'Neighbour', rssi: -79, secure: true },
  { ssid: 'CafeFree', rssi: -83, secure: false },
  { ssid: 'DIRECT-printer', rssi: -70, secure: true },
  { ssid: 'Küche ☕', rssi: -66, secure: true },
];

const STORE = 'sticky-demo-board-v1';
const PHOTO_STORE = 'sticky-demo-photos-v1';
// The board's photo store (firmware photostore.h): the same limits.
export const PHOTO_LIMITS = { max: 100, maxBytes: 200000, chunk: 3072, reserve: 256 * 1024, idleMs: 30000, head: 96,
  total: 20840448, landscape: [800, 436], portrait: [480, 756] };
const MAX_MESSAGE = 8192;
const te = new TextEncoder();
const td = new TextDecoder('utf-8', { fatal: true });
const clone = (v) => (v === undefined ? undefined : JSON.parse(JSON.stringify(v)));

function err(name, message) { const e = new Error(message); e.name = name; return e; }

export class MockDevice {
  constructor({ storage = null, mtu = 185, latency = 0, bootMs = 2500, id = 'sticky-a1b2c3', random = Math.random, setupMode = true } = {}) {
    this.storage = storage; this.mtu = mtu; this.latency = latency; this.bootMs = bootMs;
    this.id = id; this.random = random;
    this.listeners = new Set(); this.link = null; this.booting = false;
    this.hooks = { reboot: new Set(), booted: new Set() };   // the serial mock listens here
    this.flash = this.load();
    this.staged = clone(this.flash.values); this.dirty = false;
    this.setupMode = setupMode;
    this.resetRx();
    this.screen = setupMode ? 'setup' : 'normal'; this.passkey = null;
    this.stats = { framesIn: 0, framesOut: 0, rejectedWrites: 0, requests: [] };
    this.photos = this.loadPhotos(); this.upload = null; this.photoTotal = PHOTO_LIMITS.total;
  }

  // ---- photos ("LittleFS"): id -> { id, bytes: Uint8Array, entry }
  loadPhotos() {
    const m = new Map();
    try {
      for (const [id, b64] of JSON.parse(this.storage?.getItem(PHOTO_STORE) || '[]')) {
        const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
        m.set(id, { id, bytes, entry: photoEntry(id, bytes, parsePhoto(bytes)) });
      }
    } catch { /* none, or damaged: start empty */ }
    return m;
  }
  savePhotos() {
    try {
      const all = [...this.photos.values()].map((p) => [p.id, b64(p.bytes)]);
      this.storage?.setItem(PHOTO_STORE, JSON.stringify(all));
    } catch { /* over the browser's quota: they last until the page is reloaded */ }
  }
  photoUsed() { let n = 16384; for (const p of this.photos.values()) n += Math.ceil(p.bytes.length / 4096) * 4096; return n + (this.upload ? Math.ceil(this.upload.got / 4096) * 4096 : 0); }
  storageInfo() { const used = this.photoUsed(); return { total: this.photoTotal, used, free: this.photoTotal - used }; }
  photoList() { return [...this.photos.values()].map((p) => p.entry).sort((a, b) => a.added - b.added || (a.id < b.id ? -1 : 1)); }
  dropUpload(why) { if (this.upload) { clearTimeout(this.upload.timer); this.upload = null; this.lastDrop = why; } }

  async photoOp(r) {
    const L = PHOTO_LIMITS;
    const fail = (message, extra = {}) => Object.assign(new Error(message), { extra });
    switch (r.op) {
      case 'photo_list': return { photos: this.photoList(), storage: this.storageInfo() };
      case 'photo_thumb': {
        const p = this.photos.get(r.photo); if (!p) throw fail(`no photo ${r.photo} on the board`);
        const t = parsePhoto(p.bytes).pics.find((x) => x.kind === 2); if (!t) throw fail('this photo has no thumbnail');
        return { w: t.w, h: t.h, data: b64(p.bytes.subarray(t.off, t.off + t.len)) };
      }
      case 'photo_begin': {
        if (!Number.isInteger(r.size) || r.size <= L.head || r.size > L.maxBytes) throw fail(`a photo file is ${L.head + 1} to ${L.maxBytes} bytes`);
        if (typeof r.sha256 !== 'string' || !/^[0-9a-f]{64}$/i.test(r.sha256)) throw fail('sha256: 64 hex digits');
        const id = r.sha256.slice(0, 8).toLowerCase();
        if (this.photos.has(id)) return { photo: this.photos.get(id).entry, exists: true };
        this.dropUpload('a new upload started');
        if (this.photos.size >= L.max) throw fail(`The board holds ${L.max} photos at most. Delete some first.`);
        const free = this.storageInfo().free;
        if (free < r.size + L.reserve) throw fail(`Not enough room on the board: ${Math.floor(free / 1024)} KB free, this photo needs ${Math.floor((r.size + L.reserve) / 1024)} KB.`);
        this.upload = { id, sha256: r.sha256.toLowerCase(), size: r.size, got: 0, buf: new Uint8Array(r.size) };
        this.armIdle();
        return { photo: { id }, chunk: L.chunk, next: 0 };
      }
      case 'photo_chunk': {
        const u = this.upload; if (!u) throw fail('no upload on: start with photo_begin');
        if (r.offset !== u.got) throw fail(`expected offset ${u.got}`, { next: u.got });
        let bytes;
        try { bytes = Uint8Array.from(atob(r.data), (c) => c.charCodeAt(0)); } catch { bytes = null; }
        if (!bytes || !bytes.length || bytes.length > L.chunk) throw fail(`data: base64 of 1 to ${L.chunk} bytes`);
        if (u.got + bytes.length > u.size) { this.dropUpload('too much'); throw fail('more data than the size given: upload dropped'); }
        if (this.storageInfo().free < 4096 + bytes.length) { this.dropUpload('full'); throw fail("Couldn't write the photo: the board's storage is full. Upload dropped."); }
        u.buf.set(bytes, u.got); u.got += bytes.length; this.armIdle();
        return { next: u.got };
      }
      case 'photo_end': {
        const u = this.upload; if (!u) throw fail('no upload on');
        this.dropUpload('ended');
        if (u.got !== u.size) throw fail(`only ${u.got} of ${u.size} bytes came: upload dropped`);
        if (await sha256hex(u.buf) !== u.sha256) throw fail('The photo arrived damaged (its SHA-256 differs). Try again.');
        let head;
        try { head = parsePhoto(u.buf); } catch (e) { throw fail(`Not a photo this board can show: ${e.message}`); }
        const p = { id: u.id, bytes: u.buf, entry: photoEntry(u.id, u.buf, head) };
        this.photos.set(u.id, p); this.savePhotos(); this.setScreen(this.screen);
        return { photo: p.entry, storage: this.storageInfo() };
      }
      case 'photo_abort': { const was = !!this.upload; this.dropUpload('aborted'); return { aborted: was }; }
      case 'photo_delete': {
        if (!this.photos.delete(r.photo)) throw fail(`no photo ${r.photo} on the board`);
        this.savePhotos(); this.setScreen(this.screen);
        return { storage: this.storageInfo() };
      }
      default: return null;
    }
  }
  armIdle() {
    const u = this.upload; clearTimeout(u.timer);
    u.timer = setTimeout(() => { if (this.upload === u) this.dropUpload('no data for 30 s'); }, PHOTO_LIMITS.idleMs);
    u.timer.unref?.();
  }

  // ---- persistence ("flash")
  load() {
    try { const s = this.storage?.getItem(STORE); if (s) return JSON.parse(s); } catch { /* fresh */ }
    return { values: {}, bonded: false };
  }
  save() { try { this.storage?.setItem(STORE, JSON.stringify(this.flash)); } catch { /* memory only */ } }
  wipeDemo() { this.flash = { values: {}, bonded: false }; this.staged = {}; this.dirty = false; this.save(); this.setScreen(this.idleScreen()); }

  get name() {
    const n = this.flash.values['device.name'];
    return n ? (n.startsWith('Sticky') ? n : `Sticky ${n}`) : `Sticky-${this.id.slice(-4).toUpperCase()}`;
  }

  // ---- screen
  on(fn) { this.listeners.add(fn); return () => this.listeners.delete(fn); }
  setScreen(s) { this.screen = s; for (const f of this.listeners) f(this); }
  idleScreen() { return this.setupMode ? 'setup' : 'normal'; }

  // On the board: Settings → Connect to the setup app (restarts into setup
  // mode), or leaving it again.
  enterSetup() { this.reboot('sw', { setup: true }); }
  leaveSetup() { this.reboot('sw', { setup: false }); }

  // ---- link (called by MockTransport)
  beginPairing() { this.passkey = String(Math.floor(this.random() * 1e6)).padStart(6, '0'); this.setScreen('pairing'); return this.passkey; }
  endPairing(ok) { this.passkey = null; if (ok) { this.flash.bonded = true; this.save(); } this.setScreen(ok ? 'connected' : this.idleScreen()); }
  attach(link) { this.link = link; this.staged = clone(this.flash.values); this.dirty = false; this.resetRx(); this.setScreen('connected'); }
  detach(link) {
    if (this.link !== link) return;
    this.link = null; this.resetRx(); this.staged = clone(this.flash.values); this.dirty = false;
    if (!this.booting) this.setScreen(this.idleScreen());
  }

  readInfo() { return JSON.stringify(this.info()); }

  info() {
    const nets = this.flash.values['wifi.networks'] || [];
    const up = nets.find((n) => AIR.some((a) => a.ssid === n.ssid));
    const air = up && AIR.find((a) => a.ssid === up.ssid);
    return {
      proto: 1, id: this.id, fw: '0.9.0-demo', commit: '0000000demo', board: 'reterminal-sticky', touch: true,
      wifi: { ssid: up?.ssid || '', connected: !!up, rssi: up ? air.rssi : null },
      pending: this.dirty,
      storage: this.storageInfo(),
      photos: { v: 1, count: this.photos.size, max: PHOTO_LIMITS.max, max_bytes: PHOTO_LIMITS.maxBytes, chunk: PHOTO_LIMITS.chunk,
        landscape: PHOTO_LIMITS.landscape, portrait: PHOTO_LIMITS.portrait },
      keys: KEYS.filter((k) => this.isSet(this.flash.values, k)).map((k) => k.key),
    };
  }

  // ---- framing, the board side: strict, like small firmware would be
  resetRx() { this.rx = []; this.rxLen = 0; this.rxNext = 0; }

  write(frame) {
    this.stats.framesIn++;
    if (frame.length > this.mtu - 3) { this.stats.rejectedWrites++; throw err('NetworkError', 'GATT operation failed for unknown reason.'); }
    if (frame.length < 1) return;
    const last = (frame[0] & 0x80) !== 0, seq = frame[0] & 0x7f;
    if (seq === 0 && this.rxNext !== 0) this.resetRx();   // a first fragment restarts the buffer (but 127 → 0 is a wrap)
    else if (seq !== this.rxNext) { this.resetRx(); return this.reply({ id: null, ok: false, error: `bad sequence ${seq}` }); }
    this.rx.push(frame.slice(1)); this.rxLen += frame.length - 1; this.rxNext = (seq + 1) % 128;
    if (this.rxLen > MAX_MESSAGE) { this.resetRx(); return this.reply({ id: null, ok: false, error: 'message too long' }); }
    if (!last) return;
    const all = new Uint8Array(this.rxLen); let o = 0;
    for (const p of this.rx) { all.set(p, o); o += p.length; }
    this.resetRx();
    let req;
    try { req = JSON.parse(td.decode(all)); } catch { return this.reply({ id: null, ok: false, error: 'bad JSON' }); }
    this.stats.requests.push(req.op);
    Promise.resolve(this.handle(req, 'ble')).then(
      (r) => this.reply({ id: req.id, ok: true, ...r }),
      (e) => this.reply({ id: req.id, ok: false, ...e.extra, error: e.message }));
  }

  reply(msg) {
    const link = this.link; if (!link) return;
    const bytes = te.encode(JSON.stringify(msg));
    const per = this.mtu - 3 - 1;
    const n = Math.max(1, Math.ceil(bytes.length / per));
    const frames = [];
    for (let i = 0; i < n; i++) {
      const c = bytes.subarray(i * per, (i + 1) * per);
      const f = new Uint8Array(c.length + 1); f[0] = (i % 128) | (i === n - 1 ? 0x80 : 0); f.set(c, 1);
      frames.push(f);
    }
    (async () => { for (const f of frames) { if (this.latency) await sleep(this.latency); if (this.link !== link) return; this.stats.framesOut++; link._notify(f); } })();
  }

  // ---- requests
  isSet(vals, k) { const v = vals[k.key]; return v !== undefined && v !== '' && !(Array.isArray(v) && !v.length); }

  // via: 'ble' or 'serial'. Over Bluetooth, commit always ends setup mode.
  async handle(r, via = 'serial') {
    const k = r.key !== undefined ? KEYS.find((x) => x.key === r.key) : null;
    switch (r.op) {
      case 'info': return { info: this.info() };   // nested: info has its own "id" (the board's)
      case 'keys': return {
        keys: KEYS.map((x) => {
          const out = { key: x.key, type: x.type, secret: x.secret, set: this.isSet(this.staged, x) };
          for (const f of ['min', 'max', 'max_len', 'max_items', 'options']) if (x[f] != null) out[f] = x[f];
          if (x.type === 'wifi_list') out.ssids = (this.staged[x.key] || []).map((n) => n.ssid);   // never a password
          else if (!x.secret) out.value = this.staged[x.key] ?? x.default ?? null;
          return out;
        }),
      };
      case 'set': if (!k) throw new Error(`unknown key ${r.key}`); this.staged[k.key] = this.validate(k, r.value); this.dirty = true; return {};
      case 'clear': if (!k) throw new Error(`unknown key ${r.key}`); delete this.staged[k.key]; this.dirty = true; return {};
      case 'wifi_scan': await sleep(this.latency ? 1500 : 0); return { networks: AIR.map((a) => ({ ...a, rssi: a.rssi + Math.round(this.random() * 4 - 2) })) };
      case 'wifi_test': {
        await sleep(this.latency ? 2500 : 0);
        const a = AIR.find((x) => x.ssid === r.ssid);
        if (!a) throw new Error(`no network called "${r.ssid}" in range`);
        if (a.secure && (!r.password || r.password.length < 8)) throw new Error('auth failed: WPA passwords are 8-63 characters');
        if (a.secure && /wrong/i.test(r.password)) throw new Error('auth failed (wrong password?)');
        return { ip: `192.0.2.${50 + AIR.indexOf(a)}` };   // TEST-NET-1: documentation addresses only
      }
      case 'commit':
        if (!this.dirty) {
          if (via === 'ble') setTimeout(() => this.reboot('sw', { setup: false }), 300);
          return { changed: false };
        }
        this.flash.values = clone(this.staged); this.dirty = false; this.save();
        setTimeout(() => this.reboot('sw', { setup: false }), 300);
        return { changed: true, restart: true };
      case 'factory_reset':
        this.flash = { values: {}, bonded: false }; this.staged = {}; this.dirty = false; this.save();
        setTimeout(() => this.reboot('sw', { setup: false }), 300);
        return { restart: true };
      default: {
        const out = r.op?.startsWith('photo_') ? await this.photoOp(r) : null;
        if (out) return out;
        throw new Error(`unknown op ${r.op}`);
      }
    }
  }

  validate(k, v) {
    if (k.type === 'wifi_list') {
      if (!Array.isArray(v)) throw new Error('wifi.networks: expected a list');
      if (v.length > k.max_items) throw new Error(`wifi.networks: at most ${k.max_items}`);
      const old = this.staged['wifi.networks'] || [];
      const seen = new Set();
      return v.map((n, i) => {
        if (!n || typeof n.ssid !== 'string' || !n.ssid || te.encode(n.ssid).length > 32) throw new Error(`wifi.networks[${i}]: ssid of 1..32 bytes`);
        if (seen.has(n.ssid)) throw new Error(`wifi.networks: ${n.ssid} twice`);
        seen.add(n.ssid);
        if (n.password === undefined || n.password === null) {   // "keep the stored password"
          const prev = old.find((o) => o.ssid === n.ssid);
          if (!prev) throw new Error(`wifi.networks: no stored password for ${n.ssid}`);
          return { ssid: n.ssid, password: prev.password };
        }
        if (typeof n.password !== 'string' || (n.password && (n.password.length < 8 || n.password.length > 63))) throw new Error(`wifi.networks[${i}]: password of 8..63 characters (or empty)`);
        return { ssid: n.ssid, password: n.password };
      });
    }
    if (k.type === 'int') {
      if (!Number.isInteger(v)) throw new Error(`${k.key}: a whole number`);
      if ((k.min != null && v < k.min) || (k.max != null && v > k.max)) throw new Error(`${k.key}: ${k.min}..${k.max}`);
      return v;
    }
    if (k.type === 'bool') { if (typeof v !== 'boolean') throw new Error(`${k.key}: true or false`); return v; }
    if (typeof v !== 'string') throw new Error(`${k.key}: text`);
    if (k.max_len && te.encode(v).length > k.max_len) throw new Error(`${k.key}: at most ${k.max_len} bytes`);
    if (k.key === 'openai.api_key' && v && !v.startsWith('sk-')) throw new Error("that doesn't look like an OpenAI key (they start with sk-)");
    if (k.key === 'device.timezone' && !/^[<A-Za-z]/.test(v)) throw new Error('not a POSIX TZ string');
    if ((k.key === 'weather.lat' || k.key === 'weather.lon') && v && !/^-?\d{1,3}(\.\d+)?$/.test(v)) throw new Error(`${k.key}: a number like 38.72`);
    if (k.key === 'mqtt.ca' && v && !v.includes('-----BEGIN CERTIFICATE-----')) throw new Error('mqtt.ca: not a PEM certificate');
    return v;
  }

  // source: 'sw' (commit, factory reset, setup mode) or 'dtr' (the USB port
  // was opened). setup: the mode it boots into; default: unchanged.
  reboot(source = 'sw', { setup } = {}) {
    this.booting = true; this.setScreen('restarting');
    this.dropUpload('restart');
    const l = this.link; this.link = null; this.resetRx();
    l?._dropped();
    for (const f of this.hooks.reboot) f(source);
    clearTimeout(this.bootTimer);
    this.bootTimer = setTimeout(() => {
      this.booting = false; this.staged = clone(this.flash.values); this.dirty = false;
      if (setup !== undefined) this.setupMode = setup;
      this.setScreen(this.idleScreen());
      for (const f of this.hooks.booted) f();
    }, this.bootMs);
  }
}

// Same interface as app.js's BleTransport.
export class MockTransport {
  constructor(device, { passkeyPrompt, latency } = {}) {
    this.kind = 'ble';
    this.mockDevice = device; this.passkeyPrompt = passkeyPrompt;
    this.latency = latency ?? device.latency;
    this.connected = false; this._n = () => {}; this._d = () => {};
  }
  get name() { return this.mockDevice.name; }
  onNotify(cb) { this._n = cb; }
  onDisconnect(cb) { this._d = cb; }
  isGone() { return !this.connected; }
  _notify(f) { this._n(f); }
  _dropped() { if (!this.connected) return; this.connected = false; this._d(); }

  // A board that isn't in setup mode doesn't advertise: the chooser stays
  // empty, and all the person can do is cancel it.
  async choose() {
    await sleep(this.latency ? 300 : 0);
    const d = this.mockDevice;
    if (!d.setupMode || d.booting) throw err('NotFoundError', 'User cancelled the requestDevice() chooser.');
  }

  async connect() {
    const d = this.mockDevice;
    await sleep(this.latency ? 600 : 0);
    if (d.booting || !d.setupMode) throw err('NetworkError', 'Connection attempt failed.');
    if (d.link && d.link !== this) throw err('NetworkError', 'Device is busy with another connection.');
    if (!d.flash.bonded) {
      const key = d.beginPairing();
      const typed = await this.passkeyPrompt?.({ name: d.name, passkey: key });
      if (typed !== key) { d.endPairing(false); throw err('SecurityError', typed == null ? 'Pairing cancelled.' : 'Authentication failed.'); }
      d.endPairing(true);
    }
    this.connected = true;
    d.attach(this);
  }

  async readInfo() { if (!this.connected) throw err('NetworkError', 'GATT Server is disconnected.'); return this.mockDevice.readInfo(); }

  async write(frame) {
    if (!this.connected) throw err('NetworkError', 'GATT Server is disconnected.');
    if (this.latency) await sleep(this.latency);
    this.mockDevice.write(frame);
  }

  close() { if (this.connected) { this.connected = false; this.mockDevice.detach(this); } }
}

// ------------------------------------------------------------- USB serial
// A stand-in for navigator.serial and its SerialPort, wired to the same
// MockDevice, so app.js's real SerialTransport runs against it. Simulates:
//  - log lines on the port all the time, and around every answer
//  - output arriving in arbitrary chunks (lines split across reads)
//  - resetOnOpen: opening the port restarts the board (DTR/RTS through the
//    CH343's auto-reset circuit); requests sent while it boots are lost,
//    then it prints @cfg {"ready":true,...}
//  - dropOnRestart: false for the CH343 (the bridge stays on USB while the
//    ESP32 restarts, so the banner arrives on the open port); true for
//    native USB, where the port dies and comes back as a new SerialPort
//  - silent: firmware without the @cfg handler (never answers)

export const CH343 = { usbVendorId: 0x1a86, usbProductId: 0x55d3 };

export class MockSerial {
  constructor(device, { resetOnOpen = true, dropOnRestart = false, silent = false, logEveryMs = 1200, chunk = 48, replugMs, usb = CH343 } = {}) {
    this.device = device; this.resetOnOpen = resetOnOpen; this.dropOnRestart = dropOnRestart; this.silent = silent;
    this.logEveryMs = logEveryMs; this.chunk = chunk; this.replugMs = replugMs ?? Math.max(50, device.bootMs / 2);
    this.usb = usb;
    this.port = new MockSerialPort(this);
    this.open = null;        // the port that is open, if any
    this.present = true;     // false while "re-enumerating"
    this.stats = { opens: 0, resets: 0, cfgIn: 0, lost: 0, logLines: 0 };
    device.hooks.reboot.add((source) => this._onReboot(source));
    device.hooks.booted.add(() => this._onBooted());
  }

  // navigator.serial
  async requestPort({ filters = [] } = {}) {
    if (filters.length && !filters.some((f) => f.usbVendorId === this.usb.usbVendorId)) throw err('NotFoundError', 'No port selected by the user.');
    if (!this.present) throw err('NotFoundError', 'No port selected by the user.');
    return this.port;
  }
  async getPorts() { return this.present ? [this.port] : []; }

  // ---- the board's side of the wire
  _attach(port) {
    this.open = port; this.stats.opens++; this.rxBuf = '';
    this.device.staged = clone(this.device.flash.values); this.device.dirty = false;
    clearInterval(this.logTimer);
    this.logTimer = setInterval(() => this._emitLines([this._chatter()]), this.logEveryMs);
    this.logTimer.unref?.();
    if (this.resetOnOpen) { this.stats.resets++; this.device.reboot('dtr'); } else this.device.setScreen('usb');
  }
  _detach(port) {
    if (this.open !== port) return;
    this.open = null; clearInterval(this.logTimer);
    if (this.device.screen === 'usb') this.device.setScreen(this.device.idleScreen());
  }

  _chatter() {
    const t = Math.floor(performance.now());
    const pick = ['home: drawn, 3 icons changed', `heap: free ${180000 + Math.floor(this.device.random() * 9000)}`, 'wifi: rssi -52', 'weather: up to date', 'display: no change, skipped', 'battery: 87%'];
    return `[${String(t).padStart(7)}] ${pick[Math.floor(this.device.random() * pick.length)]}`;
  }

  _emitLines(lines) {
    const port = this.open; if (!port) return;
    this.stats.logLines += lines.filter((l) => !l.startsWith('@cfg ')).length;
    const bytes = te.encode(lines.map((l) => l + '\r\n').join(''));
    for (let o = 0; o < bytes.length;) {   // arbitrary chunk boundaries, as USB packets would
      const n = 1 + Math.floor(this.device.random() * this.chunk);
      port._push(bytes.slice(o, o + n)); o += n;
    }
  }

  _fromHost(bytes) {
    this.rxBuf += td.decode(bytes, { stream: true });
    let i;
    while ((i = this.rxBuf.indexOf('\n')) >= 0) {
      const line = this.rxBuf.slice(0, i).replace(/\r$/, ''); this.rxBuf = this.rxBuf.slice(i + 1);
      if (!line.startsWith('@cfg ')) continue;
      this.stats.cfgIn++;
      if (this.silent || this.device.booting) continue;   // lost while booting
      let req;
      try { req = JSON.parse(line.slice(5)); } catch { this._emitLines(['@cfg {"id":null,"ok":false,"error":"bad JSON"}']); continue; }
      this.device.stats.requests.push(req.op);
      Promise.resolve(this.device.handle(req, 'serial')).then(
        (r) => ({ id: req.id, ok: true, ...r }), (e) => ({ id: req.id, ok: false, ...e.extra, error: e.message }),
      ).then((msg) => this._emitLines([this._chatter(), `@cfg ${JSON.stringify(msg)}`, this._chatter()]));
    }
  }

  _onReboot(source) {
    const port = this.open;
    if (!port) return;
    if (source === 'sw' && this.dropOnRestart) {
      // native USB: the open port dies, and the board comes back as a new SerialPort
      this.stats.lost++;
      this._detach(port); port._lose();
      this.present = false;
      setTimeout(() => { this.port = new MockSerialPort(this); this.present = true; }, this.replugMs);
      return;
    }
    this._emitLines(['ESP-ROM:esp32s3-20210327', `rst:${source === 'dtr' ? '0x1 (POWERON)' : '0xc (RTC_SW_CPU_RST)'},boot:0x8 (SPI_FAST_FLASH_BOOT)`, 'entry 0x403c8918']);
  }

  _onBooted() {
    if (!this.open || this.silent) return;
    this.device.setScreen('usb');
    const i = this.device.info();
    this._emitLines(['[    812] settings: loaded from NVS', `@cfg ${JSON.stringify({ ready: true, max: MAX_MESSAGE, proto: i.proto, id: i.id, fw: i.fw, board: i.board })}`]);
  }
}

class MockSerialPort {
  constructor(bus) { this.bus = bus; this.readable = null; this.writable = null; this.lost = false; this.listeners = new Set(); this.signals = []; }
  getInfo() { return { ...this.bus.usb }; }
  addEventListener(type, fn) { if (type === 'disconnect') this.listeners.add(fn); }
  removeEventListener(type, fn) { this.listeners.delete(fn); }

  async open({ baudRate } = {}) {
    if (this.lost || !this.bus.present || this.bus.port !== this) throw err('NetworkError', 'Failed to open serial port.');
    if (this.readable) throw err('InvalidStateError', 'The port is already open.');
    if (baudRate !== 115200) throw err('TypeError', 'mock expects 115200 baud');
    this.readable = new ReadableStream({ start: (c) => { this.ctrl = c; }, cancel: () => { this.ctrl = null; } });
    this.writable = new WritableStream({ write: (chunk) => { if (this.lost) throw err('NetworkError', 'The device has been lost.'); this.bus._fromHost(chunk); } });
    this.bus._attach(this);
  }
  async setSignals(s) { this.signals.push(s); }
  async close() {
    if (!this.readable) throw err('InvalidStateError', 'The port is already closed.');
    try { this.ctrl?.close(); } catch { /* cancelled */ }
    this.ctrl = null; this.readable = null; this.writable = null;
    this.bus._detach(this);
  }
  _push(bytes) { try { this.ctrl?.enqueue(bytes); } catch { /* closed */ } }
  _lose() {
    this.lost = true;
    try { this.ctrl?.error(err('NetworkError', 'The device has been lost.')); } catch { /* closed */ }
    this.ctrl = null; this.readable = null; this.writable = null;
    for (const f of this.listeners) f({ type: 'disconnect', target: this });
  }
}

let sharedS;
export function sharedSerial(device, opts) { return (sharedS ??= new MockSerial(device, opts)); }

// One board per page, so a "restart" keeps the same simulated board.
let shared;
export function sharedDevice(opts) { return (shared ??= new MockDevice({ latency: 8, ...opts, mtu: opts.mtu || 185 })); }

// The simulated board, drawn like the site's device frames, with the two
// things you would do on the real one.
export function mountScreen(device, parent) {
  parent.hidden = false;
  const draw = () => {
    const d = device;
    const pk = d.passkey || '';
    const now = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    const photosLine = `<div class="ms-small">${d.photos.size} photo${d.photos.size === 1 ? '' : 's'} on the board</div>`;
    const body = {
      normal: () => `<div class="ms-small">Home</div><div class="ms-big">${now}</div><div>Running normally</div>${photosLine}`,
      setup: () => `<div class="ms-small">Setup mode</div><div class="ms-big">${esc(d.name)}</div><div>Bluetooth on. Connect from the setup app.</div>`,
      pairing: () => `<div class="ms-small">Pairing code</div><div class="ms-code">${pk.slice(0, 3)} ${pk.slice(3)}</div><div>Type it on your computer or phone</div>`,
      connected: () => `<div class="ms-small">Setup mode</div><div class="ms-big">Connected</div><div>to the setup app</div>`,
      restarting: () => '<div class="ms-big">Restarting…</div>',
      usb: () => `<div class="ms-small">USB</div><div class="ms-big">Connected</div><div>to the setup app</div>${photosLine}`,
    }[d.screen]();
    const action = d.booting ? '' : d.setupMode
      ? '<button type="button" class="btn quiet small ms-leave">Leave setup mode</button>'
      : '<button type="button" class="btn secondary small ms-setup">Settings → Connect to the setup app</button>';
    parent.innerHTML = `<p class="ms-label">Simulated board</p>
      <figure class="device ms-device"><div class="screen"><div class="ms-panel" role="status" aria-live="polite">${body}</div></div></figure>
      <div class="ms-actions">${action}<button type="button" class="btn link small ms-wipe">Wipe the demo board</button></div>`;
    parent.querySelector('.ms-setup')?.addEventListener('click', () => device.enterSetup());
    parent.querySelector('.ms-leave')?.addEventListener('click', () => device.leaveSetup());
    parent.querySelector('.ms-wipe').addEventListener('click', () => { if (confirm('Forget everything the demo board has stored?')) { device.wipeDemo(); location.reload(); } });
  };
  device.on(draw); draw();
  return parent;
}

// The photo file's header (firmware photostore.h), checked as the board does.
export function parsePhoto(b) {
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (b.length < PHOTO_LIMITS.head || td.decode(b.subarray(0, 4)) !== 'STKP') throw new Error('not a photo file');
  if (b[4] !== 1) throw new Error('a photo file of a newer version');
  const n = b[5];
  if (n < 1 || n > 4 || v.getUint16(6, true) !== PHOTO_LIMITS.head) throw new Error('a damaged photo header');
  let off = PHOTO_LIMITS.head; const pics = [];
  for (let i = 0; i < n; i++) {
    const o = 12 + i * 8, kind = b[o], orient = b[o + 1], w = v.getUint16(o + 2, true), h = v.getUint16(o + 4, true);
    if (kind > 2 || orient > 1 || w < 1 || h < 1 || w > 800 || h > 800) throw new Error('a damaged photo header');
    if (kind === 2 && (w > 160 || h > 160)) throw new Error('the thumbnail is too large');
    if ((kind === 0) !== (i === 0)) throw new Error('the photo must come first, once');
    const len = Math.ceil(w / 4) * h; pics.push({ kind, orient, w, h, off, len }); off += len;
  }
  if (off !== b.length) throw new Error("the file's size doesn't match its header");
  const t = b.subarray(44, 91), z = t.indexOf(0);
  let title; try { title = td.decode(z < 0 ? t : t.subarray(0, z)); } catch { title = ''; }
  return { added: v.getUint32(8, true), pics, title };
}

function photoEntry(id, bytes, head) {
  const p = head.pics[0];
  return { id, title: head.title, bytes: bytes.length, w: p.w, h: p.h, orient: p.orient ? 'portrait' : 'landscape', added: head.added };
}

function b64(bytes) { let s = ''; for (let i = 0; i < bytes.length; i += 8192) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 8192)); return btoa(s); }

async function sha256hex(bytes) {
  const d = new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', bytes));
  return Array.from(d, (x) => x.toString(16).padStart(2, '0')).join('');
}

function esc(s) { return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }
