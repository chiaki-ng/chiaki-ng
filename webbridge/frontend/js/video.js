/**
 * video.js — main-thread side of the video pipeline.
 *
 * Reassembles the 17-byte-header fragments from the "video" DataChannel into
 * complete Annex-B access units (PROTOCOL.md "Video fragment format"), detects
 * frameId gaps / stale incomplete frames, and hands finished AUs to the decode
 * worker (VideoDecoder + OffscreenCanvas) with a transferred buffer.
 */

const HEADER_SIZE = 17;
const FLAG_KEYFRAME = 0x01;
// An incomplete frame older than this (vs the newest frameId seen) is dead:
// the channel gave up retransmitting one of its chunks.
const STALE_FRAME_WINDOW = 4;
const MAX_PENDING_FRAMES = 16;

/** Serial-number distance a-b for u32 frameIds (handles wrap). */
function idDelta(a, b) {
  return (a - b) | 0; // wraps into signed 32-bit distance
}

/**
 * @typedef {object} PendingFrame
 * @property {Uint8Array[]} chunks
 * @property {number} received
 * @property {number} total
 * @property {number} bytes
 * @property {boolean} keyframe
 * @property {number} backendTs
 */

export class FrameAssembler {
  /**
   * @param {(au: Uint8Array, keyframe: boolean, backendTs: number) => void} onFrame
   * @param {() => void} onLoss — a frameId gap was observed
   */
  constructor(onFrame, onLoss) {
    this.onFrame = onFrame;
    this.onLoss = onLoss;
    /** @type {Map<number, PendingFrame>} */
    this.pending = new Map();
    this.lastDelivered = -1;
    this.newestSeen = -1;
    this.haveDelivered = false;
  }

  reset() {
    this.pending.clear();
    this.lastDelivered = -1;
    this.newestSeen = -1;
    this.haveDelivered = false;
  }

  /** @param {ArrayBuffer} buf — one DataChannel message (one fragment) */
  push(buf) {
    if (buf.byteLength < HEADER_SIZE) return;
    const dv = new DataView(buf);
    const frameId = dv.getUint32(0); // big-endian
    const chunkIndex = dv.getUint16(4);
    const totalChunks = dv.getUint16(6);
    const flags = dv.getUint8(8);
    const payloadSize = dv.getUint32(9);
    const backendTs = dv.getUint32(13);

    if (totalChunks === 0 || chunkIndex >= totalChunks) return;
    if (HEADER_SIZE + payloadSize > buf.byteLength) return; // truncated
    const payload = new Uint8Array(buf, HEADER_SIZE, payloadSize);

    // Frame already behind us? (retransmit that lost the race)
    if (this.haveDelivered && idDelta(frameId, this.lastDelivered) <= 0) return;

    if (this.newestSeen === -1 || idDelta(frameId, this.newestSeen) > 0) {
      this.newestSeen = frameId;
    }

    let entry = this.pending.get(frameId);
    if (!entry) {
      entry = {
        chunks: new Array(totalChunks),
        received: 0,
        total: totalChunks,
        bytes: 0,
        keyframe: (flags & FLAG_KEYFRAME) !== 0,
        backendTs,
      };
      this.pending.set(frameId, entry);
    }
    if (entry.chunks[chunkIndex] === undefined) {
      // Copy out of the message buffer: the payload view aliases the DC
      // message and we may hold it across messages.
      entry.chunks[chunkIndex] = payload.slice();
      entry.received++;
      entry.bytes += payloadSize;
      entry.keyframe = entry.keyframe || (flags & FLAG_KEYFRAME) !== 0;
    }

    if (entry.received === entry.total) {
      this.pending.delete(frameId);
      this._deliver(frameId, entry);
    }

    this._dropStale();
  }

  /** @param {number} frameId @param {PendingFrame} entry */
  _deliver(frameId, entry) {
    const au = new Uint8Array(entry.bytes);
    let off = 0;
    for (const c of entry.chunks) {
      au.set(c, off);
      off += c.length;
    }
    if (this.haveDelivered) {
      const gap = idDelta(frameId, this.lastDelivered);
      if (gap > 1) this.onLoss();
    }
    this.lastDelivered = frameId;
    this.haveDelivered = true;
    this.onFrame(au, entry.keyframe, entry.backendTs);
  }

  _dropStale() {
    let lost = false;
    for (const [id] of this.pending) {
      if (idDelta(this.newestSeen, id) > STALE_FRAME_WINDOW) {
        this.pending.delete(id);
        lost = true;
      }
    }
    if (this.pending.size > MAX_PENDING_FRAMES) {
      // Pathological: shed oldest first.
      const ids = [...this.pending.keys()].sort((a, b) => idDelta(a, b));
      while (ids.length > MAX_PENDING_FRAMES) {
        this.pending.delete(ids.shift());
        lost = true;
      }
    }
    if (lost) this.onLoss();
  }
}

export class VideoPipeline {
  /**
   * @param {HTMLCanvasElement} canvas — will be transferred to the worker
   * @param {'h264'|'hevc'} codec — expected codec from streamInfo
   * @param {{
   *   requestIdr: () => void,
   *   onStats?: (stats: object) => void,
   *   onFirstFrame?: () => void,
   *   onFatal?: (msg: string) => void,
   * }} cbs
   */
  constructor(canvas, codec, cbs) {
    this.cbs = cbs;
    this.stopped = false;
    this._lastIdrReq = 0;

    this.worker = new Worker(new URL('./video-worker.js', import.meta.url), {
      type: 'module',
    });
    const offscreen = canvas.transferControlToOffscreen();
    this.worker.postMessage({ type: 'init', canvas: offscreen, codec }, [offscreen]);

    // An uncaught exception in the worker would otherwise be a silent black
    // screen — surface it.
    this.worker.onerror = (e) => {
      const msg = 'worker uncaught: ' + (e.message || 'unknown') + ' @' + (e.filename || '') + ':' + (e.lineno || 0);
      if (cbs.onLog) cbs.onLog('[video] ' + msg);
      if (cbs.onFatal) cbs.onFatal(msg);
    };

    this.worker.onmessage = (e) => {
      const m = e.data;
      switch (m.type) {
        case 'requestIdr':
          this._requestIdr();
          break;
        case 'stats':
          if (cbs.onStats) cbs.onStats(m);
          break;
        case 'firstFrame':
          if (cbs.onFirstFrame) cbs.onFirstFrame();
          break;
        case 'fatal':
          if (cbs.onFatal) cbs.onFatal(m.msg || 'video decoder failed');
          break;
        case 'log':
          if (cbs.onLog) cbs.onLog(m.msg);
          break;
        default:
          break;
      }
    };

    this.assembler = new FrameAssembler(
      (au, keyframe, backendTs) => {
        if (this.stopped) return;
        this.worker.postMessage(
          { type: 'frame', data: au.buffer, keyframe, backendTs },
          [au.buffer],
        );
      },
      () => {
        if (this.stopped) return;
        this.worker.postMessage({ type: 'frameLoss' });
        this._requestIdr();
      },
    );
  }

  /** Rate-limited IDR request toward the bridge (1/s). */
  _requestIdr() {
    const now = performance.now();
    if (now - this._lastIdrReq < 1000) return;
    this._lastIdrReq = now;
    this.cbs.requestIdr();
  }

  /** @param {ArrayBuffer} buf — raw video-DC message */
  pushFragment(buf) {
    if (!this.stopped) this.assembler.push(buf);
  }

  /**
   * The transport was swapped mid-session: frameIds restart from 0, so the
   * assembler's "behind lastDelivered" duplicate filter must be cleared.
   */
  resetStream() {
    this.assembler.reset();
  }

  destroy() {
    this.stopped = true;
    try {
      this.worker.postMessage({ type: 'stop' });
    } catch (e) { /* worker gone */ }
    // Give the worker a beat to close the decoder before hard-terminating.
    const w = this.worker;
    setTimeout(() => w.terminate(), 250);
  }
}
