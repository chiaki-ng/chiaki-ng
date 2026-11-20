/**
 * virtual-gamepad.js — touch overlay for the stream view.
 *
 * Left virtual thumbstick (movement), right virtual thumbstick (camera look),
 * d-pad, right face-button cluster, shoulder zones and small system buttons.
 * Multi-touch correct via Pointer Events (each finger has its own pointerId;
 * every control captures its pointer). Writes its state into
 * InputManager.overlayState — the merger in input.js does the rest.
 */

import { BTN } from './input.js';

const STICK_MAX = 32767;

/** @param {string} tag @param {string} cls @param {string} [text] */
function el(tag, cls, text) {
  const n = document.createElement(tag);
  n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}

export class VirtualGamepad {
  /**
   * @param {HTMLElement} parent — stream view container
   * @param {import('./input.js').InputManager} input
   */
  constructor(parent, input) {
    this.input = input;
    this.pressed = new Set(); // chiaki flags held by touches
    this.l2 = 0;
    this.r2 = 0;
    this.stick = { x: 0, y: 0 };   // left stick — movement (lx/ly)
    this.rstick = { x: 0, y: 0 };  // right stick — camera look (rx/ry)
    this._stickPointer = -1;
    this._rstickPointer = -1;

    this.root = el('div', 'vpad');
    this.root.setAttribute('aria-hidden', 'true');
    parent.appendChild(this.root);

    this._buildStick();
    this._buildRightStick();
    this._buildDpad();
    this._buildFaceCluster();
    this._buildShoulders();
    this._buildSystemRow();

    this.hide();
  }

  show() {
    this.root.classList.add('vpad-visible');
  }

  hide() {
    this.root.classList.remove('vpad-visible');
    this._resetAll();
  }

  get visible() {
    return this.root.classList.contains('vpad-visible');
  }

  destroy() {
    this.root.remove();
  }

  _resetAll() {
    this.pressed.clear();
    this.l2 = 0;
    this.r2 = 0;
    this.stick.x = 0;
    this.stick.y = 0;
    this.rstick.x = 0;
    this.rstick.y = 0;
    this._stickPointer = -1;
    this._rstickPointer = -1;
    if (this.nub) this.nub.style.transform = 'translate(0px, 0px)';
    if (this.rnub) this.rnub.style.transform = 'translate(0px, 0px)';
    this._publish();
  }

  _publish() {
    let b = 0;
    for (const f of this.pressed) b |= f;
    this.input.overlayState = {
      b,
      l2: this.l2,
      r2: this.r2,
      lx: Math.round(this.stick.x * STICK_MAX),
      ly: Math.round(this.stick.y * STICK_MAX), // positive = down
      rx: Math.round(this.rstick.x * STICK_MAX),
      ry: Math.round(this.rstick.y * STICK_MAX), // positive = down
    };
  }

  /**
   * Wire a hold-to-press control.
   * @param {HTMLElement} node
   * @param {() => void} down @param {() => void} up
   */
  _pressable(node, down, up) {
    node.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      e.stopPropagation();
      try { node.setPointerCapture(e.pointerId); } catch { /* synthetic or stale pointer */ }
      node.classList.add('vpad-active');
      this.input.onUserGesture();
      down();
      this._publish();
    });
    const end = (e) => {
      e.preventDefault();
      e.stopPropagation();
      node.classList.remove('vpad-active');
      up();
      this._publish();
    };
    node.addEventListener('pointerup', end);
    node.addEventListener('pointercancel', end);
  }

  /** @param {HTMLElement} node @param {number} flag */
  _button(node, flag) {
    this._pressable(
      node,
      () => this.pressed.add(flag),
      () => this.pressed.delete(flag),
    );
  }

  /* ── thumbsticks ── */

  _buildStick() {
    this.nub = this._makeStick('vpad-lstick-zone', this.stick, 'left');
  }

  _buildRightStick() {
    this.rnub = this._makeStick('vpad-rstick-zone', this.rstick, 'right');
  }

  /**
   * Build a floating analog thumbstick.
   * @param {string} zoneCls — CSS class positioning the touch zone
   * @param {{x:number,y:number}} state — normalized axis output written here
   * @param {'left'|'right'} which — selects the pointer-tracking field
   * @returns {HTMLElement} the nub element (for reset transforms)
   */
  _makeStick(zoneCls, state, which) {
    const ptrField = which === 'left' ? '_stickPointer' : '_rstickPointer';
    const zone = el('div', `vpad-stick-zone ${zoneCls}`);
    const base = el('div', 'vpad-stick-base');
    const nub = el('div', 'vpad-stick-nub');
    base.appendChild(nub);
    zone.appendChild(base);
    this.root.appendChild(zone);

    const RADIUS = 60; // px of nub travel
    const EXPO = 1.25; // >1 gently softens small movements, keeps full range at edge

    const track = (e) => {
      const rect = base.getBoundingClientRect();
      const cx = rect.left + rect.width / 2;
      const cy = rect.top + rect.height / 2;
      let dx = e.clientX - cx;
      let dy = e.clientY - cy;
      const mag = Math.hypot(dx, dy);
      if (mag > RADIUS) {
        dx = (dx / mag) * RADIUS;
        dy = (dy / mag) * RADIUS;
      }
      nub.style.transform = `translate(${dx}px, ${dy}px)`;
      // Response shaping: a mild expo curve so the stick is a touch less twitchy.
      // The nub tracks the finger linearly; only the emitted axes are shaped.
      const m = Math.min(1, mag / RADIUS); // clamped deflection 0..1
      const out = Math.pow(m, EXPO);
      const len = Math.hypot(dx, dy) || 1; // unit direction from clamped offset
      state.x = (dx / len) * out;
      state.y = (dy / len) * out;
      this._publish();
    };

    zone.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      e.stopPropagation();
      if (this[ptrField] !== -1) return;
      this[ptrField] = e.pointerId;
      try { zone.setPointerCapture(e.pointerId); } catch { /* synthetic or stale pointer */ }
      this.input.onUserGesture();
      track(e);
    });
    zone.addEventListener('pointermove', (e) => {
      if (e.pointerId !== this[ptrField]) return;
      e.preventDefault();
      track(e);
    });
    const release = (e) => {
      if (e.pointerId !== this[ptrField]) return;
      e.preventDefault();
      this[ptrField] = -1;
      state.x = 0;
      state.y = 0;
      nub.style.transform = 'translate(0px, 0px)';
      this._publish();
    };
    zone.addEventListener('pointerup', release);
    zone.addEventListener('pointercancel', release);

    return nub;
  }

  /* ── d-pad ── */

  _buildDpad() {
    const pad = el('div', 'vpad-dpad');
    const dirs = [
      ['up', BTN.DPAD_UP, '▲'],
      ['left', BTN.DPAD_LEFT, '◀'],
      ['right', BTN.DPAD_RIGHT, '▶'],
      ['down', BTN.DPAD_DOWN, '▼'],
    ];
    for (const [name, flag, glyph] of dirs) {
      const b = el('button', `vpad-btn vpad-dpad-${name}`, glyph);
      b.type = 'button';
      this._button(b, flag);
      pad.appendChild(b);
    }
    this.root.appendChild(pad);
  }

  /* ── face buttons ── */

  _buildFaceCluster() {
    const cluster = el('div', 'vpad-face');
    const defs = [
      ['pyramid', BTN.PYRAMID, '△'],
      ['box', BTN.BOX, '□'],
      ['moon', BTN.MOON, '○'],
      ['cross', BTN.CROSS, '✕'],
    ];
    for (const [name, flag, glyph] of defs) {
      const b = el('button', `vpad-btn vpad-face-${name}`, glyph);
      b.type = 'button';
      this._button(b, flag);
      cluster.appendChild(b);
    }
    this.root.appendChild(cluster);
  }

  /* ── shoulders / triggers ── */

  _buildShoulders() {
    const defs = [
      ['l2', 'L2', 'left', (v) => (this.l2 = v ? 255 : 0)],
      ['l1', 'L1', 'left', null, BTN.L1],
      ['r1', 'R1', 'right', null, BTN.R1],
      ['r2', 'R2', 'right', (v) => (this.r2 = v ? 255 : 0)],
    ];
    const left = el('div', 'vpad-shoulders vpad-shoulders-left');
    const right = el('div', 'vpad-shoulders vpad-shoulders-right');
    for (const [name, label, side, setTrigger, flag] of defs) {
      const b = el('button', `vpad-btn vpad-shoulder vpad-${name}`, label);
      b.type = 'button';
      if (setTrigger) {
        this._pressable(b, () => setTrigger(true), () => setTrigger(false));
      } else {
        this._button(b, flag);
      }
      (side === 'left' ? left : right).appendChild(b);
    }
    this.root.appendChild(left);
    this.root.appendChild(right);
  }

  /* ── share / touchpad / ps / options ── */

  _buildSystemRow() {
    const row = el('div', 'vpad-system');
    const defs = [
      ['SHARE', BTN.SHARE],
      ['PAD', BTN.TOUCHPAD],
      ['PS', BTN.PS],
      ['OPT', BTN.OPTIONS],
    ];
    for (const [label, flag] of defs) {
      const b = el('button', 'vpad-btn vpad-sys', label);
      b.type = 'button';
      this._button(b, flag);
      row.appendChild(b);
    }
    this.root.appendChild(row);
  }
}

/** True when this device should get the touch overlay by default. */
export function isTouchDevice() {
  const coarse = window.matchMedia && window.matchMedia('(pointer: coarse)').matches;
  return coarse || (navigator.maxTouchPoints || 0) > 0;
}
