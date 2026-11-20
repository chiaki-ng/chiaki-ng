/**
 * mic-worklet.js — AudioWorkletProcessor for microphone capture.
 *
 * Downmixes to mono, resamples to 48 kHz if the context runs at another rate
 * (linear interpolation — fine for voice), converts to s16le, and posts one
 * 480-sample (10 ms) Int16Array buffer at a time. The chunk size matches the
 * frame the bridge's Opus encoder needs, so no re-framing happens later.
 */

const TARGET_RATE = 48000;
const FRAME_SAMPLES = 480;

class MicProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ratio = sampleRate / TARGET_RATE; // input samples per output sample
    this.pos = 0; // fractional read position into the stream, in input samples
    this.tail = new Float32Array(0); // unconsumed input
    this.out = new Int16Array(FRAME_SAMPLES);
    this.outLen = 0;
    this.muted = false;
    this.port.onmessage = (e) => {
      if (e.data && e.data.type === 'mute') this.muted = !!e.data.muted;
    };
  }

  /**
   * @param {Float32Array[][]} inputs
   */
  process(inputs) {
    const input = inputs[0];
    if (!input || input.length === 0 || this.muted) return true;

    // Downmix all capture channels to mono.
    const ch0 = input[0];
    let mono = ch0;
    if (input.length > 1) {
      mono = new Float32Array(ch0.length);
      for (let c = 0; c < input.length; c++) {
        const ch = input[c];
        for (let i = 0; i < ch.length; i++) mono[i] += ch[i];
      }
      for (let i = 0; i < mono.length; i++) mono[i] /= input.length;
    }

    // Append to the unconsumed tail.
    const buf = new Float32Array(this.tail.length + mono.length);
    buf.set(this.tail, 0);
    buf.set(mono, this.tail.length);

    // Consume at `ratio` input samples per output sample.
    let p = this.pos;
    while (p + this.ratio < buf.length) {
      const i0 = Math.floor(p);
      const frac = p - i0;
      const s = buf[i0] * (1 - frac) + buf[i0 + 1] * frac;
      const v = Math.max(-1, Math.min(1, s));
      this.out[this.outLen++] = (v * 32767) | 0;
      if (this.outLen === FRAME_SAMPLES) {
        // Transfer a copy; keep our working buffer.
        const chunk = this.out.slice();
        this.port.postMessage({ type: 'pcm', buf: chunk.buffer }, [chunk.buffer]);
        this.outLen = 0;
      }
      p += this.ratio;
    }

    // Keep only what's not consumed yet.
    const keepFrom = Math.floor(p);
    this.tail = buf.slice(keepFrom);
    this.pos = p - keepFrom;
    return true;
  }
}

registerProcessor('mic-processor', MicProcessor);
