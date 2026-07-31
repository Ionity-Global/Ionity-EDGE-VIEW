/**
 * The screen, on screen.
 *
 * Two sources, always labelled so you know which you are looking at:
 *   mirror  — real frames off the glass, 160x120, ~5 fps
 *   preview — this page re-drawing the same scene from live data, 60 fps,
 *             costs the device nothing and works while it is off
 */

const PALETTE = [
  '#000000', // 0 black
  '#0000ff', // 1 blue
  '#00ff00', // 2 green
  '#00ffff', // 3 cyan
  '#ff0000', // 4 red
  '#ff00ff', // 5 magenta
  '#ffff00', // 6 yellow
  '#ffffff', // 7 white
];

const RGB = PALETTE.map(h => [
  parseInt(h.slice(1, 3), 16),
  parseInt(h.slice(3, 5), 16),
  parseInt(h.slice(5, 7), 16),
]);

/* ─────────────────────────── mirror ─────────────────────────── */

export class Mirror {
  constructor(canvas, { onStatus = () => {} } = {}) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.ctx.imageSmoothingEnabled = false;
    this.onStatus = onStatus;
    this.es = null;
    this.frames = 0;
    this.lastTick = performance.now();
    this.fps = 0;
  }

  get live() { return this.es !== null; }

  start(base, token) {
    this.stop();
    this.es = new EventSource(`${base}/api/mirror?token=${encodeURIComponent(token)}`);
    this.es.addEventListener('frame', (ev) => {
      try { this.draw(JSON.parse(ev.data)); } catch { /* partial frame */ }
    });
    this.es.onerror = () => this.onStatus('mirror dropped — retrying');
    this.onStatus('mirror connecting…');
  }

  stop() {
    if (this.es) { this.es.close(); this.es = null; }
  }

  /** RLE pairs [count][packedByte], or raw packed. Two pixels per byte, high nibble first. */
  draw({ w, h, fmt, b }) {
    const bin = atob(b);
    const packedLen = (w * h) >> 1;
    const packed = new Uint8Array(packedLen);

    if (fmt === 0) {
      let o = 0;
      for (let i = 0; i + 1 < bin.length && o < packedLen; i += 2) {
        const run = bin.charCodeAt(i);
        const val = bin.charCodeAt(i + 1);
        for (let k = 0; k < run && o < packedLen; k++) packed[o++] = val;
      }
    } else {
      for (let i = 0; i < packedLen && i < bin.length; i++) packed[i] = bin.charCodeAt(i);
    }

    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
      this.ctx.imageSmoothingEnabled = false;
    }

    const img = this.ctx.createImageData(w, h);
    const px = img.data;
    for (let i = 0, p = 0; i < packedLen; i++) {
      const byte = packed[i];
      for (const v of [(byte >> 4) & 7, byte & 7]) {
        const [r, g, bl] = RGB[v];
        px[p++] = r; px[p++] = g; px[p++] = bl; px[p++] = 255;
      }
    }
    this.ctx.putImageData(img, 0, 0);

    this.frames++;
    const now = performance.now();
    if (now - this.lastTick >= 1000) {
      this.fps = this.frames;
      this.frames = 0;
      this.lastTick = now;
      this.onStatus(`live · ${this.fps} fps · ${w}x${h}`);
    }
  }
}

/* ─────────────────────────── preview ─────────────────────────── */

const SLOT_RECTS = {
  header:  { x: 0,   y: 0,   w: 640, h: 35 },
  info_a:  { x: 4,   y: 38,  w: 240, h: 92 },
  info_b:  { x: 248, y: 38,  w: 246, h: 92 },
  info_c:  { x: 498, y: 38,  w: 138, h: 92 },
  stage:   { x: 4,   y: 134, w: 410, h: 212 },
  side:    { x: 418, y: 134, w: 218, h: 212 },
  marquee: { x: 0,   y: 350, w: 640, h: 30 },
  ticker:  { x: 0,   y: 383, w: 640, h: 42 },
  stats:   { x: 0,   y: 429, w: 640, h: 20 },
  footer:  { x: 0,   y: 453, w: 640, h: 27 },
};

const PANEL_STYLE = {
  header:  { border: '#ff0000', title: '',          ink: '#ffffff' },
  clock:   { border: '#00ffff', title: 'TIME',      ink: '#ffff00' },
  network: { border: '#ffffff', title: 'NETWORK',   ink: '#00ff00' },
  feed:    { border: '#ffffff', title: 'SERVER LINK', ink: '#ff00ff' },
  weather: { border: '#ffffff', title: 'WEATHER',   ink: '#00ffff' },
  game:    { border: '#0000ff', title: '',          ink: '#ffff00' },
  verse:   { border: '#ff0000', title: "TODAY'S VERSE", ink: '#ffffff' },
  qr:      { border: '#ffffff', title: 'SCAN ME',   ink: '#ffffff' },
  rotate:  { border: '#ff0000', title: "TODAY'S VERSE", ink: '#ffffff' },
  text:    { border: '#ffffff', title: 'MESSAGE',   ink: '#ffffff' },
  logo:    { border: '#ff0000', title: '',          ink: '#ff0000' },
  marquee: { border: '#ffff00', title: '',          ink: '#ffff00' },
  news:    { border: '#ff0000', title: 'AI NEWS',   ink: '#ffff00' },
  chat:    { border: '#00ff00', title: 'CHAT',      ink: '#00ff00' },
  stats:   { border: '#000000', title: '',          ink: '#00ffff' },
  footer:  { border: '#ff0000', title: '',          ink: '#ffffff' },
  blank:   { border: '#000000', title: '',          ink: '#000000' },
};

export class Preview {
  constructor(canvas) {
    this.canvas = canvas;
    this.canvas.width = 640;
    this.canvas.height = 480;
    this.ctx = canvas.getContext('2d');
    this.layout = {};
    this.data = {};
    this.frame = 0;
    this._loop = this._loop.bind(this);
    requestAnimationFrame(this._loop);
  }

  setLayout(map) { this.layout = { ...map }; }
  setData(key, value) { this.data[key] = String(value); }
  setDataBulk(obj) { Object.assign(this.data, obj); }

  get(key, fallback = '') {
    const v = this.data[key];
    return v === undefined || v === '' ? fallback : v;
  }

  _loop() {
    this.render();
    this.frame++;
    requestAnimationFrame(this._loop);
  }

  render() {
    const c = this.ctx;
    c.fillStyle = '#000';
    c.fillRect(0, 0, 640, 480);
    c.textBaseline = 'top';

    for (const [slot, rect] of Object.entries(SLOT_RECTS)) {
      const panel = this.layout[slot] || 'blank';
      if (panel === 'blank') continue;
      this.drawPanel(panel, rect);
    }

    // The device is 8 colours only; this is a preview of intent, so say so.
    c.fillStyle = 'rgba(0,0,0,0.55)';
    c.fillRect(640 - 96, 2, 94, 14);
    c.fillStyle = '#8a8aa0';
    c.font = '11px monospace';
    c.fillText('PREVIEW', 640 - 90, 4);
  }

  drawPanel(panel, r) {
    const c = this.ctx;
    const st = PANEL_STYLE[panel] || PANEL_STYLE.blank;

    if (panel !== 'marquee' && panel !== 'news' && panel !== 'stats' && panel !== 'footer') {
      c.strokeStyle = st.border;
      c.lineWidth = 1;
      c.strokeRect(r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);
    }

    c.font = 'bold 13px monospace';
    if (st.title) {
      c.fillStyle = st.border;
      c.fillText(st.title, r.x + 6, r.y + 4);
      c.strokeStyle = st.border;
      c.beginPath();
      c.moveTo(r.x + 2, r.y + 20.5);
      c.lineTo(r.x + r.w - 2, r.y + 20.5);
      c.stroke();
    }

    c.fillStyle = st.ink;
    c.font = '12px monospace';
    const body = r.y + (st.title ? 26 : 6);

    switch (panel) {
      case 'header': {
        c.fillStyle = '#ff0000';
        c.fillRect(r.x, r.y, r.w, 3);
        c.fillStyle = '#ffffff';
        c.font = 'bold 16px monospace';
        c.fillText(this.get('hdr.title', 'IO-NITY EDGE-VIEW'), r.x + 40, r.y + 8);
        c.fillStyle = '#8a8aa0';
        c.font = '11px monospace';
        c.fillText(this.get('hdr.sub', 'IONITY GLOBAL (PTY) LTD'), r.x + 40, r.y + 24);
        c.fillStyle = '#ffff00';
        c.font = 'bold 16px monospace';
        c.fillText(this.get('clock', '--:--:--'), r.x + r.w - 90, r.y + 10);
        break;
      }
      case 'clock': {
        c.fillStyle = '#ffff00';
        const size = Math.min(r.h - 30, r.w / 5) | 0;
        c.font = `bold ${size}px monospace`;
        const t = this.get('clock', '--:--:--');
        c.fillText(t, r.x + (r.w - t.length * size * 0.6) / 2, r.y + (r.h - size) / 2 + 4);
        break;
      }
      case 'weather':
        c.fillText(this.get('wx.place', 'CENTURION'), r.x + 7, body);
        c.font = 'bold 26px monospace';
        c.fillText(`${this.get('wx.temp', '--')}C`, r.x + 7, body + 18);
        c.font = '12px monospace';
        c.fillText(this.get('wx.cond', 'CLEAR'), r.x + 7, body + 50);
        break;
      case 'network':
        c.fillText('STATE  CONNECTED', r.x + 7, body);
        c.fillText('IP     ' + this.get('net.ip', '—'), r.x + 7, body + 14);
        c.fillText('PORTS  4242 80 4244', r.x + 7, body + 28);
        break;
      case 'feed':
        c.fillText(`STREAM ${Object.keys(this.data).length} keys`, r.x + 7, body);
        c.fillText('SERVER-DRIVEN', r.x + 7, body + 14);
        break;
      case 'game': {
        c.fillStyle = '#0000ff';
        c.strokeStyle = '#0000ff';
        for (let i = 0; i < 6; i++) c.strokeRect(r.x + 20 + i * 60, r.y + 30, 40, 40);
        const px = r.x + 30 + ((this.frame / 2) % (r.w - 60));
        c.fillStyle = '#ffff00';
        c.beginPath();
        c.arc(px, r.y + r.h / 2, 9, 0.25 * Math.PI, 1.75 * Math.PI);
        c.lineTo(px, r.y + r.h / 2);
        c.fill();
        break;
      }
      case 'verse':
      case 'rotate':
        this.wrap(this.get('verse.text', 'Waiting for the server…'), r.x + 7, body, r.w - 14, 14);
        c.fillStyle = '#ffff00';
        c.fillText(this.get('verse.ref', ''), r.x + 7, r.y + r.h - 18);
        break;
      case 'qr':
        c.fillStyle = '#ffffff';
        for (let y = 0; y < 21; y++)
          for (let x = 0; x < 21; x++)
            if ((x * 7 + y * 3 + ((x * y) % 5)) % 3 === 0)
              c.fillRect(r.x + r.w / 2 - 63 + x * 6, r.y + 30 + y * 6, 6, 6);
        break;
      case 'text':
        c.fillStyle = '#ffff00';
        c.fillText(this.get('text.title', 'MESSAGE'), r.x + 7, body);
        c.fillStyle = '#ffffff';
        this.wrap(this.get('text.body', ''), r.x + 7, body + 16, r.w - 14, 14);
        break;
      case 'logo':
        c.strokeStyle = '#ff0000';
        c.lineWidth = 6;
        c.strokeRect(r.x + r.w / 2 - 40, r.y + r.h / 2 - 40, 80, 80);
        break;
      case 'marquee':
      case 'news': {
        const isNews = panel === 'news';
        if (isNews) {
          c.fillStyle = '#ff0000';
          c.fillRect(r.x, r.y, 76, r.h);
          c.fillStyle = '#ffffff';
          c.font = 'bold 14px monospace';
          c.fillText('AI', r.x + 8, r.y + 6);
          c.fillStyle = '#ffff00';
          c.font = '11px monospace';
          c.fillText('NEWS', r.x + 8, r.y + 22);
        }
        const text = this.get(isNews ? 'marquee' : 'marquee', 'Waiting for the server…');
        const x0 = isNews ? r.x + 86 : r.x + 10;
        const off = (this.frame * 2) % (text.length * 8 + r.w);
        c.save();
        c.beginPath();
        c.rect(x0, r.y, r.w - (x0 - r.x) - 6, r.h);
        c.clip();
        c.fillStyle = '#ffff00';
        c.font = isNews ? 'bold 15px monospace' : '13px monospace';
        c.fillText(text, r.x + r.w - off, r.y + (isNews ? 12 : 8));
        c.restore();
        break;
      }
      case 'chat': {
        const lines = (this.chat || []).slice(-Math.floor((r.h - 30) / 14));
        let y = body;
        for (const { who, text } of lines) {
          c.fillStyle = who === 'ionity-ai' ? '#ff00ff' : '#00ffff';
          c.fillText(who + ':', r.x + 7, y);
          c.fillStyle = who === 'ionity-ai' ? '#ffffff' : '#ffff00';
          c.fillText(text.slice(0, 40), r.x + 7 + (who.length + 2) * 7, y);
          y += 14;
        }
        if (lines.length === 0) {
          c.fillStyle = '#00ffff';
          c.fillText('Say something from the console', r.x + 7, body);
        }
        break;
      }
      case 'stats':
        c.fillStyle = '#00ffff';
        c.fillText(`FPS 60   252 MHz   SERVER-DRIVEN`, r.x + 7, r.y + 4);
        break;
      case 'footer':
        c.fillStyle = '#ff0000';
        c.fillRect(r.x, r.y + r.h - 3, r.w, 3);
        c.fillStyle = '#ffffff';
        c.fillText(this.get('foot.left', 'IONITY.CO.ZA'), r.x + 7, r.y + 6);
        break;
    }
  }

  pushChat(who, text) {
    this.chat = this.chat || [];
    this.chat.push({ who, text });
    while (this.chat.length > 12) this.chat.shift();
  }

  wrap(text, x, y, width, lh) {
    const c = this.ctx;
    const max = Math.floor(width / 7);
    let line = '';
    for (const word of String(text).split(' ')) {
      if ((line + word).length > max) {
        c.fillText(line, x, y);
        y += lh;
        line = '';
      }
      line += word + ' ';
    }
    if (line) c.fillText(line, x, y);
    return y;
  }
}
