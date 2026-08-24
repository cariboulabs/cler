export function buildModel(controls, state) {
  return (controls || []).map((c) => ({
    id: c.id,
    label: c.label || c.id,
    type: c.type || 'range',
    min: c.min, max: c.max, step: c.step || 1,
    options: c.options || [],
    // parallel to options: "" where usable, the server's reason where not
    options_disabled: c.options_disabled || [],
    unit: c.unit || '',
    ro: !!c.ro,
    value: state && state[c.id] !== undefined ? state[c.id] : c.value
  }));
}

export function applyState(model, state) {
  for (const c of model) if (state[c.id] !== undefined) c.value = state[c.id];
  return model;
}

export function coerce(control, raw) {
  if (control.type === 'bool') return !!raw;
  if (control.type === 'enum') return String(raw);
  let v = Number(raw);
  if (!Number.isFinite(v)) return null;
  if (control.min !== undefined) v = Math.max(control.min, v);
  if (control.max !== undefined) v = Math.min(control.max, v);
  return v;
}

// A read-only control is a value, not a dead widget: rule 1 wants the reason and
// rule 5 wants the number in monospace.
export const RO_REASON = {
  rate: 'fixed when the source was opened — reconnect to change it',
  freq: 'read-only for this source'
};
export function roReason(id) { return RO_REASON[id] || 'read-only for this source'; }

export function fmtValue(c) {
  if (c.value === undefined || c.value === null) return '—';
  if (c.type === 'bool') return c.value ? 'on' : 'off';
  if (typeof c.value === 'number') return c.value.toLocaleString('en-US');
  return String(c.value);
}

export function render(root, model, onSet) {
  root.textContent = '';
  for (const c of model) {
    const row = document.createElement(c.ro ? 'div' : 'label');
    row.className = 'ctl';
    row.dataset.id = c.id;
    const name = document.createElement('span');
    name.className = 'ctl-name';
    name.textContent = c.unit ? `${c.label} (${c.unit})` : c.label;
    row.appendChild(name);
    if (c.ro) {
      const v = document.createElement('span');
      v.className = 'ctl-value mono';
      v.dataset.testid = `ctl-${c.id}-value`;
      v.textContent = fmtValue(c);
      const why = document.createElement('span');
      why.className = 'ctl-note';
      why.textContent = roReason(c.id);
      row.append(v, why);
      root.appendChild(row);
      continue;
    }
    let input;
    if (c.type === 'bool') {
      input = document.createElement('input');
      input.type = 'checkbox';
      input.checked = !!c.value;
      input.onchange = () => onSet(c.id, input.checked);
    } else if (c.type === 'enum') {
      input = document.createElement('select');
      for (const o of c.options) {
        const opt = document.createElement('option');
        opt.value = o; opt.textContent = o; opt.selected = o === c.value;
        input.appendChild(opt);
      }
      input.onchange = () => onSet(c.id, input.value);
    } else {
      input = document.createElement('input');
      input.type = 'number';
      if (c.min !== undefined) input.min = c.min;
      if (c.max !== undefined) input.max = c.max;
      input.step = c.step;
      input.value = c.value ?? '';
      input.onchange = () => { const v = coerce(c, input.value); if (v !== null) onSet(c.id, v); };
      if (c.min !== undefined && c.max !== undefined) {
        const slider = document.createElement('input');
        slider.type = 'range'; slider.min = c.min; slider.max = c.max; slider.step = c.step; slider.value = c.value ?? c.min;
        slider.dataset.testid = `ctl-${c.id}-slider`;
        slider.dataset.ctl = c.id;
        slider.oninput = () => { input.value = slider.value; };
        slider.onchange = () => onSet(c.id, Number(slider.value));
        row.appendChild(slider);
      }
    }
    input.className = 'ctl-input';
    input.dataset.testid = `ctl-${c.id}`;
    input.dataset.ctl = c.id;
    row.appendChild(input);
    root.appendChild(row);
  }
}

export function update(root, model) {
  for (const c of model) {
    const row = root.querySelector(`[data-id="${CSS.escape(c.id)}"]`);
    if (!row) continue;
    if (c.ro) {
      const v = row.querySelector('.ctl-value');
      if (v) v.textContent = fmtValue(c);
      continue;
    }
    const input = row.querySelector('.ctl-input');
    const slider = row.querySelector('input[type=range]');
    if (!input) continue;
    if (document.activeElement === input || document.activeElement === slider) continue;
    if (c.type === 'bool') input.checked = !!c.value;
    else input.value = c.value ?? '';
    if (slider) slider.value = c.value ?? slider.min;
  }
}
