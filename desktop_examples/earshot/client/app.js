import { decodeFrame, encodeSet, encodeSource, encodeHello, encodeRecord, encodeDeleteRecording,
         formatHz, parseFreq, fmtBytes, fmtDuration, SeqTracker, T_SPECTRUM, T_AUDIO } from './proto.js';
import { Waterfall } from './waterfall.js';
import { AudioPlayer } from './audio.js';
import * as panel from './panel.js';
import { DecoderPanel, menuRows, costTotal } from './decoders.js';
import { applyError, deviceRows } from './devices.js';
import { recordingRows, dirSummary, scpLine, prunedNote } from './recordings.js';
import { keyAction, tuneStepHz } from './keys.js';

const $ = (id) => document.getElementById(id);
const prefs = (() => { try { return JSON.parse(localStorage.getItem('earshot') || '{}'); } catch { return {}; } })();
const savePrefs = () => { try { localStorage.setItem('earshot', JSON.stringify(prefs)); } catch {} };

const state = { gen: 0, hello: null, st: {}, model: [], sources: [], decoders: [],
                connected: false, role: 'view', devErrors: {}, stats: {}, confirmDelete: '' };
window.__earshot = state;

const audio = new AudioPlayer();
const seqSpec = new SeqTracker(), seqAudio = new SeqTracker();
let ws = null, backoff = 500;

// A control that cannot be used still has to say why (house rule 1).
function gate(el, ok, reason) {
  if (!el) return;
  el.disabled = !ok;
  if (!ok && reason) el.title = reason;
  else if (el.dataset.hint) el.title = el.dataset.hint;
  else el.removeAttribute('title');
}
const ctl = () => state.role === 'ctl';
const NOT_CTL = 'only the controller can change this';

const decoders = new DecoderPanel($('dectabs'), $('text'), (id) => decoders.setActive(id));

const wf = new Waterfall($('spec'), $('wf'), {
  onTune: (hz) => tune(hz),
  onPan: (dHz) => { if (ctl() && state.st.freq !== undefined) send(encodeSet({ freq: state.st.freq + dHz })); },
  onView: (v) => { prefs.view = v; savePrefs(); $('resetzoom').classList.toggle('hidden', !wf.zoomed()); },
  onCursor: (hz, db) => {
    $('cursor').textContent = hz === null ? '' : `${formatHz(hz)}  ${db === null ? '' : db.toFixed(0) + ' dB'}`;
  }
});
if (prefs.view && Number.isFinite(prefs.view.x0) && Number.isFinite(prefs.view.x1)
    && prefs.view.x0 >= 0 && prefs.view.x1 <= 1 && prefs.view.x0 < prefs.view.x1) wf.setView(prefs.view.x0, prefs.view.x1);
$('resetzoom').classList.toggle('hidden', !wf.zoomed());
$('resetzoom').onclick = () => { wf.resetView(); $('resetzoom').classList.add('hidden'); };

audio.setGain(prefs.volume ?? 0.8);
$('vol').value = prefs.volume ?? 0.8;
$('vol').oninput = () => { audio.setGain(Number($('vol').value)); prefs.volume = Number($('vol').value); savePrefs(); };

function send(text) { if (ws && ws.readyState === 1) ws.send(text); }
function tune(hz) { if (ctl()) send(encodeSet({ freq: hz })); }

function toast(msg) {
  const d = document.createElement('div'); d.className = 'toast'; d.textContent = msg;
  $('errors').appendChild(d); setTimeout(() => d.remove(), 5000);
}
// A fatal error outlives a toast: it is the only explanation of a dead app.
function fatal(msg) { $('banner').textContent = msg; $('banner').classList.remove('hidden'); }

function connect() {
  const token = new URLSearchParams(location.search).get('token');
  const url = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/${token ? `?token=${encodeURIComponent(token)}` : ''}`;
  ws = new WebSocket(url);
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => {
    state.connected = true; backoff = 500;
    $('conn').textContent = 'live'; $('conn').className = 'pill ok';
    send(encodeHello(token)); seqSpec.reset(); seqAudio.reset(); audio.flush();
  };
  ws.onclose = () => {
    state.connected = false; state.hello = null;
    $('conn').textContent = `reconnecting… (${Math.round(backoff / 1000)}s)`; $('conn').className = 'pill';
    audio.flush(); setTimeout(connect, backoff); backoff = Math.min(8000, backoff * 2);
  };
  ws.onerror = () => ws.close();
  ws.onmessage = (e) => {
    if (typeof e.data === 'string') { let m; try { m = JSON.parse(e.data); } catch { return; } onText(m); return; }
    const f = decodeFrame(e.data);
    if (!f) return;
    if (f.type === 0) { fatal(`server speaks protocol ${f.ver}, this page speaks 1 — reload after upgrading one of them`); ws.close(); return; }
    if (state.gen && f.gen !== state.gen) return;
    if (f.type === T_SPECTRUM) { seqSpec.push(f.seq); wf.push(f); }
    else if (f.type === T_AUDIO) { if (seqAudio.push(f.seq)) audio.flush(); if (audio.running) audio.push(f.pcm); }
  };
}

function onText(m) {
  if (m.t === 'hello') {
    if (m.proto !== 1) { fatal(`server speaks protocol ${m.proto}, this page speaks 1`); return; }
    const first = !state.hello;
    state.hello = m; state.sources = m.sources || []; state.role = m.role;
    state.decoders = m.decoders || [];
    $('s-ver').textContent = m.version ? `server ${m.version}` : '';
    state.model = panel.buildModel(m.controls, m.state);
    decoders.setDecoders(state.decoders);
    renderControls();
    renderRecordDirs();
    renderDecoderMenu();
    applyState({ ...(m.state || {}), role: m.role });
    renderDevices();
    if (first && !(m.state && m.state.source)) $('dlg-sources').showModal();
  } else if (m.t === 'state') {
    applyState(m);
  } else if (m.t === 'stats') {
    state.stats = m;
    const extra = (m.decoder_dropped ? ` · decoder drops ${m.decoder_dropped}` : '')
      + (m.text_dropped ? ` · text drops ${m.text_dropped}` : '');
    $('s-stats').textContent = `drops spec ${m.spectrum_dropped ?? 0} audio ${m.audio_dropped ?? 0}`
      + ` · overflows ${m.overflows ?? 0} · clients ${m.clients ?? 1}${extra}`;
    $('recstatus').textContent = state.st.recording
      ? `${fmtDuration(m.rec_seconds ?? 0)} · ${fmtBytes(m.rec_bytes ?? 0)}` : '';
    renderRecordSection();
    if (m.duration !== undefined) {
      $('pos').max = m.duration;
      if (document.activeElement !== $('pos')) $('pos').value = m.pos ?? 0;
      $('postime').textContent = `${fmtDuration(m.pos ?? 0)} / ${fmtDuration(m.duration)}${m.ended ? ' · ended' : ''}`;
    }
  } else if (m.t === 'error') {
    const r = applyError(state.devErrors, m);
    state.devErrors = r.errors;
    if (r.toast) {
      if (m.code === 'set' && /freq|record/.test(m.msg || '')) { $('freq').classList.add('bad'); $('freq').title = m.msg; }
      toast(`${m.code}: ${m.msg}`);
    } else { if (!$('dlg-sources').open) $('dlg-sources').showModal(); renderDevices(); }
  } else if (m.t === 'text') {
    decoders.push(m.stream, m.data);
  }
}

function applyState(s) {
  const prevSource = state.st.source;
  Object.assign(state.st, s);
  if (s.gen !== undefined && s.gen !== state.gen) { state.gen = s.gen; audio.flush(); seqAudio.reset(); }
  if (s.role) {
    state.role = s.role;
    $('role').textContent = s.role === 'ctl' ? 'controller' : 'viewer';
    $('role').className = `pill ${s.role === 'ctl' ? 'ctl' : ''}`;
    $('role').title = s.role === 'ctl' ? 'you hold control of this receiver' : 'another tab holds control; you can watch and listen';
  }
  if (state.st.freq !== undefined && document.activeElement !== $('freq')) {
    $('freq').value = formatHz(state.st.freq);
    $('freq').classList.remove('bad');
  }
  $('source').textContent = (state.st.source || 'no source') + (state.st.switching ? ' (switching…)' : '');
  if (s.source !== undefined && s.source !== prevSource) {
    delete state.devErrors[s.source];
    wf.clear(); renderDevices();
    if (s.source && $('dlg-sources').open) $('dlg-sources').close();
  }
  panel.applyState(state.model, state.st);
  panel.update($('controls'), state.model);
  renderModes();
  wf.setMarker(state.st.offset || 0, state.st.passband || 0);
  $('transport').classList.toggle('hidden', !state.st.is_file);
  if (state.st.paused !== undefined) $('playpause').textContent = state.st.paused ? '▶' : '⏸';
  if (state.st.loop !== undefined) $('loop').checked = !!state.st.loop;
  if (Array.isArray(state.st.decoders)) { decoders.setRunning(state.st.decoders); renderDecoderMenu(); }
  if (state.st.freq !== undefined) decoders.setTuning(state.st.freq);
  $('recchip').classList.toggle('hidden', !state.st.recording);
  renderRecordSection();
  applyRole();
}

// Per-control, with the reason attached — never a blanket sweep, and never audio,
// which is this browser's business and not the controller's.
function applyRole() {
  const can = ctl();
  gate($('freq'), can, 'only the controller can tune');
  gate($('rescan'), can, 'only the controller can rescan');
  gate($('devrate'), can, NOT_CTL);
  gate($('playpause'), can, NOT_CTL);
  gate($('pos'), can, NOT_CTL);
  gate($('loop'), can, NOT_CTL);
  gate($('recname'), can, 'only the controller can record');
  gate($('recdir'), can, 'only the controller can record');
  for (const el of $('controls').querySelectorAll('[data-ctl]')) gate(el, can, NOT_CTL);
  for (const b of $('modes').children) gate(b, can && !b.dataset.why, b.dataset.why || NOT_CTL);
  for (const el of $('decmenu-items').querySelectorAll('input[type=checkbox]')) {
    gate(el, can && el.dataset.available === '1', el.dataset.available === '1' ? NOT_CTL : (el.dataset.why || 'not available'));
  }
  renderRecordSection();
}

function renderModes() {
  const mode = state.model.find((c) => c.id === 'mode');
  if (!mode) return;
  const box = $('modes');
  if (box.children.length !== mode.options.length) {
    box.textContent = '';
    mode.options.forEach((o, i) => {
      const b = document.createElement('button');
      b.textContent = o; b.dataset.mode = o; b.dataset.testid = `mode-${o}`;
      b.onclick = () => send(encodeSet({ mode: o }));
      box.appendChild(b);
    });
  }
  mode.options.forEach((o, i) => {
    const b = box.children[i];
    const why = (mode.options_disabled || [])[i] || '';
    b.dataset.why = why ? `${o} ${why}` : '';
    b.classList.toggle('on', o === state.st.mode);
  });
  const pb = state.st.passband, rate = state.st.rate;
  $('passband').textContent = pb ? `${fmtHzShort(pb)} wide · ${fmtHzShort(240e3)} channel${rate ? ` · source ${fmtHzShort(rate)}` : ''}` : '';
}

function fmtHzShort(hz) {
  if (hz >= 1e6) return `${+(hz / 1e6).toFixed(2)} MHz`;
  if (hz >= 1e3) return `${+(hz / 1e3).toFixed(1)} kHz`;
  return `${hz} Hz`;
}

function renderControls() {
  const rest = state.model.filter((c) => c.id !== 'mode' && c.id !== 'freq');
  $('controls-title').textContent = (state.st.source || 'source').split(':')[0];
  $('sec-controls').classList.toggle('hidden', !rest.length);
  panel.render($('controls'), rest, (id, value) => send(encodeSet({ [id]: value })));
  applyRole();
}

/* ---------- record ---------- */

function recordDirs() { return (state.hello && state.hello.record && state.hello.record.dirs) || []; }
function activeDir() {
  const dirs = recordDirs();
  return dirs.find((d) => d.id === state.st.record_dir) || dirs[0] || null;
}

function renderRecordDirs() {
  const dirs = recordDirs();
  const sel = $('recdir');
  // no --record-dir at all: the section has nothing to say, so it disappears
  $('sec-record').classList.toggle('hidden', !dirs.length);
  $('recdirrow').classList.toggle('hidden', dirs.length < 2);
  $('recdironerow').classList.toggle('hidden', dirs.length !== 1);
  if (dirs.length === 1) $('recdirone').textContent = dirs[0].path;
  sel.textContent = '';
  for (const d of dirs) {
    const o = document.createElement('option');
    o.value = d.id; o.textContent = d.path; o.selected = d.id === state.st.record_dir;
    sel.appendChild(o);
  }
}

function renderRecordSection() {
  const dirs = recordDirs(), dir = activeDir();
  const prefix = $('recname').value.trim();
  const next = state.stats.next_name || state.st.next_name || '';
  $('recname').placeholder = next || 'prefix';
  $('recprev').textContent = next ? `→ ${prefix ? prefix + '_' : ''}${next}.sigmf-data` : '';
  const free = state.stats.free_bytes ?? (dir ? dir.free_bytes : 0);
  const max = state.hello?.record?.max_bytes || 0;
  $('recfree').textContent = dir ? `${fmtBytes(free)} free${max ? ` · cap ${fmtBytes(max)}` : ''}` : '';
  const low = dir && free < (state.hello?.record?.min_free_bytes || 0);
  $('recfree').className = low ? 'warnnote' : 'note';
  if (low) $('recfree').textContent += ' — recording will stop';

  const btn = $('recbtn');
  btn.textContent = state.st.recording ? '■ stop' : '● record';
  btn.classList.toggle('on', !!state.st.recording);
  const why = !dirs.length ? 'the server was started without --record-dir'
    : !ctl() ? 'only the controller can record'
    : !state.st.source ? 'no source is running'
    : (!state.st.recording && low) ? `less than ${fmtBytes(state.hello?.record?.min_free_bytes || 0)} free in ${dir.path}` : '';
  gate(btn, !why, why);
}

/* ---------- decoders ---------- */

function renderDecoderMenu() {
  const running = Array.isArray(state.st.decoders) ? state.st.decoders : [];
  const box = $('decmenu-items');
  box.textContent = '';
  for (const r of menuRows(state.decoders, running)) {
    const label = document.createElement('label');
    if (!r.available) label.className = 'off';
    const cb = document.createElement('input');
    cb.type = 'checkbox'; cb.checked = r.checked;
    cb.dataset.testid = `decoder-${r.id}`;
    cb.dataset.available = r.available ? '1' : '0';
    cb.dataset.why = r.reason;
    cb.onchange = () => {
      const next = cb.checked ? [...running, r.id] : running.filter((x) => x !== r.id);
      send(encodeSet({ decoders: next }));
    };
    const name = document.createElement('span'); name.textContent = r.id;
    const band = document.createElement('span'); band.className = 'band'; band.textContent = r.band;
    const cost = document.createElement('span'); cost.className = 'cost'; cost.textContent = r.cost;
    label.append(cb, name, band, cost);
    box.appendChild(label);
    // an unavailable decoder explains itself in text: a tooltip is unreachable on touch
    if (!r.available && r.reason) {
      const why = document.createElement('div'); why.className = 'reason'; why.textContent = `unavailable — ${r.reason}`;
      box.appendChild(why);
    }
  }
  const total = costTotal(state.decoders, running);
  $('deccost').textContent = total.note || total.text;
  $('deccost').className = total.warn ? 'warnnote' : 'note';
  // nothing running: the chips and the table have nothing to say, but the ⋯ menu
  // is how you start one, so it stays
  $('dectabs').classList.toggle('hidden', !running.length);
  $('text').classList.toggle('hidden', !running.length);
  $('decempty').classList.toggle('hidden', !!running.length);
  applyRole();
}

/* ---------- dialogs ---------- */

const RATES = [['keep rate', 0], ['1 MS/s', 1e6], ['2 MS/s', 2e6], ['2.4 MS/s', 2.4e6],
               ['4 MS/s', 4e6], ['8 MS/s', 8e6], ['10 MS/s', 10e6], ['20 MS/s', 20e6]];
for (const [label, hz] of RATES) {
  const o = document.createElement('option'); o.textContent = label; o.value = hz;
  $('devrate').appendChild(o);
}

function renderDevices() {
  const list = $('devlist'); list.textContent = '';
  if (!state.sources.length) { list.textContent = 'no devices reported yet'; return; }
  for (const r of deviceRows(state.sources, state.devErrors, state.st.source, state.role)) {
    const row = document.createElement('div'); row.className = r.why ? 'row failed' : 'row';
    const lbl = document.createElement('span'); lbl.className = 'lbl'; lbl.textContent = r.label;
    const kind = document.createElement('span'); kind.className = 'kind'; kind.textContent = r.kind;
    const btn = document.createElement('button');
    btn.textContent = r.connected ? 'connected' : r.why ? 'Retry' : 'Connect';
    btn.dataset.testid = `connect-${r.id}`;
    gate(btn, r.connectable, r.connected ? 'already the running source' : NOT_CTL);
    btn.onclick = () => {
      delete state.devErrors[r.id];
      renderDevices();
      send(encodeSource(r.id, Number($('devrate').value || 0)));
    };
    row.append(lbl, kind, btn); list.appendChild(row);
    if (r.why) {
      const why = document.createElement('div'); why.className = 'why'; why.textContent = r.why;
      list.appendChild(why);
    }
  }
}

const tokenQs = () => { const t = new URLSearchParams(location.search).get('token'); return t ? `?token=${encodeURIComponent(t)}` : ''; };

async function renderRecordings() {
  const el = $('reclist');
  const dir = activeDir();
  $('recdirinfo').textContent = dir ? `${dir.path} · ${dirSummary(dir, state.hello?.record?.max_bytes || 0)}` : 'no record directory configured';
  $('recscp').textContent = dir ? scpLine(location.hostname || 'box', dir.path, '<name>') : '';
  $('recscp').title = 'click to copy';
  $('recpruned').textContent = prunedNote(state.stats.pruned_bytes || 0);
  el.textContent = 'loading…';
  let recs = [];
  try {
    const r = await fetch(`/recordings${tokenQs()}`);
    if (!r.ok) { el.textContent = `could not list recordings (HTTP ${r.status})`; return; }
    recs = await r.json();
  } catch { el.textContent = 'could not reach the server'; return; }
  el.textContent = '';
  if (!recs.length) { el.textContent = 'none yet'; return; }
  for (const rec of recordingRows(recs, state.role)) {
    const row = document.createElement('div'); row.className = 'row';
    row.dataset.testid = `recording-${rec.name}`;
    const lbl = document.createElement('span'); lbl.className = 'lbl';
    const nm = document.createElement('div'); nm.className = 'mono'; nm.textContent = rec.name;
    const meta = document.createElement('div'); meta.className = 'note'; meta.textContent = rec.meta;
    lbl.append(nm, meta);
    const play = document.createElement('button');
    play.textContent = 'Play'; play.dataset.testid = `play-${rec.name}`;
    gate(play, rec.canPlay, rec.playReason);
    play.onclick = () => send(JSON.stringify({ t: 'play', name: rec.name }));
    const dl = document.createElement('a');
    dl.textContent = 'Download'; dl.href = `/recordings/${rec.name}.sigmf-data${tokenQs()}`;
    const del = document.createElement('button');
    del.dataset.testid = `delete-${rec.name}`;
    const armed = state.confirmDelete === rec.name;
    del.textContent = armed ? 'Confirm' : 'Delete';
    if (armed) del.className = 'confirm';
    gate(del, rec.canDelete, rec.deleteReason);
    del.onclick = () => {
      if (state.confirmDelete !== rec.name) { state.confirmDelete = rec.name; renderRecordings(); return; }
      state.confirmDelete = '';
      send(encodeDeleteRecording(rec.name));
      setTimeout(renderRecordings, 300);
    };
    row.append(lbl, play, dl, del);
    el.appendChild(row);
  }
}

/* ---------- wiring ---------- */

$('freq').onchange = () => {
  const hz = parseFreq($('freq').value);
  if (hz === null) {
    $('freq').classList.add('bad');
    $('freq').title = 'not a frequency — try 100.1, 100.1M or 433920k';
    return;
  }
  $('freq').classList.remove('bad');
  tune(hz);
  $('freq').blur();
};
$('recname').oninput = renderRecordSection;
$('recdir').onchange = () => { state.st.record_dir = $('recdir').value; renderRecordSection(); };
$('playpause').onclick = () => send(JSON.stringify({ t: 'play', pause: !state.st.paused }));
$('pos').onchange = () => send(JSON.stringify({ t: 'play', pos: Number($('pos').value) }));
$('loop').onchange = () => send(JSON.stringify({ t: 'play', loop: $('loop').checked }));
$('recbtn').onclick = () => send(encodeRecord(!state.st.recording, $('recdir').value || undefined, $('recname').value.trim()));
$('rescan').onclick = () => send(JSON.stringify({ t: 'rescan' }));
$('recscp').onclick = () => navigator.clipboard?.writeText($('recscp').textContent).then(() => toast('scp line copied'), () => {});

const closeMenus = () => { for (const d of document.querySelectorAll('details.menu[open]')) d.open = false; };
$('menu-sources').onclick = () => { closeMenus(); renderDevices(); $('dlg-sources').showModal(); };
$('menu-recordings').onclick = () => { closeMenus(); $('dlg-recordings').showModal(); renderRecordings(); };
for (const b of document.querySelectorAll('[data-close]')) b.onclick = () => b.closest('dialog').close();
document.addEventListener('click', (e) => { if (!e.target.closest('details.menu')) closeMenus(); });

const showTab = (dec) => {
  $('pane-rx').classList.toggle('hidden', dec);
  $('pane-dec').classList.toggle('hidden', !dec);
  $('tab-rx').classList.toggle('on', !dec);
  $('tab-dec').classList.toggle('on', dec);
};
$('tab-rx').onclick = () => showTab(false);
$('tab-dec').onclick = () => showTab(true);

const toggleAudio = async () => {
  if (audio.running) { await audio.suspend(); audio.flush(); } else await audio.start();
  $('listenbtn').textContent = audio.running ? '■ stop' : '▶ listen';
  $('listenbtn').classList.toggle('pulse', !audio.running);
};
$('listenbtn').onclick = toggleAudio;
$('listenbtn').classList.add('pulse');
$('listenbtn').dataset.hint = 'browsers block sound until you ask for it';
$('listenbtn').title = $('listenbtn').dataset.hint;

document.addEventListener('keydown', (e) => {
  const a = keyAction(e, document.activeElement?.tagName);
  if (!a) return;
  if (a.action === 'close') { closeMenus(); return; }
  e.preventDefault();
  if (a.action === 'tune' && ctl() && state.st.freq !== undefined) {
    tune(state.st.freq + a.steps * tuneStepHz(state.st.rate, wf.view.x1 - wf.view.x0));
  } else if (a.action === 'listen') toggleAudio();
  else if (a.action === 'record') { if (!$('recbtn').disabled) $('recbtn').click(); }
  else if (a.action === 'focusFreq') $('freq').focus();
});

setInterval(() => {
  $('s-audio').textContent = audio.running
    ? `audio: ${audio.stats.bufferedMs} ms buffered · underruns ${audio.stats.underruns} · dropped ${audio.stats.dropped}`
      + (audio.sampleRate && audio.sampleRate !== 48000 ? ` · resampled to ${audio.sampleRate} Hz` : '')
    : 'audio: off';
}, 500);

connect();
