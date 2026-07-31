/**
 * Client for the EDGE-VIEW Studio bridge (http://127.0.0.1:8787).
 *
 * The console is served over HTTPS from GitHub Pages; browsers exempt
 * loopback from mixed-content blocking, so this works from the public page.
 */

const BASE = 'http://127.0.0.1:8787';
const TOKEN_KEY = 'edgeview.token';

const bus = new EventTarget();

export const bridge = {
  base: BASE,
  online: false,
  info: null,
  events: bus,

  get token() { return localStorage.getItem(TOKEN_KEY) || ''; },
  set token(v) { v ? localStorage.setItem(TOKEN_KEY, v) : localStorage.removeItem(TOKEN_KEY); },

  emit(type, detail) { bus.dispatchEvent(new CustomEvent(type, { detail })); },

  /** Is a server there at all? Does not need the token. */
  async probe() {
    try {
      const r = await fetch(`${BASE}/api/hello`, { cache: 'no-store', signal: AbortSignal.timeout(2500) });
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      this.info = await r.json();
      this.online = true;
      this.emit('bridge', { online: true, info: this.info });
      return this.info;
    } catch (e) {
      this.online = false;
      this.info = null;
      this.emit('bridge', { online: false, error: String(e.message || e) });
      return null;
    }
  },

  async call(path, body) {
    if (!this.token) throw new Error('not paired — enter the token from Studio');
    const r = await fetch(BASE + path, {
      method: body === undefined ? 'GET' : 'POST',
      headers: { 'Content-Type': 'application/json', 'X-Edgeview-Token': this.token },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(9000),
    });
    if (r.status === 401) { this.emit('unpaired'); throw new Error('pairing token rejected'); }
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    return r.json();
  },

  // ── convenience ──
  state:    ()               => bridge.call('/api/state'),
  raw:      (json)           => bridge.call('/api/command', { json }),
  data:     (key, value)     => bridge.call('/api/data', { key, value: String(value) }),
  layout:   (slot, panel)    => bridge.call('/api/layout', { slot, panel }),
  wifi:     (ssid, pass)     => bridge.call('/api/wifi', { ssid, pass }),
  wifiReset:()               => bridge.call('/api/wifi', { reset: true }),
  reboot:   ()               => bridge.call('/api/reboot', {}),
  reload:   ()               => bridge.call('/api/reload', {}),
  mode:     (name)           => bridge.call('/api/mode', { name }),
  setDevice:(ip)             => bridge.call('/api/device', { ip }),
  feeds:    (on)             => bridge.call('/api/feeds', { on }),
  chat:     (author, text)   => bridge.call('/api/chat', { author, text }),

  // The AI lives on the server so autopilot and inbound keep running with no
  // browser open. These just steer it.
  aiState:  ()               => bridge.call('/api/ai'),
  aiConfig: (patch)          => bridge.call('/api/ai', patch),
  aiSay:    (text)           => bridge.call('/api/ai/say', { text }),
  aiScene:  (scene)          => bridge.call('/api/ai/scene', { scene }),
  aiInbound:()               => bridge.call('/api/ai/inbound', {}),

  // Local chat brain
  brain:    ()               => bridge.call('/api/brain'),
  brainSet: (patch)          => bridge.call('/api/brain', patch),
  say:      (who, text)      => bridge.call('/api/brain/say', { who, text }),
  browse:   (url)            => bridge.call('/api/browse', { url }),

  /** Live event feed: stream, chat, log, device, status. Auto-reconnects. */
  listen() {
    if (this._es) { this._es.close(); this._es = null; }
    if (!this.token) return;
    const es = new EventSource(`${BASE}/api/events?token=${encodeURIComponent(this.token)}`);
    this._es = es;
    for (const name of ['hello', 'stream', 'chat', 'log', 'device', 'status', 'ai', 'token']) {
      es.addEventListener(name, (ev) => {
        let d = null;
        try { d = JSON.parse(ev.data); } catch { d = { raw: ev.data }; }
        this.emit(name, d);
      });
    }
    es.onopen = () => { this.online = true; this.emit('bridge', { online: true, info: this.info }); };
    es.onerror = () => {
      // Let the probe loop decide when to come back instead of reconnecting
      // into a dead port every few seconds.
      this.stop();
      this.online = false;
      this.emit('bridge', { online: false, error: 'event stream dropped' });
    };
  },

  get listening() { return !!this._es; },

  stop() { if (this._es) { this._es.close(); this._es = null; } },
};
