// Keyboard map. Pure so the bindings can be tested without a browser: typing in a
// field must never tune the radio, so anything with a text target is ignored.
const TYPING = new Set(['INPUT', 'SELECT', 'TEXTAREA']);

export function keyAction(e, tagName) {
  const typing = TYPING.has((tagName || '').toUpperCase());
  if (e.key === 'Escape') return { action: 'close' };
  if (typing) return null;
  if (e.ctrlKey || e.metaKey || e.altKey) return null;
  switch (e.key) {
    case 'ArrowLeft': return { action: 'tune', steps: e.shiftKey ? -10 : -1 };
    case 'ArrowRight': return { action: 'tune', steps: e.shiftKey ? 10 : 1 };
    case ' ': return { action: 'listen' };
    case 'r': case 'R': return { action: 'record' };
    case '/': return { action: 'focusFreq' };
    default: return null;
  }
}

// One arrow press moves a fraction of what is on screen, so the step follows the
// zoom instead of being a constant that is wrong at both ends.
export function tuneStepHz(rateHz, viewSpan) {
  const visible = (Number(rateHz) || 0) * (Number(viewSpan) || 1);
  return Math.max(1, Math.round(visible / 100));
}
