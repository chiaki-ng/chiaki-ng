/**
 * video-worker.js — VideoDecoder + OffscreenCanvas worker for chiaki-web.
 *
 * Receives complete Annex-B access units from the main thread (see video.js),
 * derives decoder configuration from the in-band parameter sets, decodes with
 * WebCodecs and paints frames onto the transferred OffscreenCanvas.
 *
 * Messages in:  {type:'init', canvas, codec}
 *               {type:'frame', data:ArrayBuffer, keyframe:boolean, backendTs:number}
 *               {type:'frameLoss'} — main saw a frameId gap
 *               {type:'stop'}
 * Messages out: {type:'requestIdr'} {type:'stats',...} {type:'firstFrame'}
 *               {type:'fatal', msg}
 */

/* ────────────────────────── Annex-B / NAL helpers ───────────────────────── */

const H264_NAL_SPS = 7;
const H264_NAL_PPS = 8;
const HEVC_NAL_VPS = 32;
const HEVC_NAL_SPS = 33;
const HEVC_NAL_PPS = 34;

/**
 * Split an Annex-B buffer into NAL units (start codes removed).
 * Accepts both 3-byte and 4-byte start codes.
 * @param {Uint8Array} buf
 * @returns {Uint8Array[]}
 */
function splitNals(buf) {
  /** @type {Uint8Array[]} */
  const out = [];
  const len = buf.length;
  let i = 0;
  let nalStart = -1;
  while (i + 2 < len) {
    if (buf[i] === 0 && buf[i + 1] === 0) {
      let scLen = 0;
      if (buf[i + 2] === 1) scLen = 3;
      else if (i + 3 < len && buf[i + 2] === 0 && buf[i + 3] === 1) scLen = 4;
      if (scLen) {
        if (nalStart >= 0) out.push(buf.subarray(nalStart, i));
        nalStart = i + scLen;
        i += scLen;
        continue;
      }
    }
    i++;
  }
  if (nalStart >= 0 && nalStart < len) out.push(buf.subarray(nalStart, len));
  return out;
}

/**
 * Strip emulation-prevention bytes (00 00 03 → 00 00) from a NAL unit,
 * returning a fresh Uint8Array.
 * @param {Uint8Array} nal
 */
function deEmulate(nal) {
  const out = new Uint8Array(nal.length);
  let o = 0;
  for (let i = 0; i < nal.length; i++) {
    if (i >= 2 && nal[i] === 3 && nal[i - 1] === 0 && nal[i - 2] === 0) continue;
    out[o++] = nal[i];
  }
  return out.subarray(0, o);
}

function h264NalType(nal) {
  return nal.length ? nal[0] & 0x1f : -1;
}
function hevcNalType(nal) {
  return nal.length >= 2 ? (nal[0] >> 1) & 0x3f : -1;
}

function bytesEqual(a, b) {
  if (!a || !b || a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

/** Holds the current parameter sets and knows when configuration is possible. */
class ParamSets {
  /** @param {'h264'|'hevc'} codec */
  constructor(codec) {
    this.codec = codec;
    /** @type {Uint8Array|null} */ this.sps = null;
    /** @type {Uint8Array|null} */ this.pps = null;
    /** @type {Uint8Array|null} */ this.vps = null;
  }

  ready() {
    return this.codec === 'hevc' ? !!(this.vps && this.sps && this.pps) : !!(this.sps && this.pps);
  }

  /**
   * Scan an AU for parameter sets.
   * @param {Uint8Array[]} nals
   * @returns {boolean} true if any parameter set CHANGED (or first appeared)
   */
  absorb(nals) {
    let changed = false;
    for (const n of nals) {
      if (this.codec === 'hevc') {
        const t = hevcNalType(n);
        if (t === HEVC_NAL_VPS && !bytesEqual(this.vps, n)) {
          this.vps = n.slice();
          changed = true;
        } else if (t === HEVC_NAL_SPS && !bytesEqual(this.sps, n)) {
          this.sps = n.slice();
          changed = true;
        } else if (t === HEVC_NAL_PPS && !bytesEqual(this.pps, n)) {
          this.pps = n.slice();
          changed = true;
        }
      } else {
        const t = h264NalType(n);
        if (t === H264_NAL_SPS && !bytesEqual(this.sps, n)) {
          this.sps = n.slice();
          changed = true;
        } else if (t === H264_NAL_PPS && !bytesEqual(this.pps, n)) {
          this.pps = n.slice();
          changed = true;
        }
      }
    }
    return changed;
  }
}

/* ─────────────────── codec strings + description records ────────────────── */

/** avc1.PPCCLL from the SPS profile/compat/level bytes. */
function h264CodecString(sps) {
  if (!sps || sps.length < 4) return 'avc1.64002a';
  const hex = (b) => b.toString(16).padStart(2, '0');
  return 'avc1.' + hex(sps[1]) + hex(sps[2]) + hex(sps[3]);
}

/** AVCDecoderConfigurationRecord (avcC) with one SPS and one PPS. */
function buildAvcC(sps, pps) {
  const out = new Uint8Array(11 + sps.length + pps.length);
  let o = 0;
  out[o++] = 1; // version
  out[o++] = sps[1];
  out[o++] = sps[2];
  out[o++] = sps[3];
  out[o++] = 0xff; // 4-byte NALU lengths
  out[o++] = 0xe1; // one SPS
  out[o++] = sps.length >> 8;
  out[o++] = sps.length & 0xff;
  out.set(sps, o);
  o += sps.length;
  out[o++] = 1; // one PPS
  out[o++] = pps.length >> 8;
  out[o++] = pps.length & 0xff;
  out.set(pps, o);
  return out;
}

/** MSB-first bit reader with unsigned Exp-Golomb, for SPS walking. */
class BitReader {
  constructor(bytes) {
    this.b = bytes;
    this.pos = 0;
  }
  u(n) {
    let v = 0;
    for (let i = 0; i < n; i++) {
      const idx = this.pos >> 3;
      if (idx >= this.b.length) throw new RangeError('bitstream overrun');
      v = (v << 1) | ((this.b[idx] >> (7 - (this.pos & 7))) & 1);
      this.pos++;
    }
    return v;
  }
  ue() {
    let zeros = 0;
    while (this.u(1) === 0) if (++zeros > 31) throw new RangeError('ue too long');
    return zeros === 0 ? 0 : (1 << zeros) - 1 + this.u(zeros);
  }
}

/**
 * Walk a de-emulated HEVC SPS far enough to learn chroma format and bit
 * depths (needed for a well-formed hvcC). Defaults to 4:2:0 8-bit on error.
 * @param {Uint8Array} spsClean — de-emulated, includes 2-byte NAL header
 */
function hevcSpsFormatInfo(spsClean) {
  const info = { chroma: 1, lumaMinus8: 0, chromaMinus8: 0 };
  try {
    const r = new BitReader(spsClean.subarray(2));
    r.u(4); // vps id
    const maxSub = r.u(3);
    r.u(1); // temporal nesting
    // profile_tier_level: 96 bits of general PTL
    r.u(8); // profile_space/tier/profile_idc
    r.u(32); // compatibility flags
    r.u(32);
    r.u(16); // 48 constraint bits
    r.u(8); // level idc
    const profPresent = [];
    const lvlPresent = [];
    for (let i = 0; i < maxSub; i++) {
      profPresent.push(r.u(1));
      lvlPresent.push(r.u(1));
    }
    if (maxSub > 0) for (let i = maxSub; i < 8; i++) r.u(2);
    for (let i = 0; i < maxSub; i++) {
      if (profPresent[i]) {
        r.u(8);
        r.u(32);
        r.u(32);
        r.u(16);
      }
      if (lvlPresent[i]) r.u(8);
    }
    r.ue(); // sps id
    info.chroma = r.ue();
    if (info.chroma === 3) r.u(1);
    r.ue(); // width
    r.ue(); // height
    if (r.u(1)) {
      r.ue();
      r.ue();
      r.ue();
      r.ue(); // conformance window
    }
    info.lumaMinus8 = r.ue();
    info.chromaMinus8 = r.ue();
  } catch (e) {
    /* keep defaults */
  }
  return info;
}

/**
 * hvc1.P.C.Lxxx.B0 from the SPS profile-tier-level.
 * @param {Uint8Array} sps — raw SPS NAL (may contain emulation bytes)
 */
function hevcCodecString(sps) {
  if (!sps || sps.length < 6) return 'hvc1.1.6.L153.B0';
  const rbsp = deEmulate(sps.subarray(2));
  // rbsp[0]: sps header byte; rbsp[1]: profile_space/tier/profile_idc;
  // rbsp[2..5]: profile compatibility flags; rbsp[6..11]: constraint flags;
  // rbsp[12]: general_level_idc.
  const profileIdc = rbsp.length > 1 ? rbsp[1] & 0x1f : 1;
  const tier = rbsp.length > 1 && rbsp[1] & 0x20 ? 'H' : 'L';
  // Compatibility field: the 32 compat flags in bit-reversed order, hex.
  let compat = 6;
  if (rbsp.length > 5) {
    const flags =
      ((rbsp[2] << 24) | (rbsp[3] << 16) | (rbsp[4] << 8) | rbsp[5]) >>> 0;
    let rev = 0;
    for (let i = 0; i < 32; i++) if (flags & (1 << i)) rev |= 1 << (31 - i);
    compat = (rev >>> 0).toString(16);
  }
  let level = rbsp.length > 12 ? rbsp[12] : 153;
  if (level < 30) level = 153; // implausible level → safe 5.1
  return `hvc1.${profileIdc}.${compat}.${tier}${level}.B0`;
}

/**
 * HEVCDecoderConfigurationRecord (hvcC).
 *
 * The PTL bytes come from the DE-EMULATED SPS (emulation bytes shift the
 * fixed offsets), and the parameter-set arrays also carry de-emulated NALs:
 * Chromium cross-checks the array SPS against the header PTL and rejects the
 * config when raw emulation bytes make them disagree.
 */
function buildHvcC(vps, sps, pps) {
  const v = deEmulate(vps);
  const s = deEmulate(sps);
  const p = deEmulate(pps);
  if (s.length < 15) return null;
  const fmt = hevcSpsFormatInfo(s);

  const out = new Uint8Array(23 + 3 * 5 + v.length + s.length + p.length);
  let o = 0;
  out[o++] = 1; // version
  out.set(s.subarray(3, 15), o); // 12 bytes general PTL
  o += 12;
  out[o++] = 0xf0; // min_spatial_segmentation (reserved bits set)
  out[o++] = 0x00;
  out[o++] = 0xfc; // parallelismType
  out[o++] = 0xfc | (fmt.chroma & 3);
  out[o++] = 0xf8 | (fmt.lumaMinus8 & 7);
  out[o++] = 0xf8 | (fmt.chromaMinus8 & 7);
  out[o++] = 0; // avgFrameRate
  out[o++] = 0;
  out[o++] = 0x0f; // 1 temporal layer, nested, 4-byte lengths
  out[o++] = 3; // three arrays
  for (const [type, nal] of [
    [HEVC_NAL_VPS, v],
    [HEVC_NAL_SPS, s],
    [HEVC_NAL_PPS, p],
  ]) {
    out[o++] = type;
    out[o++] = 0;
    out[o++] = 1;
    out[o++] = nal.length >> 8;
    out[o++] = nal.length & 0xff;
    out.set(nal, o);
    o += nal.length;
  }
  return out;
}

const H264_CODEC_FALLBACKS = [
  'avc1.64002a', // High 4.2
  'avc1.640028',
  'avc1.64001f',
  'avc1.4d002a', // Main 4.2
  'avc1.4d0028',
  'avc1.42e02a', // Constrained Baseline 4.2
  'avc1.42e01e',
];

const HEVC_AVCC_FALLBACKS = [
  'hvc1.1.6.L153.B0', // Main, level 5.1 — typical 1080p60
  'hvc1.1.6.L150.B0',
  'hvc1.1.6.L123.B0',
  'hvc1.1.6.L120.B0',
  'hvc1.1.2.L153.B0',
  'hvc1.2.4.L153.B0', // Main10
];

const SDR_COLOR = {
  colorSpace: { primaries: 'bt709', transfer: 'bt709', matrix: 'bt709', fullRange: false },
};

/* ───────────────────── chunk packaging (Annex-B / AVCC) ──────────────────── */

/**
 * Repackage an AU for the decoder.
 *
 * annexB mode: 4-byte start codes, all NALs kept (Chromium's Annex-B keyframe
 * analyzer needs the parameter sets in-band).
 * AVCC mode: 4-byte big-endian length prefixes, parameter sets stripped (they
 * live in the description) and, for HEVC, leading non-IRAP NALs dropped so
 * the first NAL is an IRAP (Chromium validates this).
 *
 * @param {Uint8Array} au @param {'h264'|'hevc'} codec @param {boolean} annexB
 */
function packageAu(au, codec, annexB) {
  const nals = splitNals(au);
  /** @type {Uint8Array[]} */
  const keep = [];
  let total = 0;
  let sawIrap = false;
  for (const n of nals) {
    if (n.length === 0) continue;
    if (!annexB) {
      if (codec === 'hevc') {
        const t = hevcNalType(n);
        if (t === HEVC_NAL_VPS || t === HEVC_NAL_SPS || t === HEVC_NAL_PPS) continue;
        if (!sawIrap) {
          if (t >= 16 && t <= 21) sawIrap = true;
          else if (t >= 32) continue; // SEI/other non-VCL before the IRAP
        }
      } else {
        const t = h264NalType(n);
        if (t === H264_NAL_SPS || t === H264_NAL_PPS) continue;
      }
    }
    keep.push(n);
    total += 4 + n.length;
  }
  const out = new Uint8Array(total);
  let o = 0;
  for (const n of keep) {
    if (annexB) {
      out[o + 3] = 1; // 00 00 00 01
    } else {
      out[o] = n.length >>> 24;
      out[o + 1] = (n.length >>> 16) & 0xff;
      out[o + 2] = (n.length >>> 8) & 0xff;
      out[o + 3] = n.length & 0xff;
    }
    out.set(n, o + 4);
    o += 4 + n.length;
  }
  return out;
}

/* ─────────────────────────────── pacer ──────────────────────────────────── */

/**
 * HoldbackPacer — minimal presentation reserve keyed off backendTs.
 *
 * Transit delay = now - backendTs (two unrelated clocks, so only variation
 * matters). The minimum delay seen recently is the baseline; each frame's
 * excess over the baseline samples the jitter. The reserve targets the p95 of
 * that excess (clamped to 0..MAX) and each frame is held for whatever part of
 * the reserve it has not already spent in flight. Same shape as a full
 * FramePacer, so one can drop in behind schedule()/reset() later.
 */
class HoldbackPacer {
  constructor() {
    this.MAX = 25; // ms — cap; beyond this latency hurts more than judder
    this.WINDOW_MS = 2000;
    this.DEADBAND = 3; // ms — snap tiny reserves to zero
    this.RESYNC = 500; // ms — discontinuity → rebase
    this.reset();
  }

  reset() {
    this.primed = false;
    this.baseline = 0;
    this.target = 0;
    /** @type {{t:number, ex:number}[]} */
    this.samples = [];
    this.lastControl = 0;
  }

  /**
   * @param {number} backendTs — sender clock ms (0 = unknown → no hold)
   * @param {number} now — performance.now()
   * @returns {number} presentation deadline in the local clock
   */
  schedule(backendTs, now) {
    if (!(backendTs > 0)) return now;
    const delay = now - backendTs;
    if (!this.primed || Math.abs(delay - this.baseline) > this.RESYNC) {
      this.primed = true;
      this.baseline = delay;
      this.samples.length = 0;
      this.target = 0;
      return now;
    }
    // Baseline follows the best path down instantly, up only slowly.
    this.baseline = Math.min(delay, this.baseline + 0.05);
    const excess = delay - this.baseline;
    this.samples.push({ t: now, ex: excess });
    const cutoff = now - this.WINDOW_MS;
    while (this.samples.length && this.samples[0].t < cutoff) this.samples.shift();
    if (this.samples.length > 256) this.samples.splice(0, this.samples.length - 256);

    if (now - this.lastControl > 100) {
      this.lastControl = now;
      const sorted = this.samples.map((x) => x.ex).sort((a, b) => a - b);
      const p95 = sorted.length ? sorted[Math.min(sorted.length - 1, (sorted.length * 0.95) | 0)] : 0;
      let t = Math.min(this.MAX, p95 * 1.15);
      if (t < this.DEADBAND) t = 0;
      // Rise immediately, fall gently.
      this.target = t > this.target ? t : Math.max(t, this.target - 1);
    }
    const wait = this.target - excess;
    return wait > 2 ? now + wait : now;
  }
}

/* ───────────────────────────── worker state ─────────────────────────────── */

const W = {
  /** @type {OffscreenCanvas|null} */ canvas: null,
  /** @type {(OffscreenCanvasRenderingContext2D|null)} */ ctx: null,
  /** @type {'h264'|'hevc'} */ codec: 'h264',
  /** @type {VideoDecoder|null} */ decoder: null,
  /** @type {ParamSets|null} */ params: null,
  configured: false,
  configuring: false,
  annexBMode: false,

  /** @type {{data:Uint8Array, keyframe:boolean, backendTs:number}[]} */
  pendingAus: [],
  /** @type {{frame:VideoFrame, presentAt:number}[]} */
  renderQueue: [],
  pacer: new HoldbackPacer(),
  renderLoopArmed: false,

  referenceValid: true,
  recoveryAttempts: 0,
  stopped: false,
  firstFrameSent: false,
  queueStallSince: 0,
  frameSeq: 0,
  lastChunkTs: 0,

  stats: { decoded: 0, rendered: 0, dropped: 0 },
  fpsWindow: /** @type {number[]} */ ([]),
  lastStatsPost: 0,

  DECODE_QUEUE_MAX: 8,
  MAX_RECOVERY: 8,
  MAX_PENDING: 120,
  MAX_RENDER_QUEUE: 6,
};

// Diagnostic note relayed to the bridge log via the main thread.
function note(msg) {
  post({ type: 'log', msg: '[video-worker] ' + msg });
}

function post(msg, transfer) {
  self.postMessage(msg, transfer || []);
}

let lastIdrPost = 0;
function askForIdr(reason) {
  const now = performance.now();
  if (now - lastIdrPost < 1000) return;
  lastIdrPost = now;
  post({ type: 'requestIdr', reason });
}

/* ─────────────────────────── decoder lifecycle ──────────────────────────── */

function newDecoder() {
  if (W.decoder) {
    try {
      W.decoder.close();
    } catch (e) { /* already closed */ }
  }
  W.configured = false;
  W.configuring = false;
  W.decoder = new VideoDecoder({
    output: onDecodedFrame,
    error: (err) => {
      note('decoder error: ' + (err && (err.name + ' ' + err.message)));
      recoverDecoder();
    },
  });
}

function recoverDecoder() {
  if (W.stopped) return;
  if (++W.recoveryAttempts > W.MAX_RECOVERY) {
    post({ type: 'fatal', msg: 'video decoder failed repeatedly' });
    return;
  }
  note('recovering decoder (attempt ' + W.recoveryAttempts + ')');
  for (const item of W.renderQueue) closeQuietly(item.frame);
  W.renderQueue.length = 0;
  W.pendingAus.length = 0;
  W.referenceValid = false;
  W.pacer.reset();
  // Keep W.params: a recovery keyframe is not guaranteed to repeat the
  // parameter sets, and the cached ones are still valid for this stream.
  newDecoder();
  if (W.params && W.params.ready()) configureDecoder();
  askForIdr('decoder-error');
}

function closeQuietly(frame) {
  try {
    frame.close();
  } catch (e) { /* already closed */ }
}

/**
 * Build the ordered list of decoder configs to try.
 *
 * HEVC: Annex-B first — hev1.* strings with NO description (Chromium's HEVC
 * keyframe validation only parses start codes), from the SPS-derived string
 * down through common fallbacks, each with and without an explicit color
 * space; then AVCC hvc1.* configs with an hvcC description.
 *
 * H.264: avc1 + avcC description first (SPS-derived string, then common
 * fallbacks), progressively dropping colorSpace → dimensions → description.
 *
 * Each entry carries `annexB` so decode packaging matches the config.
 * @returns {{config: VideoDecoderConfig, annexB: boolean}[]}
 */
function buildConfigCandidates() {
  const dims = { codedWidth: 1920, codedHeight: 1080 };
  const lat = { optimizeForLatency: true };
  const cands = [];

  if (W.codec === 'hevc') {
    const primary = hevcCodecString(W.params.sps);
    const hev1 = primary.replace(/^hvc1/, 'hev1');
    const annexbStrings = [hev1];
    for (const fb of HEVC_AVCC_FALLBACKS) {
      const s = fb.replace(/^hvc1/, 'hev1');
      if (!annexbStrings.includes(s)) annexbStrings.push(s);
    }
    for (const codec of annexbStrings) {
      cands.push({ config: { codec, ...dims, ...lat, ...SDR_COLOR }, annexB: true });
      cands.push({ config: { codec, ...dims, ...lat }, annexB: true });
    }
    const desc = buildHvcC(W.params.vps, W.params.sps, W.params.pps);
    if (desc) {
      const strings = [primary];
      for (const fb of HEVC_AVCC_FALLBACKS) if (!strings.includes(fb)) strings.push(fb);
      for (const codec of strings) {
        cands.push({
          config: { codec, description: desc.buffer, ...dims, ...lat, ...SDR_COLOR },
          annexB: false,
        });
        cands.push({ config: { codec, description: desc.buffer, ...dims, ...lat }, annexB: false });
      }
      cands.push({ config: { codec: primary, description: desc.buffer }, annexB: false });
    }
    // Last-ditch: bare Annex-B without dimensions.
    cands.push({ config: { codec: hev1, ...lat }, annexB: true });
    return cands;
  }

  // H.264
  const primary = h264CodecString(W.params.sps);
  const desc = buildAvcC(W.params.sps, W.params.pps);
  const strings = [primary];
  for (const fb of H264_CODEC_FALLBACKS) if (!strings.includes(fb)) strings.push(fb);
  for (const codec of strings) {
    cands.push({
      config: { codec, description: desc.buffer, ...dims, ...lat, ...SDR_COLOR },
      annexB: false,
    });
    cands.push({ config: { codec, description: desc.buffer, ...dims, ...lat }, annexB: false });
  }
  // Progressive degradation on the primary string:
  cands.push({ config: { codec: primary, description: desc.buffer, ...lat }, annexB: false });
  cands.push({ config: { codec: primary, description: desc.buffer } , annexB: false });
  // No description at all → Annex-B input.
  cands.push({ config: { codec: primary, ...dims, ...lat }, annexB: true });
  cands.push({ config: { codec: primary }, annexB: true });
  return cands;
}

async function configureDecoder() {
  if (W.stopped || W.configured || W.configuring || !W.params.ready()) return;
  W.configuring = true;
  const candidates = buildConfigCandidates();
  const tried = [];
  for (const cand of candidates) {
    if (W.stopped) break;
    const tag = cand.config.codec + (cand.config.description ? '+desc' : '') + (cand.annexB ? '/annexb' : '/avcc');
    let supported = false;
    try {
      const res = await VideoDecoder.isConfigSupported(cand.config);
      supported = !!(res && res.supported);
    } catch (e) {
      tried.push(tag + '!throw:' + (e && e.name));
      continue;
    }
    if (!supported) {
      tried.push(tag + '!unsup');
      continue;
    }
    try {
      W.decoder.configure(cand.config);
      W.configured = true;
      W.annexBMode = cand.annexB;
      W.configuring = false;
      note('configured ' + tag);
      flushPendingAus();
      return;
    } catch (e) {
      tried.push(tag + '!cfg:' + (e && e.name));
      // configure() itself refused — move on down the waterfall.
    }
  }
  W.configuring = false;
  note('all configs failed: ' + tried.join(' '));
  post({ type: 'fatal', msg: 'no supported ' + W.codec + ' decoder configuration' });
}

function flushPendingAus() {
  // A decoder must see a keyframe first; drop leading deltas.
  while (W.pendingAus.length && !W.pendingAus[0].keyframe) {
    W.pendingAus.shift();
    W.stats.dropped++;
  }
  const backlog = W.pendingAus;
  W.pendingAus = [];
  for (const au of backlog) decodeAu(au.data, au.keyframe, au.backendTs);
}

/* ─────────────────────────────── decoding ───────────────────────────────── */

/**
 * @param {Uint8Array} data — complete Annex-B AU
 * @param {boolean} keyframe @param {number} backendTs
 */
function handleAu(data, keyframe, backendTs) {
  if (W.stopped) return;

  if (!W.sawFirstAu) {
    W.sawFirstAu = true;
    note('first AU: ' + data.length + 'B keyframe=' + keyframe);
  }

  // Track parameter sets. New/changed ones on a keyframe → (re)configure.
  const nals = splitNals(data);
  const changed = W.params.absorb(nals);
  if (!W.notedParams && W.params.ready()) {
    W.notedParams = true;
    note('parameter sets acquired');
  }
  if (!W.configured) {
    if (W.params.ready()) configureDecoder();
    else if (!keyframe) {
      // Waiting on parameter sets; nudge for a keyframe once in a while.
      if (W.pendingAus.length % 30 === 29) askForIdr('waiting-for-params');
    }
  } else if (changed && keyframe) {
    // Stream reconfigured (new SPS/PPS): rebuild the decoder around it.
    console.log('[video-worker] parameter sets changed — reconfiguring');
    for (const item of W.renderQueue) closeQuietly(item.frame);
    W.renderQueue.length = 0;
    W.pacer.reset();
    newDecoder();
    configureDecoder();
  }

  decodeAu(data, keyframe, backendTs);
}

function decodeAu(data, keyframe, backendTs) {
  if (!W.configured) {
    if (W.pendingAus.length < W.MAX_PENDING) W.pendingAus.push({ data, keyframe, backendTs });
    return;
  }
  if (!W.decoder || W.decoder.state === 'closed') {
    recoverDecoder();
    return;
  }
  if (!keyframe && !W.referenceValid) {
    W.stats.dropped++;
    askForIdr('reference-invalid');
    return;
  }

  // Backpressure: if the decode queue stays saturated, shed deltas and
  // eventually assume the decoder wedged.
  if (!keyframe && W.decoder.decodeQueueSize >= W.DECODE_QUEUE_MAX) {
    const now = performance.now();
    if (W.queueStallSince === 0) W.queueStallSince = now;
    const stalled = now - W.queueStallSince;
    if (stalled > 1000) {
      W.queueStallSince = 0;
      recoverDecoder();
      return;
    }
    if (stalled > 200) {
      W.stats.dropped++;
      W.referenceValid = false;
      askForIdr('decode-queue-overflow');
      return;
    }
  } else {
    W.queueStallSince = 0;
  }

  // Chunk timestamps: derive from backendTs (µs), kept strictly monotonic.
  let ts = backendTs > 0 ? backendTs * 1000 : W.frameSeq * 16667;
  if (ts <= W.lastChunkTs) ts = W.lastChunkTs + 1;
  W.lastChunkTs = ts;
  W.frameSeq++;

  const payload = packageAu(data, W.codec, W.annexBMode);
  if (payload.length === 0) return;
  try {
    const chunk = new EncodedVideoChunk({
      type: keyframe ? 'key' : 'delta',
      timestamp: ts,
      duration: 16667,
      data: payload,
    });
    submitTimes.set(ts, { perf: performance.now(), backendTs });
    if (submitTimes.size > 240) submitTimes.clear();
    W.decoder.decode(chunk);
    if (keyframe) W.referenceValid = true;
  } catch (e) {
    W.stats.dropped++;
    recoverDecoder();
  }
}

/** chunk timestamp (µs) → submit bookkeeping, matched up in output(). */
const submitTimes = new Map();

/* ─────────────────────────── render scheduling ──────────────────────────── */

/** @param {VideoFrame} frame */
function onDecodedFrame(frame) {
  W.stats.decoded++;
  W.recoveryAttempts = 0;

  const now = performance.now();
  const submit = submitTimes.get(frame.timestamp);
  let backendTs = 0;
  if (submit) {
    submitTimes.delete(frame.timestamp);
    backendTs = submit.backendTs;
  }

  if (!W.firstFrameSent) {
    W.firstFrameSent = true;
    post({ type: 'firstFrame' });
  }

  const presentAt = W.pacer.schedule(backendTs, now);
  if (W.renderQueue.length >= W.MAX_RENDER_QUEUE) {
    // Memory bound; drop the oldest queued frame, keep the fresh one.
    closeQuietly(W.renderQueue.shift().frame);
    W.stats.dropped++;
  }
  W.renderQueue.push({ frame, presentAt });
  armRenderLoop();
}

function armRenderLoop() {
  if (W.renderLoopArmed || W.stopped) return;
  W.renderLoopArmed = true;
  // rAF exists in workers that own an OffscreenCanvas on modern engines;
  // fall back to a short timer elsewhere.
  if (typeof self.requestAnimationFrame === 'function') {
    self.requestAnimationFrame(renderTick);
  } else {
    setTimeout(renderTick, 8);
  }
}

function renderTick() {
  W.renderLoopArmed = false;
  if (W.stopped) return;
  const now = performance.now();

  // Collect all frames that are due; paint only the freshest of them.
  let due = null;
  while (W.renderQueue.length && W.renderQueue[0].presentAt <= now) {
    if (due) {
      closeQuietly(due.frame);
      W.stats.dropped++;
    }
    due = W.renderQueue.shift();
  }
  if (due) paint(due.frame);
  if (W.renderQueue.length) armRenderLoop();
  postStats(false);
}

/** @param {VideoFrame} frame */
function paint(frame) {
  const w = frame.displayWidth || frame.codedWidth;
  const h = frame.displayHeight || frame.codedHeight;
  try {
    if (W.canvas.width !== w || W.canvas.height !== h) {
      W.canvas.width = w;
      W.canvas.height = h;
    }
    W.ctx.drawImage(frame, 0, 0, w, h);
    W.stats.rendered++;
    W.fpsWindow.push(performance.now());
    closeQuietly(frame);
  } catch (e) {
    // Some engines (WebKit) can refuse drawImage(VideoFrame) on a worker
    // canvas; go through an ImageBitmap instead.
    if (!W.notedPaintError) {
      W.notedPaintError = true;
      note('drawImage(VideoFrame) failed (' + (e && e.name) + '), using ImageBitmap path');
    }
    createImageBitmap(frame)
      .then((bmp) => {
        try {
          W.ctx.drawImage(bmp, 0, 0, w, h);
          W.stats.rendered++;
          W.fpsWindow.push(performance.now());
        } catch (e2) {
          if (!W.notedBitmapError) {
            W.notedBitmapError = true;
            note('ImageBitmap paint failed too: ' + (e2 && e2.name));
          }
        }
        bmp.close();
      })
      .catch((e2) => {
        if (!W.notedBitmapError) {
          W.notedBitmapError = true;
          note('createImageBitmap(VideoFrame) failed: ' + (e2 && e2.name));
        }
      })
      .finally(() => closeQuietly(frame));
  }
}

function postStats(force) {
  const now = performance.now();
  if (!force && now - W.lastStatsPost < 500) return;
  W.lastStatsPost = now;
  const cutoff = now - 2000;
  while (W.fpsWindow.length && W.fpsWindow[0] < cutoff) W.fpsWindow.shift();
  post({
    type: 'stats',
    fps: W.fpsWindow.length / 2,
    decoded: W.stats.decoded,
    rendered: W.stats.rendered,
    dropped: W.stats.dropped,
    decodeQueue: W.decoder ? W.decoder.decodeQueueSize : 0,
    renderQueue: W.renderQueue.length,
    holdbackMs: Math.round(W.pacer.target * 10) / 10,
  });
}

/* ─────────────────────────── message dispatch ───────────────────────────── */

self.onmessage = (e) => {
  const m = e.data;
  switch (m.type) {
    case 'init': {
      W.canvas = m.canvas;
      W.codec = m.codec === 'hevc' ? 'hevc' : 'h264';
      W.params = new ParamSets(W.codec);
      if (typeof VideoDecoder === 'undefined') {
        post({ type: 'fatal', msg: 'WebCodecs is not available in this browser' });
        return;
      }
      W.ctx = W.canvas.getContext('2d', { alpha: false, desynchronized: true });
      if (!W.ctx) W.ctx = W.canvas.getContext('2d'); // engines picky about options
      if (!W.ctx) {
        post({ type: 'fatal', msg: 'could not create canvas context' });
        return;
      }
      newDecoder();
      break;
    }
    case 'frame':
      handleAu(new Uint8Array(m.data), !!m.keyframe, m.backendTs || 0);
      break;
    case 'frameLoss':
      // Upstream saw a frameId gap: deltas past it reference missing frames.
      W.referenceValid = false;
      break;
    case 'stop':
      W.stopped = true;
      if (W.decoder) {
        try {
          W.decoder.close();
        } catch (e2) { /* already closed */ }
        W.decoder = null;
      }
      for (const item of W.renderQueue) closeQuietly(item.frame);
      W.renderQueue.length = 0;
      W.pendingAus.length = 0;
      break;
    default:
      break;
  }
};
