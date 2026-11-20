/**
 * input.js — merges keyboard, Gamepad API and the virtual touch overlay into
 * one controller state, sent on the input DataChannel as
 * `{"t":"cs", b, l2, r2, lx, ly, rx, ry, tp}` (PROTOCOL.md "Input DC").
 *
 * Send policy: poll at 60 Hz, transmit on change, and at least every 200 ms
 * as a keepalive.
 */

/** Chiaki button bitmask (PROTOCOL.md). */
export const BTN = {
  CROSS: 1 << 0,
  MOON: 1 << 1,
  BOX: 1 << 2,
  PYRAMID: 1 << 3,
  DPAD_LEFT: 1 << 4,
  DPAD_RIGHT: 1 << 5,
  DPAD_UP: 1 << 6,
  DPAD_DOWN: 1 << 7,
  L1: 1 << 8,
  R1: 1 << 9,
  L3: 1 << 10,
  R3: 1 << 11,
  OPTIONS: 1 << 12,
  SHARE: 1 << 13,
  TOUCHPAD: 1 << 14,
  PS: 1 << 15,
};

/** W3C standard-mapping gamepad button index → chiaki flag (6/7 are analog). */
const GAMEPAD_MAP = {
  0: BTN.CROSS,
  1: BTN.MOON,
  2: BTN.BOX,
  3: BTN.PYRAMID,
  4: BTN.L1,
  5: BTN.R1,
  8: BTN.SHARE,
  9: BTN.OPTIONS,
  10: BTN.L3,
  11: BTN.R3,
  12: BTN.DPAD_UP,
  13: BTN.DPAD_DOWN,
  14: BTN.DPAD_LEFT,
  15: BTN.DPAD_RIGHT,
  16: BTN.PS,
};

/** DualShock touchpad plane (PROTOCOL.md). */
export const TOUCHPAD_W = 1920;
export const TOUCHPAD_H = 942;

const STICK_MAX = 32767;

/**
 * Keyboard map, also rendered by the help overlay. code → action.
 *
 * Layout follows FPS keyboard conventions (WASD = movement, Shift = aim,
 * R = fire, Space = jump) with mnemonics where they don't conflict
 * (C = Circle, T = Triangle, O = Options, P = PS). Ctrl is deliberately
 * unused: holding it as a button while pressing W/R/T would trigger
 * close-tab / reload / new-tab in the browser.
 */
export const KEY_MAP = [
  { keys: ['Space', 'Enter'], label: 'Cross — jump / confirm', btn: BTN.CROSS },
  { keys: ['KeyC', 'Backspace'], label: 'Circle — dodge / back', btn: BTN.MOON },
  { keys: ['KeyF'], label: 'Square — interact / reload', btn: BTN.BOX },
  { keys: ['KeyT'], label: 'Triangle — swap / special', btn: BTN.PYRAMID },
  { keys: ['ArrowUp'], label: 'D-pad up', btn: BTN.DPAD_UP },
  { keys: ['ArrowDown'], label: 'D-pad down', btn: BTN.DPAD_DOWN },
  { keys: ['ArrowLeft'], label: 'D-pad left', btn: BTN.DPAD_LEFT },
  { keys: ['ArrowRight'], label: 'D-pad right', btn: BTN.DPAD_RIGHT },
  { keys: ['KeyQ'], label: 'L1', btn: BTN.L1 },
  { keys: ['KeyE'], label: 'R1', btn: BTN.R1 },
  { keys: ['ShiftLeft', 'ShiftRight'], label: 'L2 — aim', trigger: 'l2' },
  { keys: ['KeyR'], label: 'R2 — fire', trigger: 'r2' },
  { keys: ['KeyX'], label: 'L3 — left stick click', btn: BTN.L3 },
  { keys: ['KeyM'], label: 'R3 — right stick click', btn: BTN.R3 },
  { keys: ['KeyO'], label: 'Options', btn: BTN.OPTIONS },
  { keys: ['Tab'], label: 'Share / Create', btn: BTN.SHARE },
  { keys: ['KeyG'], label: 'Touchpad click', btn: BTN.TOUCHPAD },
  { keys: ['KeyP'], label: 'PS home', btn: BTN.PS },
];

/** Non-button helper rows for the help overlay. */
export const KEY_HELP_EXTRA = [
  { keys: 'W A S D', label: 'Left stick (movement) — or d-pad after F2' },
  { keys: 'I J K L', label: 'Right stick (camera)' },
  { keys: 'F2', label: 'Toggle WASD: left stick ↔ d-pad' },
  { keys: 'F1 or ?', label: 'This help' },
  { keys: 'Esc', label: 'Stream menu' },
];

/** @param {number} v -1..1 → i16 (linear passthrough — no deadzone, no
 *  anti-deadzone; the console applies its own deadzone). */
function axisToI16(v) {
  const s = Math.round(v * STICK_MAX);
  return Math.max(-STICK_MAX, Math.min(STICK_MAX, s));
}

/** Larger-magnitude wins when two sources drive the same stick axis. */
function domAxis(a, b) {
  return Math.abs(b) > Math.abs(a) ? b : a;
}

export class InputManager {
  /**
   * @param {{
   *   send: (msg: object) => void,
   *   onGamepadActive?: (active: boolean) => void,
   *   onToggleHelp?: () => void,
   *   onMenu?: () => void,
   *   onUserGesture?: () => void,
   * }} opts
   */
  constructor(opts) {
    this.send = opts.send;
    this.onGamepadActive = opts.onGamepadActive || (() => {});
    this.onToggleHelp = opts.onToggleHelp || (() => {});
    this.onMenu = opts.onMenu || (() => {});
    this.onUserGesture = opts.onUserGesture || (() => {});

    /** @type {Set<string>} pressed KeyboardEvent.code values */
    this.keys = new Set();
    this.wasdMode = /** @type {'dpad'|'stick'} */ ('stick');

    // Virtual gamepad overlay contribution (set by VirtualGamepad).
    this.overlayState = { b: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 };

    // Mouse → touchpad plane.
    this.tpPos = { x: TOUCHPAD_W / 2, y: TOUCHPAD_H / 2 };
    this.tpTouchActive = false;
    this.tpClickUntil = 0;
    this._tpDown = null; // {t, moved}

    this.gamepadSeen = false;
    this._lastSentJson = '';
    this._lastSentAt = 0;
    this._timer = 0;
    this._el = null;

    this._onKeyDown = (e) => this._keyDown(e);
    this._onKeyUp = (e) => this._keyUp(e);
    this._onPadConn = () => this._refreshGamepadActive();
    this._onPadDisc = () => this._refreshGamepadActive();
  }

  /** @param {HTMLElement} el — stream surface receiving pointer events */
  attach(el) {
    this._el = el;
    window.addEventListener('keydown', this._onKeyDown);
    window.addEventListener('keyup', this._onKeyUp);
    window.addEventListener('gamepadconnected', this._onPadConn);
    window.addEventListener('gamepaddisconnected', this._onPadDisc);

    this._onPointerDown = (e) => this._pointerDown(e);
    this._onPointerMove = (e) => this._pointerMove(e);
    this._onPointerUp = (e) => this._pointerUp(e);
    el.addEventListener('pointerdown', this._onPointerDown);
    el.addEventListener('pointermove', this._onPointerMove);
    el.addEventListener('pointerup', this._onPointerUp);
    el.addEventListener('pointercancel', this._onPointerUp);

    this._refreshGamepadActive();
    this._timer = setInterval(() => this._tick(), 16);
  }

  detach() {
    clearInterval(this._timer);
    window.removeEventListener('keydown', this._onKeyDown);
    window.removeEventListener('keyup', this._onKeyUp);
    window.removeEventListener('gamepadconnected', this._onPadConn);
    window.removeEventListener('gamepaddisconnected', this._onPadDisc);
    if (this._el) {
      this._el.removeEventListener('pointerdown', this._onPointerDown);
      this._el.removeEventListener('pointermove', this._onPointerMove);
      this._el.removeEventListener('pointerup', this._onPointerUp);
      this._el.removeEventListener('pointercancel', this._onPointerUp);
      this._el = null;
    }
    this.keys.clear();
  }

  /* ── keyboard ── */

  _keyDown(e) {
    this.onUserGesture();
    if (e.code === 'Escape') {
      e.preventDefault();
      this.onMenu();
      return;
    }
    if (e.code === 'F1' || (e.key === '?' && !e.repeat)) {
      e.preventDefault();
      this.onToggleHelp();
      return;
    }
    if (e.code === 'F2') {
      e.preventDefault();
      this.wasdMode = this.wasdMode === 'dpad' ? 'stick' : 'dpad';
      return;
    }
    if (this._isMapped(e.code)) {
      e.preventDefault();
      this.keys.add(e.code);
    }
  }

  _keyUp(e) {
    if (this._isMapped(e.code)) {
      e.preventDefault();
      this.keys.delete(e.code);
    }
  }

  _isMapped(code) {
    if (/^(Arrow(Up|Down|Left|Right)|Key[WASDIJKL]|Space|Enter|Backspace|Tab)$/.test(code)) {
      return true;
    }
    return KEY_MAP.some((m) => m.keys.includes(code));
  }

  _keyboardState() {
    const st = { b: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 };
    for (const m of KEY_MAP) {
      if (!m.keys.some((k) => this.keys.has(k))) continue;
      if (m.btn) st.b |= m.btn;
      else if (m.trigger) st[m.trigger] = 255;
    }
    // WASD: left stick by default, d-pad after F2.
    const w = this.keys.has('KeyW');
    const a = this.keys.has('KeyA');
    const s = this.keys.has('KeyS');
    const d = this.keys.has('KeyD');
    if (this.wasdMode === 'stick') {
      if (a !== d) st.lx = d ? STICK_MAX : -STICK_MAX;
      if (w !== s) st.ly = s ? STICK_MAX : -STICK_MAX; // positive = down
    } else {
      if (w) st.b |= BTN.DPAD_UP;
      if (s) st.b |= BTN.DPAD_DOWN;
      if (a) st.b |= BTN.DPAD_LEFT;
      if (d) st.b |= BTN.DPAD_RIGHT;
    }
    // IJKL: right stick.
    const i = this.keys.has('KeyI');
    const j = this.keys.has('KeyJ');
    const k = this.keys.has('KeyK');
    const l = this.keys.has('KeyL');
    if (j !== l) st.rx = l ? STICK_MAX : -STICK_MAX;
    if (i !== k) st.ry = k ? STICK_MAX : -STICK_MAX;
    return st;
  }

  /* ── mouse → touchpad plane ── */
  //
  // Desktop mouse clicks/drags deliberately do NOT emulate the DualShock
  // touchpad: almost no games use it, and the emulation made every click on the
  // video an accidental touchpad press/drag. Pointer handlers now only unlock
  // the audio gesture; the touchpad is still reachable via the 'G' key and the
  // on-screen PAD button in the touch overlay.

  _pointerDown(e) {
    this.onUserGesture();
  }

  _pointerMove(e) {}

  _pointerUp(e) {}

  /* ── gamepad ── */

  _refreshGamepadActive() {
    const active = this._anyStandardPad() !== null;
    if (active !== this.gamepadSeen) {
      this.gamepadSeen = active;
      this.onGamepadActive(active);
    }
  }

  _anyStandardPad() {
    if (!navigator.getGamepads) return null;
    for (const gp of navigator.getGamepads()) {
      if (gp && gp.connected && gp.mapping === 'standard') return gp;
    }
    return null;
  }

  _gamepadState() {
    const st = { b: 0, l2: 0, r2: 0, lx: 0, ly: 0, rx: 0, ry: 0 };
    const gp = this._anyStandardPad();
    if (!gp) return st;
    for (const idx in GAMEPAD_MAP) {
      const btn = gp.buttons[idx];
      if (btn && btn.pressed) st.b |= GAMEPAD_MAP[idx];
    }
    st.l2 = gp.buttons[6] ? Math.round(gp.buttons[6].value * 255) : 0;
    st.r2 = gp.buttons[7] ? Math.round(gp.buttons[7].value * 255) : 0;
    st.lx = axisToI16(gp.axes[0] || 0);
    st.ly = axisToI16(gp.axes[1] || 0); // positive = down (chiaki convention)
    st.rx = axisToI16(gp.axes[2] || 0);
    st.ry = axisToI16(gp.axes[3] || 0);
    return st;
  }

  /* ── merge + send ── */

  _tick() {
    // Poll-driven refresh also catches pads that never fire connect events.
    this._refreshGamepadActive();

    const kb = this._keyboardState();
    const gp = this._gamepadState();
    const ov = this.overlayState;

    const state = {
      t: 'cs',
      b: (kb.b | gp.b | ov.b) >>> 0,
      l2: Math.max(kb.l2, gp.l2, ov.l2 | 0),
      r2: Math.max(kb.r2, gp.r2, ov.r2 | 0),
      lx: domAxis(domAxis(kb.lx, gp.lx), ov.lx | 0),
      ly: domAxis(domAxis(kb.ly, gp.ly), ov.ly | 0),
      rx: domAxis(domAxis(kb.rx, gp.rx), ov.rx | 0),
      ry: domAxis(domAxis(kb.ry, gp.ry), ov.ry | 0),
    };

    if (performance.now() < this.tpClickUntil) state.b |= BTN.TOUCHPAD;
    if (this.tpTouchActive) {
      state.tp = [[0, Math.round(this.tpPos.x), Math.round(this.tpPos.y)]];
    }

    const json = JSON.stringify(state);
    const now = performance.now();
    if (json !== this._lastSentJson || now - this._lastSentAt >= 200) {
      this._lastSentJson = json;
      this._lastSentAt = now;
      this.send(state);
    }
  }

  /**
   * Bridge rumble → Gamepad vibration.
   * @param {{l?: number, r?: number}} msg — 0..255 per motor
   */
  rumble(msg) {
    const gp = this._anyStandardPad();
    if (!gp || !gp.vibrationActuator) return;
    try {
      gp.vibrationActuator.playEffect('dual-rumble', {
        duration: 200,
        strongMagnitude: Math.min(1, (msg.l || 0) / 255),
        weakMagnitude: Math.min(1, (msg.r || 0) / 255),
      });
    } catch (e) {
      /* actuator type unsupported */
    }
  }
}
