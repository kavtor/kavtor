'use strict';
const $ = id => document.getElementById(id);
const categories = { mix: 'MIX', wipe: 'WIPE', keys: 'KEYERS', dme: 'DME' };
let state = {}, catalogue = [], page = 'mix', keySlot = 0, keyTarget = 'key', effect = 'move';
let editing = null, draft = '', replace = true, busy = false, touching = false, paintedMe = 0;
let matrixPage = 0, lastPaint = '';
const subpages = { mix: 'rate', wipe: 'edge', keys: 'delegate', dme: 'effects' };

function button(text, action, selected = false) {
  const item = document.createElement('button');
  item.textContent = text;
  item.classList.toggle('selected', selected);
  item.onclick = action;
  return item;
}
function notify(text, error = false) {
  $('message').textContent = text;
  $('message').classList.toggle('error', error);
}
async function command(value) {
  if (value.cmd !== 'me') value = { ...value, expectedMe: editing ? editing.me : paintedMe };
  if (busy) return false;
  busy = true;
  notify('Submitting preparation');
  try {
    const response = await fetch('/api/command', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(value),
    });
    const reply = await response.json();
    if (!response.ok || reply.event !== 'ack') throw Error(reply.message || reply.error || 'Command rejected');
    notify('Accepted · authoritative state shown');
    await poll();
    return true;
  } catch (error) {
    notify(error.message, true);
    return false;
  } finally { busy = false; }
}
function selectCategory(next) { page = next; matrixPage = 0; lastPaint = ''; paint(); }
function selectSubpage(next) { subpages[page] = next; matrixPage = 0; lastPaint = ''; paint(); }
function choice(label, action, selected = false, detail = '') {
  return { label, action, selected, detail };
}
function number(label, value, min, max, save, step = 1, detail = '') {
  return { label, value: value < 0 ? 'Follow SOFT' : String(value), detail,
    action: () => openNumber(label, value, min, max, save, step) };
}
function color(label, value, save) {
  return { label, color: /^#[0-9a-f]{6}$/i.test(value) ? value : '#000000', save };
}
function borderProfile(side = state.wipeBorderSide ?? 0, inner = state.wipeInnerSoft ?? -1, outer = state.wipeOuterSoft ?? -1) {
  return command({ cmd: 'wipe_border_profile', side, innerSoft: inner, outerSoft: outer });
}
function keySave(settings) {
  const live = (keyTarget === 'key' ? state.keys : state.dsks)?.[keySlot];
  const onAir = !!live?.on;
  if (onAir && !confirm('This key is ON AIR. Apply processing changes?')) return Promise.resolve(false);
  return command({ cmd: 'key_processing', target: keyTarget, slot: keySlot, settings,
    guardOnAir: true, allowOnAir: onAir });
}
function currentScreen() {
  const caps = state.capabilities || {}, entries = [];
  let menus = [], title = 'Preparation', hint = 'Preparation only', onAir = false;
  if (page === 'home') {
    title = 'Top menu';
    for (const [id, label] of Object.entries(categories)) entries.push(choice(label, () => selectCategory(id)));
    return { title, hint, menus, entries };
  }
  if (page === 'mix') {
    menus = [['rate', 'Rate']];
    if (caps.dustMix) menus.push(['dust', 'Dust Mix']);
    if ((caps.mixModes || []).includes('dip')) menus.push(['dip', 'DIP']);
    if (caps.broadcastMixes) menus.push(['super', 'SUPER MIX']);
    if (!menus.some(row => row[0] === subpages.mix)) subpages.mix = 'rate';
    if (subpages.mix === 'rate') {
      title = 'Rate';
      entries.push(number('AUTO rate', state.autoFrames ?? 25, 1, 1000,
        frames => command({ cmd: 'rate', frames }), 1, 'Frames'));
    } else if (subpages.mix === 'dust') {
      title = 'Dust Mix'; hint = 'MIX 8 · preparation';
      const save = (ratio = state.dustRatio ?? 50, size = state.dustSize ?? 2, flash = state.dustFlash ?? 0) =>
        command({ cmd: 'dust_params', ratio, size, flash });
      entries.push(number('Mix ratio', state.dustRatio ?? 50, 0, 100, n => save(n), 1, '% dust'),
        number('Particle size', state.dustSize ?? 2, 1, 100, n => save(undefined, n), 1, '% picture height'),
        number('Flash steps', state.dustFlash ?? 0, 0, 100, n => save(undefined, undefined, n)),
        choice('Default recall', () => save(50, 2, 0), false, '50 / 2 / 0'));
    } else if (subpages.mix === 'dip') {
      title = 'DIP color';
      entries.push(color('Intermediate color', state.dipColor, n => command({ cmd: 'dip_color', color: n })));
    } else {
      title = 'SUPER MIX';
      entries.push(number('Picture A', state.superMixGainA ?? 100, 0, 100,
        n => command({ cmd: 'mix_params', aGain: n, bGain: state.superMixGainB ?? 100 }), 1, '% midpoint gain'),
        number('Picture B', state.superMixGainB ?? 100, 0, 100,
          n => command({ cmd: 'mix_params', aGain: state.superMixGainA ?? 100, bGain: n }), 1, '% midpoint gain'));
    }
  } else if (page === 'wipe') {
    menus = [['pattern', 'Pattern'], ['edge', 'Edge'], ['position', 'Position']];
    if (subpages.wipe === 'pattern') {
      title = 'Pattern'; hint = 'Stored default · explicit panel shortcuts take precedence';
      const seen = new Set();
      for (const row of catalogue) {
        if (row.sony == null || !(caps.sonyWipes || []).includes(row.sony) || seen.has(row.sony)) continue;
        seen.add(row.sony);
        entries.push(choice(String(row.sony), () => command({ cmd: 'wipe_pattern', pattern: row.id }),
          row.id === state.wipePattern, row.label || row.name || row.id));
      }
    } else if (subpages.wipe === 'position') {
      title = 'Position';
      entries.push(number('Horizontal', state.wipePosX ?? 500, 0, 1000,
        n => command({ cmd: 'wipe_style', posX: n }), 1, '0 — 1000'),
        number('Vertical', state.wipePosY ?? 500, 0, 1000,
          n => command({ cmd: 'wipe_style', posY: n }), 1, '0 — 1000'),
        choice('Center', () => command({ cmd: 'wipe_style', posX: 500, posY: 500 })));
    } else {
      title = 'Edge';
      entries.push(number('Border width', state.wipeBorder ?? 0, 0, 40,
        n => command({ cmd: 'wipe_style', border: n })),
        number('SOFT', state.wipeEdgeAmount ?? 0, 0, 40,
          n => command({ cmd: 'wipe_edge', mode: 'soft', amount: n })),
        color('Border color', state.wipeBorderColor, n => command({ cmd: 'wipe_style', color: n })));
      if (caps.asymmetricBorder) {
        for (const [side, label] of [[0, 'Centered'], [-1, 'Inner'], [1, 'Outer']])
          entries.push(choice(label, () => borderProfile(side), side === (state.wipeBorderSide ?? 0), 'Border placement'));
        entries.push(number('Inner soft', state.wipeInnerSoft ?? -1, -1, 100,
          n => borderProfile(undefined, n)),
          number('Outer soft', state.wipeOuterSoft ?? -1, -1, 100,
            n => borderProfile(undefined, undefined, n)));
      }
    }
  } else if (page === 'keys') {
    const rows = keyTarget === 'key' ? state.keys || [] : state.dsks || [];
    if (keySlot >= rows.length) keySlot = 0;
    const key = rows[keySlot], values = key?.processing || {};
    onAir = !!key?.on;
    hint = `${keyTarget === 'key' ? 'KEY' : 'DSK'} ${keySlot + 1} · ${onAir ? 'ON AIR' : 'OFF AIR'}`;
    menus = [['delegate', 'Delegate'], ['type', 'Type'], ['mask', 'Mask']];
    if (values.mode === 'luma') menus.push(['luma', 'Luma']);
    if (values.mode === 'chroma') menus.push(['chroma', 'Chroma']);
    if (!menus.some(row => row[0] === subpages.keys)) subpages.keys = 'type';
    if (subpages.keys === 'delegate') {
      title = 'Delegation';
      for (const [target, label] of [['key', 'Upstream'], ['dsk', 'Downstream']])
        entries.push(choice(label, () => { keyTarget = target; keySlot = 0; lastPaint = ''; paint(); }, target === keyTarget));
      rows.forEach((_, slot) => entries.push(choice(`${keyTarget === 'key' ? 'KEY' : 'DSK'} ${slot + 1}`,
        () => { keySlot = slot; lastPaint = ''; paint(); }, slot === keySlot)));
    } else if (subpages.keys === 'type') {
      title = 'Type';
      for (const mode of caps.keyModes || ['linear', 'chroma'])
        entries.push(choice(mode.toUpperCase(), () => keySave({ mode }), mode === (values.mode || 'linear')));
      if (caps.keyInversion) entries.push(choice('Invert', () => keySave({ invert: !values.invert }), !!values.invert));
    } else if (subpages.keys === 'mask') {
      title = 'Main mask';
      entries.push(choice('Mask on', () => keySave({ mask: !values.mask }), !!values.mask));
      for (const name of ['left', 'top', 'right', 'bottom']) entries.push(number(name.toUpperCase(),
        values[name] ?? 0, 0, 1, n => keySave({ [name]: n }), .01));
      if (caps.maskInversion) entries.push(choice('Invert mask', () => keySave({ maskInvert: !values.maskInvert }), !!values.maskInvert));
    } else if (subpages.keys === 'luma') {
      title = 'Luma key';
      entries.push(number('Low threshold', values.lumaLow ?? 0, 0, 1, n => keySave({ lumaLow: n }), .01),
        number('High threshold', values.lumaHigh ?? 1, 0, 1, n => keySave({ lumaHigh: n }), .01));
    } else {
      title = 'Chroma key';
      entries.push(number('Hue', values.hue ?? 120, 0, 360, n => keySave({ hue: n }), 1, 'Degrees'));
      for (const name of ['width', 'softness', 'saturation', 'brightness'])
        entries.push(number(name.toUpperCase(), values[name] ?? 0, 0, 1, n => keySave({ [name]: n }), .01));
    }
  } else {
    menus = [['effects', 'Effect'], ['background', 'Background']];
    if (subpages.dme === 'effects') {
      title = 'Effect';
      const effects = (caps.dmeEffects || []).filter(n => ['move', 'cube', 'zoom', 'page_curl', 'page_roll'].includes(n));
      if (caps.sonyDmeBackground) effects.push(...(caps.sonyDmes || []).map(code => `sony_${code}`));
      for (const name of effects) entries.push(choice(name.replaceAll('_', ' ').toUpperCase(),
        () => { effect = name; selectSubpage('background'); }, effect === name));
    } else {
      title = 'Background'; hint = effect.replaceAll('_', ' ').toUpperCase();
      const value = state.dmeBackgrounds?.[effect];
      if (caps.dmeBackground) {
        entries.push(choice('Black', () => command({ cmd: 'dme_background', effect, source: -1 }), value === -1),
          number('Source ID', Number.isInteger(value) ? value : -1, -1, 23,
            n => command({ cmd: 'dme_background', effect, source: n }), 1, 'Zero-based input ID'));
        if (caps.dmeBackgroundScopes) {
          const custom = !!state.dmeBackgroundScopes?.[effect];
          entries.push(choice('Global', () => command({ cmd: 'dme_background', effect, custom: false }), !custom),
            choice('Custom', () => command({ cmd: 'dme_background', effect, custom: true }), custom),
            choice('Copy global', () => command({ cmd: 'dme_background', effect, custom: true, copyGlobal: true })));
        }
      }
    }
  }
  return { title, hint, menus, entries, onAir };
}
function paint() {
  if (editing || touching || document.activeElement?.tagName === 'INPUT') return;
  const stamp = JSON.stringify([state, catalogue, page, subpages, keySlot, keyTarget, effect, matrixPage, innerWidth, innerHeight]);
  if (stamp === lastPaint) return;
  lastPaint = stamp; paintedMe = state.me || 0;
  const model = currentScreen();
  $('mes').replaceChildren(); $('rail').replaceChildren(); $('submenus').replaceChildren(); $('matrix').replaceChildren(); $('pagination').replaceChildren();
  for (let slot = 0; slot < (state.capabilities?.meCount || 4); slot++)
    $('mes').append(button(innerWidth < 600 ? String(slot + 1) : `M/E ${slot + 1}`, () => command({ cmd: 'me', slot }), slot === paintedMe));
  for (const [id, label] of Object.entries(categories)) $('rail').append(button(label, () => selectCategory(id), id === page));
  $('title').textContent = page === 'home' ? model.title : `M/E ${paintedMe + 1} › ${categories[page]} › ${model.title}`;
  $('context').textContent = model.hint;
  $('context').classList.toggle('onair', !!model.onAir);
  for (const [id, label] of model.menus) $('submenus').append(button(label, () => selectSubpage(id), id === subpages[page]));
  const box = $('matrix').getBoundingClientRect();
  const columns = Math.max(1, Math.min(6, Math.floor((box.width + 8) / 155)));
  const rows = Math.max(1, Math.min(6, Math.floor((box.height + 8) / 82)));
  const capacity = columns * rows, pages = Math.max(1, Math.ceil(model.entries.length / capacity));
  matrixPage = Math.max(0, Math.min(matrixPage, pages - 1));
  $('matrix').style.gridTemplateColumns = `repeat(${columns}, minmax(0, 1fr))`;
  $('matrix').style.gridTemplateRows = `repeat(${rows}, minmax(0, 1fr))`;
  if (pages > 1) {
    const previous = button('◀', () => { matrixPage--; lastPaint = ''; paint(); }); previous.disabled = matrixPage === 0;
    const next = button('▶', () => { matrixPage++; lastPaint = ''; paint(); }); next.disabled = matrixPage === pages - 1;
    const label = document.createElement('span'); label.textContent = `${matrixPage + 1} / ${pages}`;
    $('pagination').append(previous, label, next);
  }
  for (const entry of model.entries.slice(matrixPage * capacity, (matrixPage + 1) * capacity)) {
    const item = button('', entry.action || (() => {}), !!entry.selected);
    item.classList.add('tile', 'choice');
    const label = document.createElement('span'); label.className = 'label'; label.textContent = entry.label; item.append(label);
    if (entry.value !== undefined) { const value = document.createElement('strong'); value.className = 'value'; value.textContent = entry.value; item.append(value); }
    if (entry.color) {
      const picker = document.createElement('input'); picker.type = 'color'; picker.value = entry.color; picker.setAttribute('aria-label', entry.label);
      picker.onclick = event => event.stopPropagation(); picker.onchange = () => entry.save(picker.value);
      item.onclick = () => picker.click(); item.append(picker);
    }
    if (entry.detail) { const detail = document.createElement('span'); detail.className = 'detail'; detail.textContent = entry.detail; item.append(detail); }
    $('matrix').append(item);
  }
  $('back').disabled = page === 'home';
}
function openNumber(label, value, min, max, save, step) {
  editing = { label, value, min, max, save, step, me: paintedMe };
  draft = String(value); replace = true;
  $('editTitle').textContent = label;
  $('editHint').textContent = `Committed: ${value} · range ${min} to ${max} · step ${step}`;
  showDraft(); $('editor').showModal();
}
function showDraft() { $('draft').textContent = draft || '—'; }
function typeDigit(digit) { if (replace) { draft = ''; replace = false; } if (draft.length < 9) draft += digit; showDraft(); }
for (const digit of ['7', '8', '9', '4', '5', '6', '1', '2', '3', '±', '0', '.']) $('pad').append(button(digit, () => {
  if (digit === '±') { draft = draft.startsWith('-') ? draft.slice(1) : '-' + draft; replace = false; showDraft(); }
  else if (digit !== '.' || !draft.includes('.') || replace) typeDigit(digit);
}));
$('pad').append(button('Clear', () => { draft = ''; replace = false; showDraft(); }),
  button('⌫', () => { draft = draft.slice(0, -1); replace = false; showDraft(); }),
  button('Reset', () => { draft = String(editing.value); replace = true; showDraft(); }));
function cancelEdit() { $('editor').close(); editing = null; lastPaint = ''; paint(); }
$('cancel').onclick = cancelEdit;
$('editor').addEventListener('cancel', () => { editing = null; lastPaint = ''; paint(); });
$('commit').onclick = async () => {
  const number = Number(draft);
  if (!draft || !Number.isFinite(number) || number < editing.min || number > editing.max || Math.abs(number / editing.step - Math.round(number / editing.step)) > 1e-5) {
    $('editHint').textContent = `Invalid value · range ${editing.min} to ${editing.max} · step ${editing.step}`;
    replace = true; return;
  }
  const save = editing.save;
  if (await save(number)) cancelEdit();
};
$('home').onclick = () => selectCategory('home');
$('back').onclick = () => { if (page !== 'home' && subpages[page] !== currentScreen().menus[0]?.[0]) selectSubpage(currentScreen().menus[0][0]); else selectCategory('home'); };
$('fullscreen').onclick = async () => {
  try { if (document.fullscreenElement) await document.exitFullscreen(); else await document.documentElement.requestFullscreen(); }
  catch (_) { notify('Full screen is unavailable in this browser', true); }
};
async function poll() {
  try {
    const response = await fetch('/api/state', { cache: 'no-store' });
    if (!response.ok) throw Error('HTTP unavailable');
    const data = await response.json(); state = data.state || {}; catalogue = data.catalogue || [];
    const connected = data.transportConnected && Object.keys(state).length > 0;
    const ready = connected && !!state.capabilities?.touchPreparation;
    $('lost').hidden = ready;
    $('lost').querySelector('strong').textContent = connected && !ready ? 'UPDATE KAVTOR' : 'SERVER LOST';
    $('lost').querySelector('span').textContent = connected && !ready ? 'Touch preparation requires kavtor 0.26.0 or newer' : 'Waiting for kavtor';
    $('status').textContent = ready ? (state.connected ? 'CONNECTED' : 'ENGINE LOST') : 'SERVER LOST';
    paint();
  } catch (_) { $('lost').hidden = false; $('status').textContent = 'SERVER LOST'; }
}
document.addEventListener('pointerdown', () => { touching = true; });
for (const event of ['pointerup', 'pointercancel']) document.addEventListener(event, () => setTimeout(() => { touching = false; paint(); }, 0));
window.addEventListener('resize', () => { lastPaint = ''; paint(); });
document.addEventListener('fullscreenchange', () => { $('fullscreen').textContent = document.fullscreenElement ? 'Window' : 'Full screen'; lastPaint = ''; paint(); });
poll(); setInterval(poll, 500);
