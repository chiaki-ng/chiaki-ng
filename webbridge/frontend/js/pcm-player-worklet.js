/**
 * pcm-player-worklet.js — jitter-buffered PCM playback for the WebSocket
 * transport (there is no RTP track to lean on, so buffering/concealment that
 * the browser normally does for WebRTC audio happens here).
 *
 * Receives interleaved s16le Int16Array chunks (any length) via port messages:
 *   { pcm: Int16Array }                      — audio data
 *   { config: { rate, channels } }           — source format (before first pcm)
 *
 * Policy: hold START_S of audio before starting (and after every underrun),
 * and if the queue grows past MAX_S (slow tab, TCP burst after stall) drop
 * down to TARGET_S so latency cannot creep up permanently.
 */

const START_S = 0.08;
const TARGET_S = 0.12;
const MAX_S = 0.45;

class PcmPlayerProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.inRate = 48000;
    this.channels = 2;
    /** @type {Float32Array[][]} deinterleaved, resampled chunks */
    this.queue = [];
    this.queued = 0; // frames currently queued
    this.readOff = 0; // frames consumed from queue[0]
    this.playing = false;
    this._resampPos = 0;
    /** @type {Float32Array[]|null} last frame of previous chunk (resampler seam) */
    this._tail = null;

    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.config) {
        this.inRate = m.config.rate || 48000;
        this.channels = m.config.channels || 2;
        return;
      }
      if (m.pcm) this._push(m.pcm);
    };
  }

  /** @param {Int16Array} pcm interleaved s16 */
  _push(pcm) {
    const ch = this.channels;
    const inFrames = Math.floor(pcm.length / ch);
    if (inFrames === 0) return;

    const ratio = this.inRate / sampleRate;
    const outFrames = ratio === 1 ? inFrames : Math.max(1, Math.round(inFrames / ratio));
    const out = [];
    for (let c = 0; c < ch; c++) out.push(new Float32Array(outFrames));

    if (ratio === 1) {
      for (let i = 0; i < inFrames; i++) {
        for (let c = 0; c < ch; c++) out[c][i] = pcm[i * ch + c] / 32768;
      }
    } else {
      // Linear resample; good enough for a fallback voice/game-audio path.
      for (let i = 0; i < outFrames; i++) {
        const pos = i * ratio;
        const i0 = Math.min(inFrames - 1, Math.floor(pos));
        const i1 = Math.min(inFrames - 1, i0 + 1);
        const frac = pos - i0;
        for (let c = 0; c < ch; c++) {
          const a = pcm[i0 * ch + c] / 32768;
          const b = pcm[i1 * ch + c] / 32768;
          out[c][i] = a + (b - a) * frac;
        }
      }
    }

    this.queue.push(out);
    this.queued += outFrames;

    // Latency cap: shed oldest audio down to TARGET_S.
    const maxFrames = MAX_S * sampleRate;
    if (this.queued > maxFrames) {
      const targetFrames = TARGET_S * sampleRate;
      while (this.queued - (this.queue[0][0].length - this.readOff) > targetFrames && this.queue.length > 1) {
        this.queued -= this.queue[0][0].length - this.readOff;
        this.queue.shift();
        this.readOff = 0;
      }
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const frames = out[0].length;
    const outCh = out.length;

    if (!this.playing) {
      if (this.queued < START_S * sampleRate) return true; // silence
      this.playing = true;
    }
    if (this.queued === 0) {
      this.playing = false; // underrun: rebuffer
      return true;
    }

    let written = 0;
    while (written < frames && this.queue.length > 0) {
      const chunk = this.queue[0];
      const avail = chunk[0].length - this.readOff;
      const n = Math.min(avail, frames - written);
      for (let c = 0; c < outCh; c++) {
        const src = chunk[Math.min(c, this.channels - 1)];
        out[c].set(src.subarray(this.readOff, this.readOff + n), written);
      }
      written += n;
      this.readOff += n;
      this.queued -= n;
      if (this.readOff >= chunk[0].length) {
        this.queue.shift();
        this.readOff = 0;
      }
    }
    return true;
  }
}

registerProcessor('pcm-player', PcmPlayerProcessor);
