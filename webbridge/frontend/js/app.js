/**
 * app.js — chiaki-web UI flow.
 *
 * Console list view (discovery, wake, register, settings) and the stream
 * view (WebRTC session, video worker, input, HUD). Everything talks to the
 * bridge per webbridge/PROTOCOL.md.
 */

import { Signaling, api } from './signaling.js';
import { RtcSession } from './webrtc.js';
import { WsSession } from './ws-transport.js';
import { VideoPipeline } from './video.js';
import { InputManager, KEY_MAP, KEY_HELP_EXTRA } from './input.js';
import { VirtualGamepad, isTouchDevice } from './virtual-gamepad.js';
import { MicCapture } from './mic.js';
import { toast, openModal, Hud, renderKeyHelp } from './ui.js';

const $ = (id) => document.getElementById(id);

const SETTINGS_KEY = 'chiaki-web.settings';
// Auto: prefer WebRTC (works via the libnice ICE backend + Cloudflare TURN,
// even for relay-only remote clients), fall back to WebSocket if it can't
// connect. The bridge must run with CHIAKI_ICE_BACKEND=nice for remote WebRTC.
const DEFAULT_SETTINGS = { resolution: '720p', fps: 60, codec: 'h264', bitrate: 0, transport: 'auto' };
// How long WebRTC gets to become ready before "auto" falls back to the
// WebSocket transport. TURN allocation + ICE on a hostile network is slow,
// but past this it's essentially never going to connect.
const WEBRTC_FALLBACK_TIMEOUT_MS = 15000;

/** @type {{wsPort: number, name?: string, version?: string}} */
let bridgeInfo = { wsPort: 9081 };
/** @type {ActiveSession|null} */
let session = null;

/* ────────────────────────────── settings ────────────────────────────── */

function loadSettings() {
  try {
    const raw = localStorage.getItem(SETTINGS_KEY);
    if (raw) return { ...DEFAULT_SETTINGS, ...JSON.parse(raw) };
  } catch (e) {
    /* corrupted settings — fall back */
  }
  return { ...DEFAULT_SETTINGS };
}

function saveSettings(s) {
  try {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(s));
  } catch (e) {
    /* private mode */
  }
}

function bindSettingsPanel() {
  const s = loadSettings();
  $('set-resolution').value = s.resolution;
  $('set-fps').value = String(s.fps);
  $('set-codec').value = s.codec;
  $('set-bitrate').value = String(s.bitrate || 0);
  $('set-transport').value = s.transport || 'auto';
  const update = () => {
    saveSettings({
      resolution: $('set-resolution').value,
      fps: parseInt($('set-fps').value, 10),
      codec: $('set-codec').value,
      bitrate: parseInt($('set-bitrate').value, 10) || 0,
      transport: $('set-transport').value,
    });
  };
  for (const id of ['set-resolution', 'set-fps', 'set-codec', 'set-bitrate', 'set-transport']) {
    $(id).addEventListener('change', update);
  }
}

/* ─────────────────────────── console list view ───────────────────────── */

async function refreshHosts() {
  const list = $('host-list');
  try {
    const res = await api('api/hosts');
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const hosts = await res.json();
    renderHosts(Array.isArray(hosts) ? hosts : []);
  } catch (e) {
    list.textContent = '';
    const err = document.createElement('p');
    err.className = 'empty-note';
    err.textContent = 'Could not reach the bridge (' + e.message + ')';
    list.appendChild(err);
  }
}

/** @param {Array<object>} hosts */
function renderHosts(hosts) {
  const list = $('host-list');
  list.textContent = '';
  if (hosts.length === 0) {
    const p = document.createElement('p');
    p.className = 'empty-note';
    p.textContent = 'No consoles found. Register one manually, or try Demo mode.';
    list.appendChild(p);
    return;
  }
  for (const h of hosts) {
    list.appendChild(hostCard(h));
  }
}

/** @param {object} h — /api/hosts entry */
function hostCard(h) {
  const card = document.createElement('article');
  card.className = 'card';

  const head = document.createElement('div');
  head.className = 'card-head';
  const dot = document.createElement('span');
  dot.className = 'state-dot state-' + (h.state || 'unknown');
  dot.title = h.state || 'unknown';
  const name = document.createElement('h3');
  name.textContent = h.nickname || h.host;
  head.append(dot, name);
  if (h.registered) {
    const badge = document.createElement('span');
    badge.className = 'badge';
    badge.textContent = 'registered';
    head.appendChild(badge);
  }

  const meta = document.createElement('p');
  meta.className = 'card-meta';
  const bits = [h.ps5 ? 'PS5' : 'PS4', h.host, h.state || 'unknown'];
  if (h.appName) bits.push('▶ ' + h.appName);
  meta.textContent = bits.join('  ·  ');

  const actions = document.createElement('div');
  actions.className = 'card-actions';

  const connectBtn = document.createElement('button');
  connectBtn.className = 'btn btn-primary';
  connectBtn.textContent = 'Connect';
  connectBtn.disabled = !h.registered;
  connectBtn.title = h.registered ? '' : 'Register this console first';
  connectBtn.addEventListener('click', () => startStream({ host: h.host }));
  actions.appendChild(connectBtn);

  const wakeBtn = document.createElement('button');
  wakeBtn.className = 'btn';
  wakeBtn.textContent = 'Wake';
  wakeBtn.disabled = !h.registered;
  wakeBtn.addEventListener('click', async () => {
    wakeBtn.disabled = true;
    try {
      const res = await api('api/wakeup', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ host: h.host }),
      });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      toast('Wake signal sent to ' + (h.nickname || h.host));
    } catch (e) {
      toast('Wake failed: ' + e.message, 'error');
    } finally {
      wakeBtn.disabled = false;
    }
  });
  actions.appendChild(wakeBtn);

  const regBtn = document.createElement('button');
  regBtn.className = 'btn';
  regBtn.textContent = h.registered ? 'Re-register' : 'Register';
  regBtn.addEventListener('click', () => openRegisterDialog(h.host, !!h.ps5));
  actions.appendChild(regBtn);

  card.append(head, meta, actions);
  return card;
}

/* ───────────────────────────── registration ──────────────────────────── */

/** @param {string} [host] @param {boolean} [ps5] */
function openRegisterDialog(host, ps5 = true) {
  $('reg-host').value = host || '';
  $('reg-ps5').checked = ps5;
  $('reg-pin').value = '';
  $('reg-error').textContent = '';
  const close = openModal($('modal-register'));
  $('reg-cancel').onclick = close;
  $('reg-submit').onclick = async () => {
    const body = {
      host: $('reg-host').value.trim(),
      ps5: $('reg-ps5').checked,
      pin: parseInt($('reg-pin').value, 10),
      psnAccountId: $('reg-psn').value.trim(),
    };
    if (!body.host || Number.isNaN(body.pin) || !body.psnAccountId) {
      $('reg-error').textContent = 'Host, PIN and PSN Account ID are all required.';
      return;
    }
    $('reg-submit').disabled = true;
    $('reg-error').textContent = 'Registering… (this can take a few seconds)';
    try {
      const res = await api('api/register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok) throw new Error(data.error || 'HTTP ' + res.status);
      toast('Registered ' + (data.nickname || body.host));
      close();
      refreshHosts();
    } catch (e) {
      $('reg-error').textContent = 'Registration failed: ' + e.message;
    } finally {
      $('reg-submit').disabled = false;
    }
  };
}

/* ────────────────────────────── stream view ──────────────────────────── */

class ActiveSession {
  /** @param {{host?: string, demo?: boolean}} target */
  constructor(target) {
    this.target = target;
    this.closed = false;
    this.hud = new Hud();
    /** @type {VideoPipeline|null} */
    this.pipeline = null;
    /** @type {WakeLockSentinel|null} */
    this.wakeLock = null;
    this.closeMenu = null;
    this.closePin = null;
    this.streamCodec = loadSettings().codec;

    // Fresh canvas per session: transferControlToOffscreen is one-shot.
    const wrap = $('canvas-wrap');
    wrap.textContent = '';
    this.canvas = document.createElement('canvas');
    this.canvas.className = 'stream-canvas';
    this.canvas.width = 1280;
    this.canvas.height = 720;
    wrap.appendChild(this.canvas);

    this.signaling = new Signaling(bridgeInfo.wsPort);
    /** @type {RtcSession|WsSession|null} — one common surface (see ws-transport.js) */
    this.rtc = null;
    this.transportMode = loadSettings().transport || 'auto'; // auto | webrtc | ws
    this.useWs = this.transportMode === 'ws';
    /** @type {RTCIceServer[]} minted by the bridge (Cloudflare TURN) */
    this.iceServers = [];
    this._fellBack = false;
    this._rtcDeadline = null;

    this.input = new InputManager({
      send: (msg) => this.rtc && this.rtc.sendInput(msg),
      onGamepadActive: (active) => this._updateOverlayVisibility(active),
      onToggleHelp: () => this._toggleHelp(),
      onMenu: () => this._toggleMenu(),
      onUserGesture: () => this._onGesture(),
    });
    this.vpad = new VirtualGamepad($('view-stream'), this.input);

    this._onVisibility = () => {
      if (document.visibilityState === 'visible') this._acquireWakeLock();
    };
    this._onFsGesture = () => this._onGesture();
  }

  async start() {
    showView('stream');
    this.hud.setNickname(this.target.demo ? 'Demo' : this.target.host || '');
    $('hud-streaminfo').textContent = 'connecting…';

    try {
      await this.signaling.connect();
    } catch (e) {
      this.teardown('Bridge signaling unreachable', true);
      return;
    }
    if (this.closed) return;

    this.signaling.onclose = (byUs) => {
      if (!byUs && !this.closed) this.teardown('Connection to bridge lost', true);
    };
    this.signaling.onmessage = (msg) => this._onSignal(msg);

    // Remote diagnostics: surface client-side errors in the bridge log.
    this.signaling.log('[client] ' + navigator.userAgent + ' | WebCodecs=' + !!window.VideoDecoder);
    this._onWindowError = (e) => {
      this.signaling.log('[client] error: ' + (e.message || (e.reason && (e.reason.stack || e.reason.message)) || 'unknown'));
    };
    window.addEventListener('error', this._onWindowError);
    window.addEventListener('unhandledrejection', this._onWindowError);

    if (this.useWs) {
      this._createTransport('ws');
    }
    // WebRTC transport is created lazily at the bridge's offer, so the
    // minted TURN servers (an "iceServers" signaling message) can be in the
    // RTCPeerConnection configuration from the start.

    const s = loadSettings();
    const startMsg = {
      resolution: s.resolution,
      fps: s.fps,
      codec: s.codec,
      transport: this.useWs ? 'ws' : 'webrtc',
    };
    if (s.bitrate > 0) startMsg.bitrate = s.bitrate;
    if (this.target.demo) startMsg.demo = true;
    else startMsg.host = this.target.host;
    this.signaling.start(startMsg);

    this.input.attach($('view-stream'));
    this._updateOverlayVisibility(this.input.gamepadSeen);
    this._acquireWakeLock();
    document.addEventListener('visibilitychange', this._onVisibility);
    // Any tap doubles as the iOS audio-unlock gesture.
    $('view-stream').addEventListener('pointerdown', this._onFsGesture);

    // Immersive mode: best-effort, silently tolerated failures (iOS).
    try {
      await $('view-stream').requestFullscreen({ navigationUI: 'hide' });
    } catch (e) {
      /* not available / denied */
    }
    try {
      if (screen.orientation && screen.orientation.lock) {
        await screen.orientation.lock('landscape');
      }
    } catch (e) {
      /* unsupported (iOS) or not in fullscreen */
    }

    // Stats: worker posts decode stats; we add DC bitrate.
    this._statsTimer = setInterval(async () => {
      if (this.rtc) this.hud.noteBytes(await this.rtc.getVideoBytesReceived());
    }, 1000);

    $('btn-stream-menu').onclick = () => this._toggleMenu();
  }

  /**
   * Create the media transport and wire the handlers shared by both kinds.
   * @param {'webrtc'|'ws'} kind
   */
  _createTransport(kind) {
    const t = kind === 'ws'
      ? new WsSession(this.signaling)
      : new RtcSession(this.signaling, this.iceServers);
    this.rtc = t;
    t.onVideoData = (buf) => {
      if (this.pipeline) this.pipeline.pushFragment(buf);
    };
    t.onInputMessage = (msg) => this._onInputMessage(msg);
    t.onConnectionState = (st) => {
      this.signaling.log('[' + kind + '] connection state: ' + st);
      if (st === 'failed' && kind === 'webrtc') this._webrtcFailed('WebRTC connection failed');
    };
    t.onReady = () => {
      this._clearRtcDeadline();
      $('hud-streaminfo').textContent = '';
      if (this.streamInfo) this.hud.setStreamInfo(this.streamInfo);
    };
    if (kind === 'webrtc' && this.transportMode === 'auto') {
      // Belt for the cases ICE never reaches "failed" (silent UDP blackhole).
      this._rtcDeadline = setTimeout(
        () => this._webrtcFailed('WebRTC timed out'),
        WEBRTC_FALLBACK_TIMEOUT_MS,
      );
    }
  }

  _clearRtcDeadline() {
    if (this._rtcDeadline) {
      clearTimeout(this._rtcDeadline);
      this._rtcDeadline = null;
    }
  }

  /**
   * Swap to the WebSocket transport mid-session (the bridge keeps the
   * console session alive). Used by the auto fallback and the stream menu.
   * @param {string} why — shown as a toast
   */
  _switchToWs(why) {
    if (this.closed || this._fellBack || (this.rtc && this.rtc.isWs)) return;
    this._clearRtcDeadline();
    this._fellBack = true;
    this.signaling.log('[transport] switching to WebSocket: ' + why);
    toast(why);
    if (this.mic && this.mic.running) this._toggleMic(); // mic rode the old DCs
    if (this.rtc) this.rtc.close();
    this._createTransport('ws');
    if (this.pipeline) this.pipeline.resetStream(); // frameIds restart at 0
    this.signaling.switchTransport('ws');
  }

  /**
   * WebRTC didn't make it. In auto mode, fall back; otherwise end.
   * @param {string} reason
   */
  _webrtcFailed(reason) {
    if (this.closed || this._fellBack || this.useWs) return;
    if (this.transportMode !== 'auto') {
      this._clearRtcDeadline();
      this.teardown(reason, true);
      return;
    }
    this.signaling.log('[transport] ' + reason);
    this._switchToWs('WebRTC failed — falling back to WebSocket transport');
  }

  /** @param {object} msg — signaling message from the bridge */
  _onSignal(msg) {
    // "t"-keyed messages are bridge→browser events riding the signaling
    // socket (WebSocket transport mode).
    if (msg.type === undefined && msg.t !== undefined) {
      this._onInputMessage(msg);
      return;
    }
    switch (msg.type) {
      case 'streamInfo':
        this.streamInfo = msg;
        this.streamCodec = msg.codec === 'hevc' ? 'hevc' : 'h264';
        this.hud.setStreamInfo(msg);
        this._createPipeline();
        break;
      case 'iceServers':
        // Arrives before the offer; used when the RtcSession is constructed.
        this.iceServers = Array.isArray(msg.iceServers) ? msg.iceServers : [];
        break;
      case 'offer':
        if (!this.pipeline) this._createPipeline();
        if (!this.rtc) this._createTransport('webrtc');
        if (this.rtc.isWs) break; // stale offer after fallback
        this.rtc.acceptOffer(msg.sdp).catch((e) => {
          this._webrtcFailed('SDP negotiation failed: ' + e.message);
        });
        break;
      case 'candidate':
        if (this.rtc && !this.rtc.isWs) this.rtc.addRemoteCandidate(msg.candidate, msg.mid);
        break;
      case 'audioInfo':
        if (this.rtc && this.rtc.isWs) this.rtc.handleSignal(msg);
        break;
      case 'status':
        if (msg.state === 'connecting') $('hud-streaminfo').textContent = 'connecting…';
        if (msg.state === 'connected' && this.rtc && this.rtc.isWs) this.rtc.handleSignal(msg);
        break;
      case 'quit':
        this.teardown(msg.reason || 'session ended', !!msg.error);
        break;
      default:
        break; // unknown types: tolerate
    }
  }

  _createPipeline() {
    if (this.pipeline) return;
    this.pipeline = new VideoPipeline(this.canvas, this.streamCodec, {
      requestIdr: () => this.rtc && this.rtc.sendInput({ t: 'idr' }),
      onStats: (s) => this.hud.update(s),
      onFirstFrame: () => {
        $('hud-streaminfo').textContent = '';
        this.signaling.log('[video] first frame rendered');
      },
      onFatal: (msg) => {
        this.signaling.log('[video] FATAL: ' + msg);
        this.teardown(msg, true);
      },
      onLog: (msg) => this.signaling.log(msg),
    });
  }

  /** @param {object} msg — bridge → browser message on the input DC */
  _onInputMessage(msg) {
    switch (msg.t) {
      case 'rumble':
        this.input.rumble(msg);
        break;
      case 'pinRequest':
        this._showPinDialog(!!msg.incorrect);
        break;
      case 'nickname':
        this.hud.setNickname(msg.name || '');
        break;
      case 'led':
      case 'stats':
        break; // informational
      default:
        break; // forward-compatible: ignore unknown types
    }
  }

  _showPinDialog(incorrect) {
    if (this.closePin) this.closePin();
    $('pin-msg').textContent = incorrect
      ? 'Incorrect PIN — try again.'
      : 'The console is asking for a login PIN.';
    $('pin-input').value = '';
    this.closePin = openModal($('modal-pin'), { sticky: true });
    $('pin-input').focus();
    $('pin-submit').onclick = () => {
      const pin = $('pin-input').value.trim();
      if (!/^\d{4,8}$/.test(pin)) {
        $('pin-msg').textContent = 'Enter the numeric PIN shown on the console.';
        return;
      }
      this.rtc.sendInput({ t: 'pin', pin });
      this.closePin();
      this.closePin = null;
    };
    $('pin-cancel').onclick = () => {
      if (this.closePin) this.closePin();
      this.closePin = null;
    };
  }

  _updateOverlayVisibility(gamepadActive) {
    if (!this.vpad) return;
    if (isTouchDevice() && !gamepadActive) this.vpad.show();
    else this.vpad.hide();
  }

  _onGesture() {
    if (this.rtc) this.rtc.resumeAudio();
  }

  async _acquireWakeLock() {
    if (this.closed || !('wakeLock' in navigator)) return;
    try {
      this.wakeLock = await navigator.wakeLock.request('screen');
    } catch (e) {
      /* denied (battery saver, background tab) */
    }
  }

  _toggleMenu() {
    if (this.closeMenu) {
      this.closeMenu();
      this.closeMenu = null;
      return;
    }
    this.closeMenu = openModal($('modal-menu'), { onClose: () => (this.closeMenu = null) });
    $('menu-disconnect').onclick = () => this.teardown('Disconnected', false);
    $('menu-mic').textContent = 'Microphone: ' + (this.mic && this.mic.running ? 'ON' : 'off');
    $('menu-mic').onclick = () => {
      this._toggleMic();
      this.closeMenu();
    };
    const onWs = this.rtc && this.rtc.isWs;
    $('menu-transport').textContent = 'Transport: ' + (onWs ? 'WebSocket' : 'WebRTC');
    // WebRTC→WS is a live swap; going back would need full renegotiation, so
    // returning to WebRTC means reconnecting.
    $('menu-transport').disabled = !!onWs;
    $('menu-transport').title = onWs
      ? 'Already on WebSocket — disconnect and reconnect to use WebRTC'
      : 'Switch this session to the WebSocket transport';
    $('menu-transport').onclick = () => {
      this.closeMenu();
      this._switchToWs('Switched to WebSocket transport');
    };
    $('menu-stats').onclick = () => {
      this.hud.toggle();
      this.closeMenu();
    };
    $('menu-fullscreen').onclick = async () => {
      try {
        if (document.fullscreenElement) await document.exitFullscreen();
        else await $('view-stream').requestFullscreen({ navigationUI: 'hide' });
      } catch (e) {
        /* unsupported */
      }
      this.closeMenu();
    };
    $('menu-help').onclick = () => {
      this.closeMenu();
      this._toggleHelp();
    };
  }

  async _toggleMic() {
    if (this.mic && this.mic.running) {
      this.mic.stop();
      if (this.rtc) this.rtc.sendInput({ t: 'micOff' });
      toast('Microphone off');
      return;
    }
    if (!this.mic) {
      this.mic = new MicCapture({
        send: (buf) => this.rtc && this.rtc.sendMic(buf),
        log: (msg) => this.signaling.log(msg),
      });
    }
    try {
      await this.mic.start();
      if (this.rtc) this.rtc.sendInput({ t: 'micOn' });
      toast('Microphone ON');
    } catch (e) {
      this.signaling.log('[mic] ' + e.message);
      toast(e.message, 'error');
    }
  }

  _toggleHelp() {
    const m = $('modal-help');
    if (m.classList.contains('modal-open')) {
      m.classList.remove('modal-open');
    } else {
      openModal(m);
    }
  }

  /**
   * Tear the whole session down and return to the console list.
   * @param {string} reason @param {boolean} isError
   */
  teardown(reason, isError) {
    if (this.closed) return;
    this.closed = true;

    clearInterval(this._statsTimer);
    this._clearRtcDeadline();
    if (this.closeMenu) this.closeMenu();
    if (this.closePin) this.closePin();
    $('modal-help').classList.remove('modal-open');

    this.input.detach();
    this.vpad.destroy();
    if (this.mic && this.mic.running) this.mic.stop();
    if (this.pipeline) this.pipeline.destroy();
    if (this.rtc) this.rtc.close();
    this.signaling.stop();

    document.removeEventListener('visibilitychange', this._onVisibility);
    $('view-stream').removeEventListener('pointerdown', this._onFsGesture);
    if (this._onWindowError) {
      window.removeEventListener('error', this._onWindowError);
      window.removeEventListener('unhandledrejection', this._onWindowError);
    }
    if (this.wakeLock) {
      this.wakeLock.release().catch(() => {});
      this.wakeLock = null;
    }
    if (document.fullscreenElement) {
      document.exitFullscreen().catch(() => {});
    }
    if (screen.orientation && screen.orientation.unlock) {
      try {
        screen.orientation.unlock();
      } catch (e) {
        /* fine */
      }
    }
    this.hud.hideStats();

    session = null;
    showView('hosts');
    refreshHosts();
    if (reason) toast(reason, isError ? 'error' : 'info');
  }
}

/** @param {{host?: string, demo?: boolean}} target */
function startStream(target) {
  if (session) return;
  if (typeof VideoDecoder === 'undefined') {
    toast('This browser has no WebCodecs support — cannot stream.', 'error');
    return;
  }
  session = new ActiveSession(target);
  session.start();
}

/* ─────────────────────────────── shell ───────────────────────────────── */

/** @param {'hosts'|'stream'} which */
function showView(which) {
  $('view-hosts').classList.toggle('hidden', which !== 'hosts');
  $('view-stream').classList.toggle('hidden', which !== 'stream');
  document.body.classList.toggle('streaming', which === 'stream');
}

async function init() {
  if (typeof VideoDecoder === 'undefined') {
    $('banner-unsupported').classList.remove('hidden');
  }

  bindSettingsPanel();
  renderKeyHelp(KEY_MAP, KEY_HELP_EXTRA);

  $('btn-refresh').addEventListener('click', refreshHosts);
  $('btn-register-new').addEventListener('click', () => openRegisterDialog());
  $('btn-demo').addEventListener('click', () => startStream({ demo: true }));

  // Never zoom: this is a gamepad UI, and iOS Safari ignores user-scalable=no.
  // Kill pinch-zoom (gesture* events) and double-tap-to-zoom (two taps within
  // ~350 ms) at the document level. Single taps are untouched, so button
  // presses and list clicks still work.
  document.addEventListener('gesturestart', (e) => e.preventDefault());
  document.addEventListener('gesturechange', (e) => e.preventDefault());
  let lastTouchEnd = 0;
  document.addEventListener('touchend', (e) => {
    const now = Date.now();
    if (now - lastTouchEnd <= 350) e.preventDefault();
    lastTouchEnd = now;
  }, { passive: false });
  $('view-stream').addEventListener('dblclick', (e) => e.preventDefault());

  try {
    const res = await api('api/info');
    if (res.ok) {
      const info = await res.json();
      if (info && typeof info.wsPort === 'number') bridgeInfo = info;
      if (info.version) {
        $('app-version').textContent = (info.name || 'chiaki-web') + ' ' + info.version;
      }
    }
  } catch (e) {
    toast('Bridge /api/info unreachable — using default WS port 9081', 'error');
  }

  refreshHosts();
}

init();
