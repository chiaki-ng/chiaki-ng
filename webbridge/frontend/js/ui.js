/**
 * ui.js — small UI helpers: toasts, modal handling, HUD text, help overlay.
 */

/**
 * Show a transient toast at the bottom of the screen.
 * @param {string} text
 * @param {'info'|'error'} [kind]
 */
export function toast(text, kind = 'info') {
  const host = document.getElementById('toasts');
  if (!host) return;
  const node = document.createElement('div');
  node.className = 'toast' + (kind === 'error' ? ' toast-error' : '');
  node.textContent = text;
  host.appendChild(node);
  setTimeout(() => node.classList.add('toast-show'), 10);
  setTimeout(() => {
    node.classList.remove('toast-show');
    setTimeout(() => node.remove(), 300);
  }, 4200);
}

/**
 * Open a modal by element. Returns a close function. Clicking the backdrop
 * closes unless opts.sticky.
 * @param {HTMLElement} modal @param {{sticky?: boolean, onClose?: () => void}} [opts]
 */
export function openModal(modal, opts = {}) {
  modal.classList.add('modal-open');
  const close = () => {
    modal.classList.remove('modal-open');
    modal.removeEventListener('click', onBackdrop);
    if (opts.onClose) opts.onClose();
  };
  const onBackdrop = (e) => {
    if (e.target === modal && !opts.sticky) close();
  };
  modal.addEventListener('click', onBackdrop);
  return close;
}

/**
 * Stream HUD: shows nickname / stream parameters / live stats.
 */
export class Hud {
  constructor() {
    this.el = document.getElementById('hud-stats');
    this.nickEl = document.getElementById('hud-nickname');
    this.infoEl = document.getElementById('hud-streaminfo');
    this.visible = false;
    this._bitrateBytes = 0;
    this._bitrateAt = 0;
    this._bitrateKbps = 0;
  }

  setNickname(name) {
    if (this.nickEl) this.nickEl.textContent = name || '';
  }

  /** @param {{codec: string, width: number, height: number, fps: number}} info */
  setStreamInfo(info) {
    if (this.infoEl) {
      this.infoEl.textContent = `${info.codec} ${info.width}×${info.height}@${info.fps}`;
    }
  }

  /** Feed a cumulative byte counter; the HUD derives kbps. */
  noteBytes(bytes) {
    const now = performance.now();
    if (this._bitrateAt > 0 && now > this._bitrateAt) {
      const dt = (now - this._bitrateAt) / 1000;
      if (dt > 0.2) {
        this._bitrateKbps = ((bytes - this._bitrateBytes) * 8) / 1000 / dt;
        this._bitrateBytes = bytes;
        this._bitrateAt = now;
      }
    } else {
      this._bitrateBytes = bytes;
      this._bitrateAt = now;
    }
  }

  /** @param {object} s — worker stats message */
  update(s) {
    if (!this.el || !this.visible) return;
    const lines = [
      `fps      ${s.fps.toFixed(1)}`,
      `bitrate  ${(this._bitrateKbps / 1000).toFixed(1)} Mbps`,
      `decodeQ  ${s.decodeQueue}  renderQ ${s.renderQueue}`,
      `holdback ${s.holdbackMs} ms`,
      `frames   ${s.rendered} drawn / ${s.dropped} dropped`,
    ];
    this.el.textContent = lines.join('\n');
  }

  toggle() {
    this.visible = !this.visible;
    if (this.el) this.el.classList.toggle('hidden', !this.visible);
    return this.visible;
  }

  hideStats() {
    this.visible = false;
    if (this.el) this.el.classList.add('hidden');
  }
}

/** Fill the keyboard-help table from the input map. */
export function renderKeyHelp(keyMap, extras) {
  const tbody = document.getElementById('help-table');
  if (!tbody) return;
  tbody.textContent = '';
  const row = (keysText, label) => {
    const tr = document.createElement('tr');
    const td1 = document.createElement('td');
    const td2 = document.createElement('td');
    td1.innerHTML = keysText
      .split(' / ')
      .map((k) => `<kbd>${k}</kbd>`)
      .join(' ');
    td2.textContent = label;
    tr.append(td1, td2);
    tbody.appendChild(tr);
  };
  for (const m of keyMap) {
    const keys = m.keys
      .map((k) => k
        .replace(/^Key/, '')
        .replace(/^Digit/, '')
        .replace(/^Arrow/, '')
        .replace(/^(Shift|Control|Alt)(Left|Right)$/, '$2 $1'))
      .join(' / ');
    row(keys, m.label);
  }
  for (const e of extras) row(e.keys, e.label);
}
