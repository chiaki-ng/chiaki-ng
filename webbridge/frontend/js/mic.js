/**
 * mic.js — microphone capture for voice chat.
 *
 * getUserMedia → AudioWorklet (mono, 48 kHz s16le, 480-sample chunks) →
 * mic DataChannel. The bridge feeds the PCM into libchiaki's Opus encoder,
 * which produces the exact frames the console requires.
 */

export class MicCapture {
  /**
   * @param {{ send: (buf: ArrayBuffer) => boolean,
   *           log: (msg: string) => void }} cbs
   */
  constructor(cbs) {
    this.cbs = cbs;
    this.active = false;
    /** @type {MediaStream|null} */ this.stream = null;
    /** @type {AudioContext|null} */ this.ctx = null;
    /** @type {AudioWorkletNode|null} */ this.node = null;
  }

  /** @returns {boolean} whether capture is running */
  get running() {
    return this.active;
  }

  /**
   * Ask for the mic and start streaming PCM. Must be called from a user
   * gesture on iOS. Throws with a user-presentable message on failure.
   */
  async start() {
    if (this.active) return;
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      throw new Error('Microphone capture not available (needs HTTPS)');
    }
    try {
      this.stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          echoCancellation: true,
          noiseSuppression: true,
          autoGainControl: true,
          channelCount: 1,
        },
      });
    } catch (e) {
      throw new Error('Microphone permission denied (' + (e && e.name) + ')');
    }

    try {
      // Ask for 48 kHz; if the engine refuses, the worklet resamples.
      try {
        this.ctx = new AudioContext({ sampleRate: 48000 });
      } catch (e) {
        this.ctx = new AudioContext();
      }
      if (this.ctx.state === 'suspended') await this.ctx.resume();
      await this.ctx.audioWorklet.addModule(new URL('./mic-worklet.js', import.meta.url));
      const src = this.ctx.createMediaStreamSource(this.stream);
      this.node = new AudioWorkletNode(this.ctx, 'mic-processor', {
        numberOfInputs: 1,
        numberOfOutputs: 0,
      });
      this.node.port.onmessage = (e) => {
        if (e.data && e.data.type === 'pcm') this.cbs.send(e.data.buf);
      };
      src.connect(this.node);
      this.active = true;
      this.cbs.log('[mic] capture started (ctx rate ' + this.ctx.sampleRate + ')');
    } catch (e) {
      this.stop();
      throw new Error('Microphone pipeline failed: ' + (e && e.message));
    }
  }

  stop() {
    this.active = false;
    if (this.node) {
      try {
        this.node.port.postMessage({ type: 'mute', muted: true });
        this.node.disconnect();
      } catch (e) { /* already gone */ }
      this.node = null;
    }
    if (this.ctx) {
      this.ctx.close().catch(() => {});
      this.ctx = null;
    }
    if (this.stream) {
      for (const t of this.stream.getTracks()) t.stop();
      this.stream = null;
    }
    this.cbs.log('[mic] capture stopped');
  }
}
