const TARGET = 4800;
const MAX = 14400;
const CAP = 1 << 16;

class PcmPlayer extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.buf = new Float32Array(CAP);
    this.r = 0; this.w = 0; this.count = 0;
    this.refilling = true;
    this.underruns = 0;
    this.dropped = 0;
    this.gain = 1;
    this.lastReport = 0;
    this.step = (options.processorOptions?.inputRate || sampleRate) / sampleRate;
    this.pos = 0; this.prev = 0;
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.flush) { this.r = this.w = this.count = 0; this.refilling = true; this.pos = 0; return; }
      if (m.gain !== undefined) { this.gain = m.gain; return; }
      const pcm = m.pcm;
      if (this.step === 1) { for (let i = 0; i < pcm.length; i++) this.put(pcm[i] / 32768); }
      else {
        while (this.pos < pcm.length) {
          const i = Math.floor(this.pos), f = this.pos - i;
          const a = i === 0 ? this.prev : pcm[i - 1] / 32768, b = pcm[i] / 32768;
          this.put(a + (b - a) * f);
          this.pos += this.step;
        }
        this.pos -= pcm.length;
        this.prev = pcm[pcm.length - 1] / 32768;
      }
      if (this.refilling && this.count >= TARGET) this.refilling = false;
    };
  }
  put(v) {
    if (this.count >= MAX) { this.r = (this.r + 1) % CAP; this.count--; this.dropped++; }
    this.buf[this.w] = v;
    this.w = (this.w + 1) % CAP;
    this.count++;
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
