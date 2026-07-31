import { bridge } from './bridge.js';
import { Crawl } from './chat.js';
import {
  SLOTS, PANELS, SCENES, DEFAULT_LAYOUT, autoScene,
  interpret, curate, Sentinel, pullInbound,
} from './ai.js';
import {
  usbSupported, driveSupported, serialSupported,
  flashOverUsb, flashViaDrive, rebootToBootsel, parseUf2,
} from './picoboot.js';
import { Mirror, Preview } from './view.js';

const $ = (id) => document.getElementById(id);
const sentinel = new Sentinel();
const crawl = new Crawl($('crawlLine'));
const preview = new Preview($('previewCanvas'));
const mirror = new Mirror($('mirrorCanvas'), { onStatus: s => { $('viewStatus').textContent = s; } });

let streamPaused = false;
let streamCount = 0;
let fwManifest = [];
let localUf2 = null;

/* ───────────────────────────── stream box ───────────────────────────── */

function line(dir, text) {
  if (streamPaused) return;
  const box = $('streamBox');
  const filter = $('streamFilter').value.trim().toLowerCase();
  const show = { tx: $('chkTx').checked, rx: $('chkRx').checked, log: $('chkLog').checked };
  if ((dir === 'tx' || dir === 'rx' || dir === 'err') && !show[dir === 'err' ? 'tx' : dir]) return;
  if (dir === 'log' && !show.log) return;
  if (filter && !String(text).toLowerCase().includes(filter)) return;

  const el = document.createElement('div');
  el.className = 'l';
  el.innerHTML =
    `<span class="t">${new Date().toLocaleTimeString([], { hour12: false })}</span>` +
    `<span class="d ${dir}">${dir}</span><span class="p"></span>`;
  el.lastChild.textContent = text;

  const stick = box.scrollTop + box.clientHeight > box.scrollHeight - 40;
  box.appendChild(el);
  while (box.childElementCount > 400) box.firstChild.remove();
  if (stick) box.scrollTop = box.scrollHeight;

  streamCount++;
}

setInterval(() => { $('streamRate').textContent = `${streamCount} /min`; streamCount = 0; }, 60000);

function ai(msg) {
  const log = $('aiLog');
  const el = document.createElement('div');
  el.innerHTML = '<b>AI</b> ';
  el.appendChild(document.createTextNode(msg));
  log.appendChild(el);
  while (log.childElementCount > 60) log.firstChild.remove();
  log.scrollTop = log.scrollHeight;
  line('ai', msg);
}

function pill(id, state, text) {
  const p = $(id);
  p.dataset.state = state;
  if (text) p.textContent = text;
}

/* ───────────────────────────── bridge wiring ───────────────────────────── */

bridge.events.addEventListener('bridge', (e) => {
  const { online, info, error } = e.detail;
  sentinel.bridgeOnline = online;
  pill('pillBridge', online ? 'on' : 'err', online ? 'Bridge online' : 'Bridge offline');
  const s = $('serverState');
  if (online) {
    s.className = 'state ok';
    s.textContent = `${info.product} v${info.version} · device ${info.device} · feeds ${info.feeds ? 'running' : 'stopped'}`;
    sentinel.feeds = !!info.feeds;
    $('btnFeeds').textContent = `🔄 Feeds: ${info.feeds ? 'on' : 'off'}`;
    if (info.device) { $('ipInput').value = info.device; pill('pillDevice', 'on', info.device); }
  } else {
    s.className = 'state err';
    s.textContent = `No server on 127.0.0.1:8787 — ${error || 'not running'}. Open EDGE-VIEW Studio → Control → Web Console Bridge.`;
  }
});

bridge.events.addEventListener('stream', (e) => {
  const { dir, payload } = e.detail;
  sentinel.note(dir);
  line(dir, payload);

  // Keep the preview in step with whatever the server is sending.
  try {
    const cmd = JSON.parse(payload);
    if (cmd.type === 'data' && cmd.key) preview.setData(cmd.key, cmd.value ?? '');
    else if (cmd.type === 'layout' && cmd.slot) {
      preview.setLayout({ ...preview.layout, [cmd.slot]: cmd.panel });
      if (selects[cmd.slot] && PANELS.includes(cmd.panel)) selects[cmd.slot].value = cmd.panel;
    } else if (cmd.type === 'chat' && cmd.text) preview.pushChat(cmd.who || 'web', cmd.text);
  } catch { /* not a command we mirror */ }
});

bridge.events.addEventListener('log', (e) => line('log', e.detail.line));

bridge.events.addEventListener('ai', (e) => {
  const { say, applied } = e.detail;
  ai(`${say}${applied ? ` (${applied} applied)` : ''}`);
});

bridge.events.addEventListener('chat', (e) => {
  const { author, text } = e.detail;
  crawl.push(author && author !== 'history' ? `${author}: ${text}` : text);
  if (author && author !== 'history') preview.pushChat(author, text);
});

bridge.events.addEventListener('device', (e) => {
  const { name, ip } = e.detail;
  pill('pillDevice', 'on', ip);
  const list = $('deviceList');
  if ([...list.children].some(li => li.dataset.ip === ip)) return;
  const li = document.createElement('li');
  li.dataset.ip = ip;
  li.textContent = `▸ ${name} @ ${ip}`;
  li.onclick = () => { $('ipInput').value = ip; setIp(); };
  list.appendChild(li);
});

bridge.events.addEventListener('status', (e) => {
  sentinel.feeds = !!e.detail.feeds;
  $('btnFeeds').textContent = `🔄 Feeds: ${e.detail.feeds ? 'on' : 'off'}`;
});

bridge.events.addEventListener('unpaired', () => {
  pill('pillBridge', 'err', 'Token rejected');
  $('serverState').className = 'state err';
  $('serverState').textContent = 'Pairing token rejected — copy a fresh one from Studio → Control.';
});

/** Wrap a bridge call so every failure lands in the log instead of the void. */
async function guard(what, fn) {
  try {
    const r = await fn();
    line('log', `${what}: ok`);
    return r;
  } catch (err) {
    line('err', `${what}: ${err.message}`);
    return null;
  }
}

/* ───────────────────────────── server card ───────────────────────────── */

$('btnPair').onclick = async () => {
  bridge.token = $('tokenInput').value.trim();
  await bridge.probe();
  bridge.listen();
  if (bridge.online) { await refreshState(); await refreshAi(); ai('Paired with the server. Its AI is now driving the glass.'); }
};

$('btnProbe').onclick = () => bridge.probe();
$('btnDetach').onclick = () => { bridge.token = ''; bridge.stop(); $('tokenInput').value = ''; pill('pillBridge', 'off', 'Unpaired'); };

$('btnStartServer').onclick = async () => {
  // Studio registers the edgeview: scheme at install time; if it is not there
  // the browser silently does nothing, so fall back to the download page.
  const before = Date.now();
  location.href = 'edgeview://start';
  setTimeout(async () => {
    const found = await bridge.probe();
    if (!found && Date.now() - before < 60000) {
      $('serverState').className = 'state err';
      $('serverState').textContent = 'Could not launch the server from the browser — install it from the download button, then start the bridge in Studio.';
    }
  }, 2500);
};

$('btnFeeds').onclick = async () => {
  const on = !$('btnFeeds').textContent.includes('on');
  const r = await guard('feeds', () => bridge.feeds(on));
  if (r) $('btnFeeds').textContent = `🔄 Feeds: ${r.feeds ? 'on' : 'off'}`;
};

async function refreshState() {
  const s = await guard('state', () => bridge.state());
  if (!s) return;
  if (s.device) $('ipInput').value = s.device;
  for (const c of s.chat || []) crawl.push(c);
}

/* ───────────────────────────── device card ───────────────────────────── */

async function setIp() {
  const ip = $('ipInput').value.trim();
  if (!ip) return;
  const r = await guard('set device', () => bridge.setDevice(ip));
  if (r) pill('pillDevice', 'on', r.device);
}

$('btnSetIp').onclick = setIp;

$('btnReset').onclick = async () => {
  if (!confirm('Reset the display now? It reboots and rejoins WiFi in a few seconds.')) return;
  await guard('reset', () => bridge.reboot());
  ai('Device reset. It will be back on the network shortly.');
};

$('btnReload').onclick = async () => {
  await guard('reload', () => bridge.reload());
  ai('Re-pushed the live content into every panel.');
};

$('btnPing').onclick = async () => {
  const r = await guard('ping', () => bridge.raw('{"type":"ping"}'));
  if (r) pill('pillDevice', r.ok ? 'on' : 'err', r.ok ? $('ipInput').value : 'No reply');
};

document.querySelectorAll('[data-mode]').forEach(b => {
  b.onclick = () => guard(`mode ${b.dataset.mode}`, () => bridge.mode(b.dataset.mode));
});

/* ───────────────────────────── raw send ───────────────────────────── */

$('btnRawSend').onclick = async () => {
  const json = $('rawInput').value.trim();
  if (!json) return;
  try { JSON.parse(json); } catch { line('err', 'that is not valid JSON'); return; }
  await guard('raw', () => bridge.raw(json));
};
$('rawInput').addEventListener('keydown', e => { if (e.key === 'Enter') $('btnRawSend').click(); });

$('btnStreamPause').onclick = (e) => {
  streamPaused = !streamPaused;
  e.target.textContent = streamPaused ? '▶ Resume' : '⏸ Pause';
};
$('btnStreamClear').onclick = () => { $('streamBox').innerHTML = ''; };

/* ───────────────────────────── wifi card ───────────────────────────── */

$('btnWifi').onclick = async () => {
  const ssid = $('ssidInput').value.trim();
  if (!ssid) { $('wifiState').textContent = 'Enter the SSID first.'; return; }
  if (!confirm(`Move the display to "${ssid}"? If the details are wrong you will need physical access to recover it.`)) return;
  const r = await guard('wifi', () => bridge.wifi(ssid, $('passInput').value));
  $('passInput').value = '';
  $('wifiState').className = 'state ' + (r?.ok ? 'ok' : 'err');
  $('wifiState').textContent = r?.ok ? `Saved. Rejoining on ${ssid}…` : 'Failed to send the credentials.';
};

$('btnWifiReset').onclick = async () => {
  if (!confirm('Forget the saved WiFi? The device reboots onto its build-time network, or its own setup access point if it has none — you must be in range to recover it.')) return;
  const r = await guard('wifi reset', () => bridge.wifiReset());
  $('wifiState').className = 'state ' + (r?.ok ? 'ok' : 'err');
  $('wifiState').textContent = r?.ok ? 'Credentials cleared. Device rebooting…' : 'Failed to clear the credentials.';
  if (r?.ok) ai('WiFi forgotten. The device is rebooting — it may come back on a different network.');
};

/* ───────────────────────────── scene card ───────────────────────────── */

const selects = {};
for (const slot of SLOTS) {
  const label = document.createElement('label');
  label.textContent = slot;
  const sel = document.createElement('select');
  for (const p of PANELS) sel.add(new Option(p, p));
  sel.value = DEFAULT_LAYOUT[slot] || 'blank';
  sel.onchange = () => guard(`${slot}←${sel.value}`, () => bridge.layout(slot, sel.value));
  selects[slot] = sel;
  $('slotGrid').append(label, sel);
}

$('btnApplyLayout').onclick = async () => {
  for (const slot of SLOTS) await guard(`${slot}`, () => bridge.layout(slot, selects[slot].value));
  ai('Layout applied to all ten slots.');
};

$('btnReadLayout').onclick = async () => {
  const r = await guard('read layout', () => bridge.raw('{"type":"query","what":"layout"}'));
  if (!r?.reply) return;
  try {
    const doc = JSON.parse(r.reply);
    for (const [slot, panel] of Object.entries(doc.slots || {})) {
      if (selects[slot] && PANELS.includes(panel)) selects[slot].value = panel;
    }
    ai('Read the live layout back off the device.');
  } catch { line('err', 'the device replied with something I could not parse'); }
};

/* ───────────────────────────── AI card ───────────────────────────── */

async function applyActions(actions) {
  for (const a of actions) {
    switch (a.kind) {
      case 'scene': await applyScene(a.scene); break;
      case 'layout':
        if (selects[a.slot]) selects[a.slot].value = a.panel;
        await guard(`${a.slot}←${a.panel}`, () => bridge.layout(a.slot, a.panel));
        break;
      case 'data': await guard(`data ${a.key}`, () => bridge.data(a.key, a.value)); break;
      case 'mode': await guard(`mode ${a.name}`, () => bridge.mode(a.name)); break;
      case 'reboot': await guard('reboot', () => bridge.reboot()); break;
      case 'reload': await guard('reload', () => bridge.reload()); break;
      case 'chat':
        await guard('chat', () => bridge.chat('ionity-ai', a.text));
        crawl.push(`ionity-ai: ${a.text}`);
        break;
      case 'feeds': await guard('feeds', () => bridge.feeds(a.on)); break;
      case 'rediscover': ai('Waiting for the next discovery beacon to correct the device address.'); break;
      case 'flash': $('cardFlash').scrollIntoView({ behavior: 'smooth' }); break;
    }
  }
}

async function applyScene(id) {
  const scene = SCENES[id];
  if (!scene) return;
  // The server owns the scene; ask it, and only paint the slots locally.
  const r = await guard(`scene ${id}`, () => bridge.aiScene(id));
  for (const [slot, panel] of Object.entries(scene.layout))
    if (selects[slot]) selects[slot].value = panel;
  if (!r) {
    for (const [slot, panel] of Object.entries(scene.layout))
      await guard(`${slot}←${panel}`, () => bridge.layout(slot, panel));
  }
  ai(`Scene "${scene.label}" applied. ${scene.why || ''}`);
}

for (const [id, s] of Object.entries(SCENES)) {
  const b = document.createElement('button');
  b.textContent = s.label;
  b.title = s.why || '';
  b.onclick = () => applyScene(id);
  $('aiPresets').appendChild(b);
}

$('btnAiRun').onclick = async () => {
  const text = $('aiInput').value.trim();
  if (!text) return;
  $('aiInput').value = '';
  pill('pillAi', 'busy', 'AI working');

  // Prefer the server's AI; fall back to the local parser when unpaired.
  const r = await guard('ai', () => bridge.aiSay(text));
  if (r?.ok) {
    ai(`${r.say}${r.applied ? ` (${r.applied} applied)` : ''}`);
    await syncLayoutFromDevice();
  } else {
    const { actions, say } = interpret(text);
    ai(`${say} (offline parse)`);
    if (actions.length) await applyActions(actions);
  }
  pill('pillAi', 'on', 'AI ready');
};
$('aiInput').addEventListener('keydown', e => { if (e.key === 'Enter') $('btnAiRun').click(); });

/* sentinel loop */
setInterval(async () => {
  const r = sentinel.assess();
  $('healthBar').style.width = `${Math.round(r.score * 100)}%`;
  $('healthText').textContent = `${r.label} · ${Math.round(r.score * 100)}% ${r.advice ? '· ' + r.advice : ''}`;
  $('healthText').className = 'state ' + (r.score > 0.6 ? 'ok' : r.score > 0.3 ? '' : 'err');
  pill('pillAi', r.score > 0.6 ? 'on' : 'busy', `AI ${r.label.toLowerCase()}`);
}, 20000);

/** Mirror the server's AI settings into the checkboxes. */
async function refreshAi() {
  const s = await guard('ai state', () => bridge.aiState());
  if (!s) return;
  $('chkAutopilot').checked = !!s.autopilot;
  $('chkSentinel').checked = !!s.sentinel;
  $('chkCurator').checked = !!s.curator;
  $('chkGistAuto').checked = !!s.gistAuto;
  if (s.gist && !$('gistInput').value) $('gistInput').value = s.gist;
  $('healthBar').style.width = `${Math.round((s.health ?? 0) * 100)}%`;
  $('healthText').textContent =
    `${s.label} · ${Math.round((s.health ?? 0) * 100)}% ${s.advice ? '· ' + s.advice : ''} · server-side`;
}

for (const [id, key] of [['chkAutopilot', 'autopilot'], ['chkSentinel', 'sentinel'],
                         ['chkCurator', 'curator'], ['chkGistAuto', 'gistAuto']]) {
  $(id).onchange = () => guard(`ai ${key}`, () => bridge.aiConfig({ [key]: $(id).checked }));
}

async function syncLayoutFromDevice() {
  const r = await guard('read layout', () => bridge.raw('{"type":"query","what":"layout"}'));
  if (!r?.reply) return;
  try {
    const doc = JSON.parse(r.reply);
    for (const [slot, panel] of Object.entries(doc.slots || {}))
      if (selects[slot] && PANELS.includes(panel)) selects[slot].value = panel;
  } catch { /* device replied with something else */ }
}

/* autopilot */
let lastAuto = '';
setInterval(async () => {
  if (!$('chkAutopilot').checked || !bridge.online) return;
  const want = autoScene();
  if (want === lastAuto) return;
  lastAuto = want;
  await refreshAi();
}, 60000);

/* inbound */
const GIST_KEY = 'edgeview.gist';
$('gistInput').value = localStorage.getItem(GIST_KEY) || '';

async function pullGist() {
  const src = $('gistInput').value.trim();
  localStorage.setItem(GIST_KEY, src);
  const st = $('gistState');
  st.className = 'state';
  st.textContent = 'pulling…';

  // The server polls the gist on its own schedule; this asks it to pull now.
  await guard('ai gist', () => bridge.aiConfig({ gist: src }));
  const r = await guard('ai inbound', () => bridge.aiInbound());
  if (r?.ok) {
    st.className = 'state ok';
    st.textContent = `${r.directives} directive(s), ${r.applied} applied · ${new Date().toLocaleTimeString()}`;
    ai(`Inbound: ${r.directives} directive(s) from the gist.`);
    await syncLayoutFromDevice();
    return;
  }
  if (r && !r.ok) { st.className = 'state err'; st.textContent = r.error; return; }

  // Unpaired: fall back to pulling it here.
  try {
    const { doc, actions } = await pullInbound(src);
    const curated = Array.isArray(doc.news) ? curate(doc.news) : [];
    st.className = 'state ok';
    st.textContent = `${actions.length} directive(s)${curated.length ? `, ${curated.length} headline(s) kept` : ''} · offline pull`;
    await applyActions(actions);
  } catch (err) {
    st.className = 'state err';
    st.textContent = err.message;
  }
}

$('btnGistPoll').onclick = pullGist;

/* ───────────────────────────── flash card ───────────────────────────── */

function flashState(msg, cls = '') {
  $('flashState').className = 'state ' + cls;
  $('flashState').textContent = msg;
}

async function loadManifest() {
  try {
    const r = await fetch('firmware/manifest.json', { cache: 'no-store' });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const m = await r.json();
    fwManifest = m.builds || [];
    const sel = $('fwSelect');
    sel.innerHTML = '';
    for (const b of fwManifest) {
      sel.add(new Option(`${b.name}${b.size ? ` · ${(b.size / 1024).toFixed(0)} KiB` : ''}`, b.file));
    }
    flashState(`${fwManifest.length} build(s) from ${m.commit ? m.commit.slice(0, 7) : 'CI'}${m.built ? ` · ${m.built}` : ''}`);
  } catch (e) {
    $('fwSelect').innerHTML = '<option value="">No CI builds published — use a local UF2</option>';
    flashState(`Build list unavailable (${e.message}).`);
  }
}

async function currentUf2() {
  if (localUf2) return localUf2;
  const file = $('fwSelect').value;
  if (!file) throw new Error('choose a build or pick a local UF2');
  flashState('downloading firmware…');
  const r = await fetch(`firmware/${file}`, { cache: 'no-store' });
  if (!r.ok) throw new Error(`download failed (HTTP ${r.status})`);
  const buf = await r.arrayBuffer();
  return { name: file, buffer: buf };
}

$('btnRefreshFw').onclick = loadManifest;

$('fwFile').onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  const buffer = await f.arrayBuffer();
  try {
    const img = parseUf2(buffer);
    localUf2 = { name: f.name, buffer };
    flashState(`Local ${f.name} · ${(img.bytes / 1024).toFixed(0)} KiB ready.`, 'ok');
  } catch (err) {
    localUf2 = null;
    flashState(err.message, 'err');
  }
};

$('btnDownloadUf2').onclick = async () => {
  try {
    const { name, buffer } = await currentUf2();
    const url = URL.createObjectURL(new Blob([buffer], { type: 'application/octet-stream' }));
    const a = document.createElement('a');
    a.href = url; a.download = name; a.click();
    URL.revokeObjectURL(url);
  } catch (e) { flashState(e.message, 'err'); }
};

$('btnFlashUsb').onclick = async () => {
  if (!usbSupported) { flashState('WebUSB needs Chrome or Edge.', 'err'); return; }
  try {
    pill('pillUsb', 'busy', 'USB flashing');
    const { name, buffer } = await currentUf2();
    flashState(`flashing ${name}…`);
    await flashOverUsb(buffer, (pct, note) => {
      $('flashProgress').value = pct;
      flashState(`${(pct * 100).toFixed(0)}% · ${note}`);
    });
    flashState('Done — the board is rebooting into the new firmware.', 'ok');
    pill('pillUsb', 'on', 'USB flashed');
    ai('Firmware flashed over USB. Give it a few seconds to rejoin WiFi.');
  } catch (e) {
    flashState(e.message, 'err');
    pill('pillUsb', 'err', 'USB failed');
  }
};

$('btnFlashDrive').onclick = async () => {
  try {
    pill('pillUsb', 'busy', 'copying');
    const { name, buffer } = await currentUf2();
    await flashViaDrive(buffer, name, (pct, note) => { $('flashProgress').value = pct; flashState(note); });
    flashState('Copied to RPI-RP2 — the board reboots itself.', 'ok');
    pill('pillUsb', 'on', 'copied');
  } catch (e) {
    flashState(e.message, 'err');
    pill('pillUsb', 'err', 'copy failed');
  }
};

$('btnBootsel').onclick = async () => {
  try {
    await rebootToBootsel();
    flashState('BOOTSEL requested — the board should reappear as RPI-RP2.', 'ok');
  } catch (e) {
    flashState(`${e.message} · hold BOOTSEL and replug instead.`, 'err');
  }
};

if (!usbSupported) $('btnFlashUsb').disabled = true;
if (!driveSupported) $('btnFlashDrive').disabled = true;
if (!serialSupported) $('btnBootsel').disabled = true;

/* ───────────────────────────── chat ───────────────────────────── */

let aiBubble = null;

function chatLine(who, text, cls) {
  const log = $('chatLog');
  const el = document.createElement('div');
  el.className = cls;
  el.innerHTML = `<b>${who}</b> `;
  const span = document.createElement('span');
  span.textContent = text;
  el.appendChild(span);
  log.appendChild(el);
  while (log.childElementCount > 60) log.firstChild.remove();
  log.scrollTop = log.scrollHeight;
  return span;
}

// The server streams the answer token by token; grow one bubble as it arrives.
bridge.events.addEventListener('token', (e) => {
  const { t, done } = e.detail;
  if (done) { aiBubble = null; return; }
  if (!aiBubble) aiBubble = chatLine('ionity-ai', '', 'ai');
  aiBubble.textContent += t;
  $('chatLog').scrollTop = $('chatLog').scrollHeight;
});

async function sendChat() {
  const text = $('chatInput').value.trim();
  if (!text) return;
  $('chatInput').value = '';
  chatLine('you', text, 'me');
  aiBubble = null;
  const r = await guard('chat', () => bridge.say('web', text));
  if (!r) chatLine('ionity-ai', 'No server — pair with EDGE-VIEW Studio or the host exe first.', 'ai');
}

$('btnChatSend').onclick = sendChat;
$('chatInput').addEventListener('keydown', e => { if (e.key === 'Enter') sendChat(); });

$('btnChatClear').onclick = async () => {
  $('chatLog').innerHTML = '';
  await guard('clear', () => bridge.brainSet({ clear: true }));
};

async function refreshBrain() {
  const b = await guard('brain', () => bridge.brain());
  const chip = $('brainChip');
  const state = $('brainState');
  if (!b) { chip.textContent = 'brain: no server'; state.textContent = 'Start the server to use chat.'; return; }
  const br = b.brain || {};
  if (br.available) {
    chip.textContent = `brain: ${br.model}`;
    state.className = 'state ok';
    state.textContent = `${br.backend} at ${br.endpoint} — running on your machine, nothing leaves it.`;
  } else {
    chip.textContent = 'brain: built-in';
    state.className = 'state';
    state.textContent = 'No local model found. Chat still works on built-in replies. ' +
      'Start Ollama (ollama run qwen2.5:1.5b) or LM Studio and press Find.';
  }
}

$('btnBrainProbe').onclick = refreshBrain;

/* ───────────────────────────── live view ───────────────────────────── */

function showSource(which) {
  const isMirror = which === 'mirror';
  $('mirrorCanvas').hidden = !isMirror;
  $('previewCanvas').hidden = isMirror;
  $('btnSourceMirror').classList.toggle('primary', isMirror);
  $('btnSourcePreview').classList.toggle('primary', !isMirror);

  if (isMirror) {
    if (!bridge.token) { $('viewNote').textContent = 'Pair with the server first — the mirror comes through it.'; }
    mirror.start(bridge.base, bridge.token);
    $('viewNote').textContent = 'Real frames off the glass, 160x120, downscaled and RLE-packed on the device.';
  } else {
    mirror.stop();
    $('viewStatus').textContent = 'preview';
    $('viewNote').textContent = 'Preview is drawn here from live data — the device is not involved.';
  }
}

$('btnSourcePreview').onclick = () => showSource('preview');
$('btnSourceMirror').onclick = () => showSource('mirror');
$('mirrorFps').oninput = (e) => { $('mirrorFpsLabel').textContent = e.target.value; };
$('mirrorFps').onchange = () => { if (!$('mirrorCanvas').hidden) showSource('mirror'); };

/* ───────────────────────────── chat crawl ───────────────────────────── */

$('crawlForm').onsubmit = async (e) => {
  e.preventDefault();
  const text = $('crawlText').value.trim();
  if (!text) return;
  const name = $('crawlName').value.trim() || 'web';
  $('crawlText').value = '';
  const r = await guard('chat', () => bridge.chat(name, text));
  if (r) crawl.push(`${name}: ${text}`);
};

$('crawlToggle').onclick = (e) => {
  const p = e.target.textContent === '⏸';
  crawl.setPaused(p);
  e.target.textContent = p ? '▶' : '⏸';
};

/* ───────────────────────────── ambience ───────────────────────────── */

(function bgfx() {
  const c = $('bgfx');
  const ctx = c.getContext('2d');
  let w, h, dots = [];
  const resize = () => {
    w = c.width = innerWidth;
    h = c.height = innerHeight;
    dots = Array.from({ length: Math.min(70, Math.round(w / 24)) }, () => ({
      x: Math.random() * w, y: Math.random() * h,
      vx: (Math.random() - 0.5) * 0.25, vy: (Math.random() - 0.5) * 0.25,
    }));
  };
  addEventListener('resize', resize);
  resize();
  (function draw() {
    ctx.clearRect(0, 0, w, h);
    for (const d of dots) {
      d.x = (d.x + d.vx + w) % w;
      d.y = (d.y + d.vy + h) % h;
    }
    ctx.strokeStyle = '#e9456022';
    for (let i = 0; i < dots.length; i++) {
      for (let j = i + 1; j < dots.length; j++) {
        const dx = dots[i].x - dots[j].x, dy = dots[i].y - dots[j].y;
        const d2 = dx * dx + dy * dy;
        if (d2 < 20000) {
          ctx.globalAlpha = 1 - d2 / 20000;
          ctx.beginPath();
          ctx.moveTo(dots[i].x, dots[i].y);
          ctx.lineTo(dots[j].x, dots[j].y);
          ctx.stroke();
        }
      }
    }
    ctx.globalAlpha = 1;
    ctx.fillStyle = '#00bcd455';
    for (const d of dots) ctx.fillRect(d.x, d.y, 1.6, 1.6);
    requestAnimationFrame(draw);
  })();
})();

/* ───────────────────────────── boot ───────────────────────────── */

(async function boot() {
  $('tokenInput').value = bridge.token;
  preview.setLayout(DEFAULT_LAYOUT);
  await loadManifest();
  await bridge.probe();
  if (bridge.token) { bridge.listen(); await refreshState(); await refreshAi(); await refreshBrain(); }
  ai('Console ready. The AI runs on the server — it keeps working with this page closed.');
  crawl.push('IO-nity EDGE-VIEW web console online');
  setInterval(async () => {
    if (bridge.online && bridge.listening) return;
    if (await bridge.probe() && bridge.token && !bridge.listening) {
      bridge.listen();
      await refreshState();
    }
  }, 15000);
})();
