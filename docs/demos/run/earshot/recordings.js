// relative, not /client/…: the same specifier then resolves in the browser and
// under `node --test`, which is what keeps these functions testable
import { fmtBytes, fmtDuration } from './proto.js';

// What a capture is worth showing: what it is, how big, how old, and the two
// things you can do with it from here.
export function recordingRows(recs, role) {
  const ctl = role === 'ctl';
  return (recs || []).map((r) => ({
    name: r.name,
    dir: r.dir || '',
    meta: `${fmtBytes(r.bytes)} · ${fmtDuration(r.seconds)} · ${(r.freq / 1e6).toFixed(3)} MHz @ ${(r.rate / 1e6).toFixed(2)} MS/s · ${fmtAge(r.age_s)}`,
    canDelete: ctl,
    deleteReason: ctl ? '' : 'only the controller can delete a recording',
    canPlay: ctl,
    playReason: ctl ? '' : 'only the controller can change the source'
  }));
}

export function fmtAge(sec) {
  const s = Math.max(0, Math.floor(Number(sec) || 0));
  if (s < 90) return `${s}s ago`;
  if (s < 5400) return `${Math.round(s / 60)}m ago`;
  if (s < 172800) return `${Math.round(s / 3600)}h ago`;
  return `${Math.round(s / 86400)}d ago`;
}

// One line per configured directory: where captures land and how much room is left.
export function dirSummary(dir, maxBytes) {
  if (!dir) return '';
  const cap = maxBytes > 0 ? ` · cap ${fmtBytes(maxBytes)}` : '';
  return `${fmtBytes(dir.free_bytes)} free of ${fmtBytes(dir.total_bytes)}${cap}${dir.writable === false ? ' · not writable' : ''}`;
}

// The next thing an operator does with a capture is fetch it, and that is scp,
// so hand them the line rather than the idea of the line.
export function scpLine(host, dirPath, name) {
  const p = `${dirPath.replace(/\/$/, '')}/${name || '<name>'}.sigmf-*`;
  return `scp ${host}:${p} .`;
}

// A file that vanished because the cap was reached should not be a mystery.
export function prunedNote(prunedBytes) {
  return prunedBytes > 0 ? `${fmtBytes(prunedBytes)} pruned this run (oldest first, cap reached)` : '';
}
