export class AudioPlayer {
  constructor() {
    this.ctx = null;
    this.node = null;
    this.stats = { bufferedMs: 0, underruns: 0, dropped: 0 };
    this.gain = 1;
    this.pending = [];
  }
  get running() { return !!this.ctx && this.ctx.state === 'running'; }
  async start() {
    if (!this.ctx) {
      this.ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 48000 });
      await this.ctx.audioWorklet.addModule('/client/audio_worklet.js');
      this.node = new AudioWorkletNode(this.ctx, 'pcm-player', { outputChannelCount: [1] });
      this.node.port.onmessage = (e) => { this.stats = e.data; };
      this.node.connect(this.ctx.destination);
      this.node.port.postMessage({ gain: this.gain });
    }
    if (this.ctx.state !== 'running') await this.ctx.resume();
  }
  push(pcm) {
    if (!this.node) return;
    this.node.port.postMessage({ pcm }, [pcm.buffer]);
  }
  flush() { if (this.node) this.node.port.postMessage({ flush: true }); }
  setGain(g) { this.gain = g; if (this.node) this.node.port.postMessage({ gain: g }); }
  async suspend() { if (this.ctx) await this.ctx.suspend(); }
}
