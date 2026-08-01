/**
 * Ionity AI — the console's own reasoning layer. No API keys, no round trips:
 * intent parsing, scene composition, health sentinelling and headline curation
 * all run locally in the page. The Gist puller is the inbound channel, so a
 * scene can be driven from anywhere on the internet by editing one file.
 */

export const SLOTS = ['header', 'info_a', 'info_b', 'info_c', 'stage', 'side', 'marquee', 'ticker', 'stats', 'footer'];
export const PANELS = ['header', 'clock', 'network', 'feed', 'weather', 'game', 'verse', 'qr',
  'rotate', 'text', 'logo', 'marquee', 'news', 'stats', 'footer', 'blank'];

export const DEFAULT_LAYOUT = {
  header: 'header', info_a: 'network', info_b: 'feed', info_c: 'weather',
  stage: 'game', side: 'rotate', marquee: 'marquee', ticker: 'news',
  stats: 'stats', footer: 'footer',
};

/* ──────────────────────────── scene composer ──────────────────────────── */

export const SCENES = {
  default: { label: 'Default', layout: DEFAULT_LAYOUT },
  focus: {
    label: 'Focus',
    why: 'One big clock, everything else quiet.',
    layout: { ...DEFAULT_LAYOUT, stage: 'clock', side: 'weather', info_b: 'blank', ticker: 'blank' },
  },
  newswall: {
    label: 'News wall',
    why: 'Headlines take the stage, the crawl doubles up underneath.',
    layout: { ...DEFAULT_LAYOUT, stage: 'news', side: 'weather', info_b: 'clock', ticker: 'news' },
  },
  kiosk: {
    label: 'Kiosk',
    why: 'Scannable QR on the stage for walk-up visitors.',
    layout: { ...DEFAULT_LAYOUT, stage: 'qr', side: 'text', info_a: 'clock', ticker: 'marquee' },
  },
  reflect: {
    label: 'Reflect',
    why: 'Verse front and centre, minimal noise.',
    layout: { ...DEFAULT_LAYOUT, stage: 'verse', side: 'clock', info_b: 'blank', ticker: 'blank', marquee: 'blank' },
  },
  ops: {
    label: 'Ops',
    why: 'Diagnostics everywhere — network, stream health, stats.',
    layout: { ...DEFAULT_LAYOUT, stage: 'stats', side: 'network', info_a: 'network', info_b: 'feed', info_c: 'clock' },
  },
  night: {
    label: 'Night',
    why: 'Dark hours: clock only, no motion to distract.',
    layout: {
      header: 'header', info_a: 'blank', info_b: 'blank', info_c: 'blank',
      stage: 'clock', side: 'blank', marquee: 'blank', ticker: 'blank',
      stats: 'blank', footer: 'footer',
    },
  },
  showcase: {
    label: 'Showcase',
    why: 'Logo and motion — for a stand or a demo table.',
    layout: { ...DEFAULT_LAYOUT, stage: 'game', side: 'logo', info_b: 'clock', ticker: 'news' },
  },
};

/** Pick a scene from the clock. Night is quiet, mornings inform, evenings reflect. */
export function autoScene(date = new Date()) {
  const h = date.getHours();
  if (h >= 23 || h < 6) return 'night';
  if (h < 9) return 'newswall';
  if (h < 17) return 'ops';
  if (h < 21) return 'showcase';
  return 'reflect';
}

/* ─────────────────────────── intent parsing ─────────────────────────── */

const SLOT_WORDS = {
  header: ['header', 'top bar', 'title bar'],
  info_a: ['info a', 'first panel', 'left panel', 'left'],
  info_b: ['info b', 'second panel', 'middle panel', 'centre panel', 'center panel'],
  info_c: ['info c', 'third panel', 'right panel', 'right'],
  stage: ['stage', 'middle', 'centre', 'center', 'main', 'big', 'hero'],
  side: ['side', 'sidebar', 'right side'],
  marquee: ['marquee', 'scroller', 'scrolling bar'],
  ticker: ['ticker', 'news bar', 'bottom bar'],
  stats: ['stats', 'statistics', 'metrics'],
  footer: ['footer', 'bottom'],
};

const PANEL_WORDS = {
  clock: ['clock', 'time'],
  weather: ['weather', 'temperature', 'temp', 'forecast'],
  news: ['news', 'headlines'],
  verse: ['verse', 'scripture', 'bible'],
  qr: ['qr', 'qr code', 'scan code'],
  game: ['game', 'pacman', 'pac-man', 'snake', 'bounce', 'animation'],
  network: ['network', 'wifi', 'ip', 'connection'],
  feed: ['feed', 'stream status', 'link status'],
  stats: ['stats', 'diagnostics', 'metrics'],
  text: ['text', 'message', 'note'],
  logo: ['logo', 'brand'],
  marquee: ['marquee', 'crawl'],
  rotate: ['rotate', 'rotating', 'carousel'],
  blank: ['blank', 'nothing', 'empty', 'clear', 'hide'],
  header: ['header'],
  footer: ['footer'],
};

function matchWord(text, table) {
  let best = null, bestLen = 0;
  for (const [key, words] of Object.entries(table)) {
    for (const w of words) {
      if (text.includes(w) && w.length > bestLen) { best = key; bestLen = w.length; }
    }
  }
  return best;
}

/**
 * Turn a sentence into concrete device actions.
 * Returns { actions:[…], say:'what it decided' }.
 */
export function interpret(raw) {
  const t = ' ' + raw.toLowerCase().trim() + ' ';
  const actions = [];
  const notes = [];

  // Scenes by name.
  for (const [id, s] of Object.entries(SCENES)) {
    if (t.includes(id) || t.includes(s.label.toLowerCase())) {
      if (/(scene|mode|layout|look|preset)/.test(t) || id !== 'default') {
        actions.push({ kind: 'scene', scene: id });
        notes.push(`scene → ${s.label}`);
        break;
      }
    }
  }

  if (/\b(reboot|restart|reset)\b/.test(t)) { actions.push({ kind: 'reboot' }); notes.push('reboot device'); }
  if (/\b(reload|refresh|re-?push|resend)\b/.test(t)) { actions.push({ kind: 'reload' }); notes.push('re-push content'); }
  if (/\b(flash|reflash|firmware|update the board)\b/.test(t)) { actions.push({ kind: 'flash' }); notes.push('open the flasher'); }

  for (const g of ['pacman', 'snake', 'bounce']) {
    if (t.includes(g)) { actions.push({ kind: 'mode', name: g }); notes.push(`game → ${g}`); }
  }
  if (/\b(stop|kill|no) (the )?game\b/.test(t)) { actions.push({ kind: 'mode', name: 'off' }); notes.push('game off'); }

  // "say <something>" / "announce <something>" → the crawl and the glass.
  const say = raw.match(/\b(?:say|announce|post|tell (?:them|everyone))\s+(.{2,140})/i);
  if (say) { actions.push({ kind: 'chat', text: say[1].trim().replace(/^["']|["']$/g, '') }); notes.push('announce'); }

  // "title <something>" / "headline <something>"
  const title = raw.match(/\b(?:title|heading|header text)\s+(?:to\s+)?(.{2,60})/i);
  if (title) { actions.push({ kind: 'data', key: 'hdr.title', value: title[1].trim() }); notes.push('header title'); }

  // "put X in the Y" / "show X on the Y" / bare "X in Y"
  const placement = /(?:put|show|move|place|display)\s+(?:the\s+)?(.{2,24}?)\s+(?:in|on|at|to)\s+(?:the\s+)?(.{2,24}?)(?:\s|$|[.,])/gi;
  let m;
  while ((m = placement.exec(raw)) !== null) {
    const panel = matchWord(m[1].toLowerCase(), PANEL_WORDS);
    const slot = matchWord(m[2].toLowerCase(), SLOT_WORDS);
    if (panel && slot) { actions.push({ kind: 'layout', slot, panel }); notes.push(`${slot} ← ${panel}`); }
  }

  // Fallback: a bare panel + slot mention with no verb.
  if (!actions.length) {
    const panel = matchWord(t, PANEL_WORDS);
    const slot = matchWord(t, SLOT_WORDS);
    if (panel && slot) { actions.push({ kind: 'layout', slot, panel }); notes.push(`${slot} ← ${panel}`); }
    else if (panel) { actions.push({ kind: 'layout', slot: 'stage', panel }); notes.push(`stage ← ${panel}`); }
  }

  return {
    actions,
    say: notes.length ? notes.join(', ') : "I could not find anything to do in that.",
  };
}

/* ──────────────────────────── curator ──────────────────────────── */

const TOPIC_WEIGHTS = {
  ai: 3, 'artificial intelligence': 4, model: 2, llm: 3, openai: 3, anthropic: 3, gemini: 2,
  chip: 2, silicon: 2, robot: 2, agent: 2, 'open source': 2, africa: 3, 'south africa': 4,
  edge: 2, embedded: 2, raspberry: 3, pico: 4, iot: 2, energy: 2, solar: 2,
};

const NOISE = /\b(sponsored|advertisement|deal of the day|shop now|coupon)\b/i;

function tokens(s) { return new Set(s.toLowerCase().replace(/[^a-z0-9 ]/g, ' ').split(/\s+/).filter(w => w.length > 3)); }

function overlap(a, b) {
  let hit = 0;
  for (const t of a) if (b.has(t)) hit++;
  return hit / Math.max(1, Math.min(a.size, b.size));
}

/** Score, de-duplicate and trim headlines before they hit the glass. */
export function curate(headlines, max = 8) {
  const scored = [];
  for (const raw of headlines) {
    const h = String(raw).replace(/\s+/g, ' ').trim();
    if (h.length < 12 || NOISE.test(h)) continue;
    const low = h.toLowerCase();
    let score = Math.min(6, h.length / 40);
    for (const [k, w] of Object.entries(TOPIC_WEIGHTS)) if (low.includes(k)) score += w;
    if (/\b(20\d\d|\d+%|\$\d)/.test(h)) score += 1;      // concrete beats vague
    if (h.endsWith('?')) score -= 1;                      // clickbait tax
    scored.push({ h, score, tk: tokens(h) });
  }
  scored.sort((a, b) => b.score - a.score);

  const kept = [];
  for (const c of scored) {
    if (kept.some(k => overlap(c.tk, k.tk) > 0.6)) continue;
    kept.push(c);
    if (kept.length >= max) break;
  }
  return kept.map(k => k.h);
}

/* ──────────────────────────── sentinel ──────────────────────────── */

/**
 * Watches the link and scores it 0..1. Below 0.5 it proposes a repair, which
 * the console can apply on its own when autopilot is armed.
 */
export class Sentinel {
  constructor() {
    this.reset();
  }

  reset() {
    this.tx = 0; this.rx = 0; this.err = 0;
    this.lastTx = 0; this.lastRx = 0;
    this.bridgeOnline = false;
    this.feeds = false;
    this.since = Date.now();
  }

  note(dir) {
    const now = Date.now();
    if (dir === 'tx') { this.tx++; this.lastTx = now; }
    else if (dir === 'rx') { this.rx++; this.lastRx = now; }
    else if (dir === 'err') this.err++;
  }

  /** @returns {{score:number, label:string, advice:string|null, fix:object|null}} */
  assess() {
    if (!this.bridgeOnline) {
      return { score: 0, label: 'No server', advice: 'Start or install the EDGE-VIEW app.', fix: null };
    }
    const now = Date.now();
    const total = this.tx + this.err;
    const errRate = total ? this.err / total : 0;
    const quietMs = this.lastTx ? now - this.lastTx : now - this.since;

    let score = 1;
    score -= errRate * 0.7;
    if (quietMs > 120000) score -= 0.35;
    else if (quietMs > 60000) score -= 0.15;
    if (!this.feeds) score -= 0.2;
    score = Math.max(0, Math.min(1, score));

    let advice = null, fix = null;
    if (errRate > 0.4 && total > 4) {
      advice = 'Most commands are failing — the device IP is probably stale.';
      fix = { kind: 'rediscover' };
    } else if (!this.feeds) {
      advice = 'Feeds are stopped, so the glass is running on local fallbacks.';
      fix = { kind: 'feeds', on: true };
    } else if (quietMs > 120000) {
      advice = 'Nothing has been sent for two minutes — re-pushing the content.';
      fix = { kind: 'reload' };
    }

    const label = score > 0.85 ? 'Healthy' : score > 0.6 ? 'Degraded' : score > 0.3 ? 'Struggling' : 'Down';
    return { score, label, advice, fix };
  }
}

/* ──────────────────────────── gist inbound ──────────────────────────── */

/**
 * Pull a directive document from a public Gist (or any raw URL) and turn it
 * into actions. The document is plain JSON:
 *
 * {
 *   "title": "…", "chat": ["…"], "news": ["…"],
 *   "data": { "hdr.sub": "…" }, "layout": { "stage": "clock" }, "scene": "focus"
 * }
 *
 * Anything unrecognised is ignored — this input is untrusted.
 */
export async function pullInbound(idOrUrl) {
  const src = String(idOrUrl).trim();
  if (!src) throw new Error('no gist configured');

  let text;
  if (/^https?:\/\//i.test(src)) {
    const u = new URL(src);
    if (!/(githubusercontent\.com|github\.com|api\.github\.com)$/i.test(u.hostname))
      throw new Error('only github/gist URLs are accepted');
    text = await (await fetch(u, { cache: 'no-store' })).text();
  } else {
    const id = src.split('/').pop();
    if (!/^[0-9a-f]{6,64}$/i.test(id)) throw new Error('that does not look like a gist id');
    const meta = await (await fetch(`https://api.github.com/gists/${id}`, { cache: 'no-store' })).json();
    if (!meta.files) throw new Error(meta.message || 'gist not found');
    const file = Object.values(meta.files).find(f => /\.json$/i.test(f.filename)) || Object.values(meta.files)[0];
    text = file.truncated ? await (await fetch(file.raw_url)).text() : file.content;
  }

  let doc;
  try { doc = JSON.parse(text); }
  catch { throw new Error('the gist is not valid JSON'); }

  return { doc, actions: inboundToActions(doc) };
}

const KEY_RE = /^[a-z][a-z0-9._-]{0,23}$/i;

export function inboundToActions(doc) {
  const actions = [];
  if (!doc || typeof doc !== 'object') return actions;

  if (typeof doc.scene === 'string' && SCENES[doc.scene]) actions.push({ kind: 'scene', scene: doc.scene });

  if (doc.layout && typeof doc.layout === 'object') {
    for (const [slot, panel] of Object.entries(doc.layout)) {
      if (SLOTS.includes(slot) && PANELS.includes(panel)) actions.push({ kind: 'layout', slot, panel });
    }
  }

  if (typeof doc.title === 'string') actions.push({ kind: 'data', key: 'hdr.title', value: doc.title.slice(0, 60) });

  if (doc.data && typeof doc.data === 'object') {
    for (const [key, value] of Object.entries(doc.data)) {
      if (KEY_RE.test(key) && (typeof value === 'string' || typeof value === 'number'))
        actions.push({ kind: 'data', key, value: String(value).slice(0, 180) });
    }
  }

  if (Array.isArray(doc.news)) {
    const items = curate(doc.news.filter(x => typeof x === 'string'), 6);
    if (items.length) actions.push({ kind: 'data', key: 'marquee', value: items.join('   •   ').slice(0, 180) });
  }

  if (Array.isArray(doc.chat)) {
    for (const line of doc.chat.slice(0, 8)) {
      if (typeof line === 'string' && line.trim()) actions.push({ kind: 'chat', text: line.trim().slice(0, 140) });
    }
  }

  if (typeof doc.mode === 'string' && ['pacman', 'snake', 'bounce', 'off'].includes(doc.mode))
    actions.push({ kind: 'mode', name: doc.mode });

  return actions;
}
