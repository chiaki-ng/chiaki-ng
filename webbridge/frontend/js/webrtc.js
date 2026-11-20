/**
 * webrtc.js — RTCPeerConnection wrapper for the chiaki-web bridge.
 *
 * The bridge is ALWAYS the offerer (PROTOCOL.md): audio arrives as a remote
 * Opus track, video and input ride on pre-negotiated DataChannels that both
 * sides create with identical ids/reliability:
 *
 *   "video": negotiated, id 0, ordered, maxRetransmits 3  (binary fragments)
 *   "input": negotiated, id 2, reliable ordered           (JSON text)
 */

export class RtcSession {
  /**
   * @param {import('./signaling.js').Signaling} signaling
   * @param {RTCIceServer[]} [iceServers] — STUN/TURN minted by the bridge
   *   (Cloudflare TURN). Empty on LAN: host candidates suffice there.
   */
  constructor(signaling, iceServers = []) {
    this.signaling = signaling;
    this.isWs = false;
    this.pc = new RTCPeerConnection({ iceServers });
    if (iceServers.length) {
      signaling.log('[rtc] using ' + iceServers.length + ' ICE server entr' +
        (iceServers.length === 1 ? 'y' : 'ies') + ' (TURN fallback available)');
    }

    /** @type {(data: ArrayBuffer) => void} fired per video fragment */
    this.onVideoData = () => {};
    /** @type {(msg: object) => void} fired per input-DC JSON message */
    this.onInputMessage = () => {};
    /** @type {(state: string) => void} */
    this.onConnectionState = () => {};
    /** @type {() => void} first time both channels + ICE are up */
    this.onReady = () => {};

    this._readyFired = false;
    this._pendingCandidates = [];
    this._haveRemote = false;

    // Negotiated channels must exist before the SDP answer is generated so
    // the SCTP association carries them; ids/settings must mirror the bridge.
    this.videoDC = this.pc.createDataChannel('video', {
      negotiated: true,
      id: 0,
      ordered: true,
      maxRetransmits: 3,
    });
    this.videoDC.binaryType = 'arraybuffer';
    this.videoDC.onmessage = (ev) => {
      if (ev.data instanceof ArrayBuffer) this.onVideoData(ev.data);
    };
    this.videoDC.onopen = () => {
      this.signaling.log('[rtc] video DC open');
      this._maybeReady();
    };

    this.inputDC = this.pc.createDataChannel('input', {
      negotiated: true,
      id: 2,
    });
    this.inputDC.binaryType = 'arraybuffer';
    this.inputDC.onmessage = (ev) => {
      if (typeof ev.data !== 'string') return;
      try {
        const msg = JSON.parse(ev.data);
        if (msg && typeof msg === 'object') this.onInputMessage(msg);
      } catch (e) {
        /* malformed input message — ignore */
      }
    };
    this.inputDC.onopen = () => this._maybeReady();

    // Microphone PCM (browser -> bridge): unordered + lossy, mirrors the
    // bridge's negotiated channel id 4. 48 kHz mono s16le chunks.
    this.micDC = this.pc.createDataChannel('mic', {
      negotiated: true,
      id: 4,
      ordered: false,
      maxRetransmits: 0,
    });
    this.micDC.binaryType = 'arraybuffer';

    // Remote audio: hand the track to a hidden <audio> element and let the
    // browser's jitter buffer / FEC / PLC do the work.
    this.audioEl = document.createElement('audio');
    this.audioEl.autoplay = true;
    this.audioEl.setAttribute('playsinline', '');
    this.audioEl.style.display = 'none';
    document.body.appendChild(this.audioEl);
    this._audioBlocked = false;

    this.pc.ontrack = (ev) => {
      if (ev.track.kind !== 'audio') return;
      this.signaling.log('[audio] track received (muted=' + ev.track.muted + ')');
      ev.track.onunmute = () => this.signaling.log('[audio] track unmuted (RTP flowing)');
      const stream = ev.streams && ev.streams[0] ? ev.streams[0] : new MediaStream([ev.track]);
      this.audioEl.srcObject = stream;
      this._tryPlayAudio();
    };

    this.pc.onicecandidate = (ev) => {
      if (ev.candidate && ev.candidate.candidate) {
        this.signaling.candidate(ev.candidate.candidate, ev.candidate.sdpMid || '0');
      }
    };

    this.pc.onconnectionstatechange = () => {
      this.onConnectionState(this.pc.connectionState);
      if (this.pc.connectionState === 'connected') {
        this._logSelectedPair();
        this._maybeReady();
      }
    };
  }

  /** Telemetry: which candidate pair won (host = direct, relay = TURN). */
  async _logSelectedPair() {
    try {
      const stats = await this.pc.getStats();
      let pair = null;
      stats.forEach((s) => {
        if (s.type === 'transport' && s.selectedCandidatePairId) pair = stats.get(s.selectedCandidatePairId);
      });
      if (!pair) {
        stats.forEach((s) => {
          if (s.type === 'candidate-pair' && (s.selected || s.nominated) && s.state === 'succeeded') pair = s;
        });
      }
      if (!pair) return;
      const local = stats.get(pair.localCandidateId);
      const remote = stats.get(pair.remoteCandidateId);
      this.signaling.log('[rtc] selected pair: local=' + (local && local.candidateType) +
        ' remote=' + (remote && remote.candidateType));
    } catch { /* stats unavailable */ }
  }

  _maybeReady() {
    if (this._readyFired) return;
    if (
      this.videoDC.readyState === 'open' &&
      this.inputDC.readyState === 'open' &&
      (this.pc.connectionState === 'connected' || this.pc.connectionState === 'connecting')
    ) {
      this._readyFired = true;
      this.onReady();
    }
  }

  _tryPlayAudio() {
    const p = this.audioEl.play();
    if (p && p.catch) {
      p.then(() => {
        this._audioBlocked = false;
        this.signaling.log('[audio] playing');
      }).catch((e) => {
        // Autoplay policy (iOS Safari & friends): retry on first gesture.
        this._audioBlocked = true;
        this.signaling.log('[audio] autoplay blocked: ' + (e && e.name));
      });
    }
  }

  /**
   * Call from any user-gesture handler; unblocks audio if autoplay was denied.
   */
  resumeAudio() {
    if (this._audioBlocked || this.audioEl.paused) this._tryPlayAudio();
  }

  /**
   * Handle the bridge's SDP offer: setRemote → createAnswer → setLocal →
   * signal the answer. Flushes any candidates that raced the offer.
   * @param {string} sdp
   */
  async acceptOffer(sdp) {
    await this.pc.setRemoteDescription({ type: 'offer', sdp });
    this._haveRemote = true;
    const answer = await this.pc.createAnswer();
    await this.pc.setLocalDescription(answer);
    this.signaling.answer(this.pc.localDescription.sdp);
    for (const c of this._pendingCandidates) this._addCandidate(c);
    this._pendingCandidates = [];
  }

  /**
   * Trickled remote candidate from the bridge.
   * @param {string} candidate @param {string} mid
   */
  addRemoteCandidate(candidate, mid) {
    const init = { candidate, sdpMid: mid };
    if (!this._haveRemote) {
      this._pendingCandidates.push(init);
      return;
    }
    this._addCandidate(init);
  }

  _addCandidate(init) {
    this.pc.addIceCandidate(init).catch((e) => {
      console.warn('[webrtc] addIceCandidate failed:', e && e.message);
    });
  }

  /**
   * Send a JSON message on the input DC.
   * @param {object} msg — e.g. {t:'cs',...} / {t:'idr'} / {t:'pin',...}
   * @returns {boolean} true if the channel accepted it
   */
  sendInput(msg) {
    if (this.inputDC.readyState !== 'open') return false;
    try {
      this.inputDC.send(JSON.stringify(msg));
      return true;
    } catch (e) {
      return false;
    }
  }

  /**
   * Send a microphone PCM chunk (Int16Array buffer) on the mic DC.
   * @param {ArrayBuffer} buf
   * @returns {boolean}
   */
  sendMic(buf) {
    if (!this.micDC || this.micDC.readyState !== 'open') return false;
    try {
      this.micDC.send(buf);
      return true;
    } catch (e) {
      return false;
    }
  }

  /** Bytes received on the video DC so far (for the HUD bitrate estimate). */
  async getVideoBytesReceived() {
    let bytes = 0;
    try {
      const stats = await this.pc.getStats();
      stats.forEach((s) => {
        if (s.type === 'data-channel' && s.label === 'video') {
          bytes = s.bytesReceived || 0;
        }
      });
    } catch (e) {
      /* stats unavailable */
    }
    return bytes;
  }

  close() {
    try {
      this.videoDC.close();
    } catch (e) { /* noop */ }
    try {
      this.inputDC.close();
    } catch (e) { /* noop */ }
    try {
      this.pc.close();
    } catch (e) { /* noop */ }
    if (this.audioEl) {
      this.audioEl.srcObject = null;
      this.audioEl.remove();
    }
  }
}
