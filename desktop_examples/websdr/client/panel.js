export function buildModel(controls, state) {
  return (controls || []).map((c) => ({
    id: c.id,
    label: c.label || c.id,
    type: c.type || 'range',
    min: c.min, max: c.max, step: c.step || 1,
    options: c.options || [],
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

export function render(root, model, onSet) {
  root.textContent = '';
  for (const c of model) {
    const row = document.createElement('label');
    row.className = 'ctl';
    row.dataset.id = c.id;
    const name = document.createElement('span');
    name.className = 'ctl-name';
    name.textContent = c.unit ? `${c.label} (${c.unit})` : c.label;
    row.appendChild(name);
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
        slider.disabled = c.ro;
        slider.oninput = () => { input.value = slider.value; };
        slider.onchange = () => onSet(c.id, Number(slider.value));
        row.appendChild(slider);
      }
    }
    input.disabled = c.ro;
    input.className = 'ctl-input';
    row.appendChild(input);
    root.appendChild(row);
  }
}

export function update(root, model) {
  for (const c of model) {
    const row = root.querySelector(`[data-id="${CSS.escape(c.id)}"]`);
    if (!row) continue;
    const input = row.querySelector('.ctl-input');
    const slider = row.querySelector('input[type=range]');
    if (document.activeElement === input || document.activeElement === slider) continue;
    if (c.type === 'bool') input.checked = !!c.value;
    else input.value = c.value ?? '';
    if (slider) slider.value = c.value ?? slider.min;
  }
}
