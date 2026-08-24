// A failed connect must survive long enough to be read, so a source error is
// remembered against the device it names and rendered under that row instead of
// a toast that fades. Everything else still goes to the toast.
export function applyError(errors, m) {
  if (m.code !== 'source' || !m.id) return { errors, toast: true };
  return { errors: { ...errors, [m.id]: m.msg }, toast: false };
}

export function deviceRows(sources, errors, currentSource, role) {
  return sources.map((s) => ({
    id: s.id,
    label: s.label || s.id,
    kind: s.kind || '',
    connected: currentSource === s.id,
    // a failed row keeps its button: the fix is usually outside the app
    connectable: currentSource !== s.id && s.available !== false && role === 'ctl',
    why: errors[s.id] || ''
  }));
}
