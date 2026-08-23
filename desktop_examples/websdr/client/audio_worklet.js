// Jitter buffer: hold TARGET samples before playing, go silent on underrun until refilled, drop oldest past MAX.
const TARGET = 4800;
const MAX = 14400;
const CAP = 1 << 16;

class PcmPlayer extends AudioWorkletProcessor {
  constructor() {
    super();
    this.buf = new Float32Array(CAP);
    this.r = 0; this.w = 0; this.count = 0;
    this.refilling = true;
    this.underruns = 0;
    this.dropped = 0;
    this.gain = 1;
    this.lastReport = 0;
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.flush) { this.r = this.w = this.count = 0; this.refilling = true; return; }
      if (m.gain !== undefined) { this.gain = m.gain; return; }
      const pcm = m.pcm;
      for (let i = 0; i < pcm.length; i++) {
        if (this.count >= MAX) { this.r = (this.r + 1) % CAP; this.count--; this.dropped++; }
        this.buf[this.w] = pcm[i] / 32768;
        this.w = (this.w + 1) % CAP;
        this.count++;
      }
      if (this.refilling && this.count >= TARGET) this.refilling = false;
    };
  }
  process(inputs, outputs) {
    const out = outputs[0][0];
    if (this.refilling || this.count < out.length) {
      if (!this.refilling) { this.refilling = true; this.underruns++; }
      out.fill(0);
    } else {
      for (let i = 0; i < out.length; i++) { out[i] = this.buf[this.r] * this.gain; this.r = (this.r + 1) % CAP; }
      this.count -= out.length;
    }
    if (currentTime - this.lastReport > 0.25) {
      this.lastReport = currentTime;
      this.port.postMessage({ bufferedMs: Math.round(this.count / sampleRate * 1000), underruns: this.underruns, dropped: this.dropped });
    }
    return true;
  }
}

registerProcessor('pcm-player', PcmPlayer);
