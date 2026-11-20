/**
 * signaling.js — WebSocket signaling client for the chiaki-web bridge.
 *
 * Wire format: JSON text messages, per webbridge/PROTOCOL.md.
 * The client sends `start` / `answer` / `candidate` / `stop`; the bridge sends
 * `streamInfo` / `offer` / `candidate` / `status` / `quit`.
 */

/**
 * Build the signaling WebSocket URL.
 *
 * PROTOCOL.md: when the page is served through a path-preserving reverse
 * proxy (path contains `/lp/<port>/`), substitute the port segment of our own
 * URL instead of using `location.port`. Otherwise connect straight to
 * `ws(s)://<hostname>:<wsPort>/`.
 *
 * @param {number} wsPort — signaling port from GET /api/info
 * @returns {string}
 */
export function buildWsUrl(wsPort) {
  const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const m = location.pathname.match(/^(.*\/lp\/)\d+(\/|$)/);
  if (m) {
    return `${scheme}//${location.host}${m[1]}${wsPort}/`;
  }
  // Dedicated hostname on standard ports (tunnel/reverse proxy): the fronting
  // proxy routes the same-origin /ws path to the signaling port.
  if (!location.port || location.port === '443' || location.port === '80') {
    return `${scheme}//${location.host}/ws`;
  }
  return `${scheme}//${location.hostname}:${wsPort}/`;
}

/**
 * REST base path. Relative fetches already resolve correctly under the
 * `/lp/<port>/` proxy prefix as long as the page URL ends with a slash (or a
 * file name); this normalizes the directory part so `api(...)` always works.
 * @returns {string} base path ending with '/'
 */
export function apiBase() {
  const p = location.pathname;
  return p.endsWith('/') ? p : p.slice(0, p.lastIndexOf('/') + 1);
}

/** @param {string} path e.g. 'api/hosts' */
export function apiUrl(path) {
  return apiBase() + path;
}

/* ── optional shared-secret gate (CHIAKI_WEB_TOKEN on the bridge) ────────── */

const TOKEN_KEY = 'cw-token';

export function getToken() {
  try {
    return localStorage.getItem(TOKEN_KEY) || '';
  } catch {
    return '';
  }
}

export function setToken(t) {
  try {
    localStorage.setItem(TOKEN_KEY, t);
  } catch { /* private browsing */ }
}

/**
 * fetch() wrapper that sends the access token and, on 401, prompts for one
 * once and retries.
 * @param {string} path @param {RequestInit} [opts]
 */
export async function api(path, opts = {}) {
  const headers = { ...(opts.headers || {}) };
  const tok = getToken();
  if (tok) headers['X-Auth-Token'] = tok;
  let res = await fetch(apiUrl(path), { ...opts, headers });
  if (res.status === 401) {
    const entered = window.prompt('This bridge requires an access token:');
    if (entered && entered.trim()) {
      setToken(entered.trim());
      headers['X-Auth-Token'] = entered.trim();
      res = await fetch(apiUrl(path), { ...opts, headers });
    }
  }
  return res;
}

export class Signaling {
  /** @param {number} wsPort */
  constructor(wsPort) {
    this.url = buildWsUrl(wsPort);
    /** @type {WebSocket|null} */
    this.ws = null;
    /** @type {(msg: object) => void} */
    this.onmessage = () => {};
    /** @type {(buf: ArrayBuffer) => void} ws-transport media (channel-prefixed) */
    this.onbinary = () => {};
    /** @type {(clean: boolean) => void} */
    this.onclose = () => {};
    this._closedByUs = false;
  }

  /** Open the socket. Resolves once connected. @returns {Promise<void>} */
  connect() {
    return new Promise((resolve, reject) => {
      let settled = false;
      const ws = new WebSocket(this.url);
      ws.binaryType = 'arraybuffer';
      this.ws = ws;
      ws.onopen = () => {
        settled = true;
        resolve();
      };
      ws.onerror = () => {
        if (!settled) {
          settled = true;
          reject(new Error('signaling connection failed (' + this.url + ')'));
        }
      };
      ws.onclose = () => {
        if (!settled) {
          settled = true;
          reject(new Error('signaling connection closed'));
          return;
        }
        this.onclose(this._closedByUs);
      };
      ws.onmessage = (ev) => {
        if (ev.data instanceof ArrayBuffer) {
          this.onbinary(ev.data);
          return;
        }
        if (typeof ev.data !== 'string') return;
        let msg;
        try {
          msg = JSON.parse(ev.data);
        } catch (e) {
          console.warn('[signaling] non-JSON message ignored');
          return;
        }
        if (msg && typeof msg === 'object') this.onmessage(msg);
      };
    });
  }

  /** @param {object} msg */
  send(msg) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(msg));
    }
  }

  /** ws-transport upstream media (channel-prefixed). @param {ArrayBuffer|Uint8Array} buf */
  sendBinary(buf) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      try {
        this.ws.send(buf);
        return true;
      } catch { /* closing */ }
    }
    return false;
  }

  /** Ask the bridge to swap the media path mid-session. @param {'ws'} transport */
  switchTransport(transport) {
    this.send({ type: 'switchTransport', transport });
  }

  /**
   * Ask the bridge to start a session.
   * @param {{host?: string, demo?: boolean, resolution?: string,
   *          fps?: number, codec?: string, bitrate?: number}} opts
   */
  start(opts) {
    const token = getToken();
    this.send(token ? { type: 'start', token, ...opts } : { type: 'start', ...opts });
  }

  /** Send our SDP answer. @param {string} sdp */
  answer(sdp) {
    this.send({ type: 'answer', sdp });
  }

  /**
   * Trickle a local ICE candidate to the bridge.
   * @param {string} candidate @param {string} mid
   */
  candidate(candidate, mid) {
    this.send({ type: 'candidate', candidate, mid });
  }

  /**
   * Remote diagnostics: forward a client-side event to the bridge log.
   * Rate-limited and truncated; silently dropped when the WS is closed.
   * @param {string} msg
   */
  log(msg) {
    const text = String(msg);
    // Mirror to the browser devtools console so it's visible client-side too,
    // not only in the bridge log. Not rate-limited locally.
    try {
      // eslint-disable-next-line no-console
      console.log('[chiaki-web] ' + text);
    } catch { /* console unavailable */ }

    const tick = (Date.now() / 1000) | 0;
    if (tick !== this._logTick) {
      this._logTick = tick;
      this._logCount = 0;
    }
    if (++this._logCount > 20) return; // cap on what we ship upstream: 20 lines/s
    try {
      this.send({ type: 'clientlog', msg: text.slice(0, 600) });
    } catch { /* never let diagnostics break the session */ }
  }

  /** Politely end the session, then close the socket. */
  stop() {
    this._closedByUs = true;
    this.send({ type: 'stop' });
    this.close();
  }

  close() {
    this._closedByUs = true;
    if (this.ws) {
      try {
        this.ws.close();
      } catch (e) {
        /* already closed */
      }
      this.ws = null;
    }
  }
}
