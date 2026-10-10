/**
 * ws-transport.js — WebSocket media transport (fallback for networks where
 * WebRTC cannot connect: UDP-hostile wifi, symmetric NATs beyond TURN).
 *
 * Everything rides the signaling WebSocket. Binary messages carry a 1-byte
 * channel prefix (PROTOCOL.md "WebSocket transport"):
 *   0x01 bridge→browser  video: 17-byte fragment header + Annex-B AU
 *   0x02 bridge→browser  audio: interleaved s16le PCM (bridge decodes Opus)
 *   0x03 browser→bridge  mic:   48 kHz mono s16le PCM
 * Input/events are JSON text on the same socket ("t"-keyed).
 *
 * Exposes the same surface as RtcSession so app.js can treat both alike.
 */

const CHAN_VIDEO = 0x01;
const CHAN_AUDIO = 0x02;
const CHAN_MIC = 0x03;

export class WsSession {
  /** @param {import('./signaling.js').Signaling} signaling */
  constructor(signaling) {
    this.signaling = signaling;
    this.isWs = true;

    /** @type {(data: ArrayBuffer) => void} fired per video fragment */
    this.onVideoData = () => {};
    /** @type {(msg: object) => void} input/event message from the bridge */
    this.onInputMessage = () => {};
    /** @type {(state: string) => void} */
    this.onConnectionState = () => {};
    /** @type {() => void} */
    this.onReady = () => {};

    this._readyFired = false;
    this._videoBytes = 0;
    this._audio = new PcmPlayer((msg) => signaling.log(msg));

    signaling.onbinary = (buf) => this._onBinary(buf);
  }

  _onBinary(buf) {
    if (buf.byteLength < 1) return;
    const chan = new Uint8Array(buf, 0, 1)[0];
    if (chan === CHAN_VIDEO) {
      this._videoBytes += buf.byteLength - 1;
      this.onVideoData(buf.slice(1));
    } else if (chan === CHAN_AUDIO) {
      this._audio.push(buf);
    }
  }

  /** Bridge signaling that concerns the transport. @param {object} msg */
  handleSignal(msg) {
    if (msg.type === 'audioInfo') {
      this._audio.configure(msg.rate || 48000, msg.channels || 2);
    } else if (msg.type === 'status' && msg.state === 'connected') {
      if (!this._readyFired) {
        this._readyFired = true;
        this.onConnectionState('connected');
        this.onReady();
      }
    }
  }

  /** @param {object} msg @returns {boolean} */
  sendInput(msg) {
    if (!this.signaling.ws || this.signaling.ws.readyState !== WebSocket.OPEN) return false;
    this.signaling.send(msg); // "t"-keyed: the bridge routes it to input
    return true;
  }

  /** @param {ArrayBuffer} buf mic PCM chunk @returns {boolean} */
  sendMic(buf) {
    const msg = new Uint8Array(1 + buf.byteLength);
    msg[0] = CHAN_MIC;
    msg.set(new Uint8Array(buf), 1);
    return this.signaling.sendBinary(msg);
  }

  resumeAudio() {
    this._audio.resume();
  }

  async getVideoBytesReceived() {
    return this._videoBytes;
  }

  close() {
    this.signaling.onbinary = () => {};
    this._audio.close();
  }
}

/**
 * Raw-PCM playback via an AudioWorklet with its own jitter buffer (see
 * pcm-player-worklet.js). Tolerates configure()/push() in any order and
 * before the (async) worklet finishes loading.
 */
class PcmPlayer {
  /** @param {(msg: string) => void} log */
  constructor(log) {
    this.log = log;
    this.ctx = null;
    this.node = null;
    this.rate = 48000;
    this.channels = 2;
    this._starting = null;
    /** @type {ArrayBuffer[]} audio received while the worklet loads */
    this._preBuffer = [];
    this.closed = false;
  }

  configure(rate, channels) {
    this.rate = rate;
    this.channels = channels;
    if (this.node) {
      this.node.port.postMessage({ config: { rate, channels } });
    }
  }

  /** @param {ArrayBuffer} buf channel-prefixed audio message */
  push(buf) {
    if (this.closed) return;
    if (!this.node) {
      if (!this._starting) this._starting = this._start();
      if (this._preBuffer.length < 32) this._preBuffer.push(buf);
      return;
    }
    this._post(buf);
  }

  _post(buf) {
    // Odd payload would misalign Int16Array; trim the prefix via slice.
    const pcm = new Int16Array(buf.slice(1));
    this.node.port.postMessage({ pcm }, [pcm.buffer]);
  }

  async _start() {
    try {
      const Ctx = window.AudioContext || window.webkitAudioContext;
      this.ctx = new Ctx({ sampleRate: this.rate, latencyHint: 'interactive' });
    } catch {
      this.ctx = new (window.AudioContext || window.webkitAudioContext)();
    }
    try {
      await this.ctx.audioWorklet.addModule('js/pcm-player-worklet.js');
    } catch (e) {
      this.log('[audio] pcm worklet load failed: ' + (e && e.message));
      return;
    }
    if (this.closed) return;
    this.node = new AudioWorkletNode(this.ctx, 'pcm-player', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [Math.max(2, this.channels)],
    });
    this.node.connect(this.ctx.destination);
    this.node.port.postMessage({ config: { rate: this.rate, channels: this.channels } });
    this.log('[audio] pcm player started (ctx rate ' + this.ctx.sampleRate + ')');
    for (const b of this._preBuffer) this._post(b);
    this._preBuffer = [];
    if (this.ctx.state !== 'running') {
      this.log('[audio] context suspended — waiting for gesture');
    }
  }

  /** Call from a user-gesture handler (iOS autoplay policy). */
  resume() {
    if (this.ctx && this.ctx.state !== 'running') {
      this.ctx.resume().then(() => this.log('[audio] context resumed')).catch(() => {});
    }
  }

  close() {
    this.closed = true;
    if (this.node) {
      try { this.node.disconnect(); } catch { /* gone */ }
      this.node = null;
    }
    if (this.ctx) {
      this.ctx.close().catch(() => {});
      this.ctx = null;
    }
  }
}
