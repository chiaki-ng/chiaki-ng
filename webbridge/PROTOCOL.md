# chiaki-web bridge — wire protocol

Contract between the bridge backend (`webbridge/src`) and the web frontend
(`webbridge/frontend`). Architecture modeled on moonlight-web: video as
Annex-B access units over a partially-reliable DataChannel decoded with
WebCodecs, audio as a native RTP Opus track, input as JSON over a reliable
DataChannel.

## Ports

- HTTP (static frontend + REST): default `9080`
- Signaling WebSocket: default `9081` (any path accepted)

The frontend discovers the WS port via `GET /api/info`. When accessed through
a path-preserving reverse proxy (e.g. `https://<host>/lp/9080/`), the frontend
must build the WS URL by substituting the port segment in its own URL
(`/lp/9081/`) rather than using `location.port`.

## REST API (HTTP port)

- `GET /api/info` → `{ "name", "version", "wsPort": 9081 }`
- `GET /api/hosts` → `[ { "host", "nickname", "mac", "ps5": bool,
  "state": "ready"|"standby"|"unknown", "discovered": bool,
  "registered": bool, "appName": string|null } ]`
  (union of discovery results and registered consoles from config)
- `POST /api/wakeup` `{ "host" }` → `204` (console must be registered)
- `POST /api/register` `{ "host", "ps5": bool, "pin": number,
  "psnAccountId": base64 string }` → `200 { "nickname" }` on success,
  `4xx/5xx { "error" }` on failure. Blocking (runs chiaki_regist).
- `GET /api/config` → sanitized config (no keys), includes
  `{ "psnAccountIdSet": bool, "consoles": [...] }`

## Signaling (WebSocket, JSON text messages)

Client connects, then:

1. client → `{ "type": "start", "host": "<ip>" }` or
   `{ "type": "start", "demo": true }`, optional fields:
   `"resolution": "360p"|"540p"|"720p"|"1080p"`, `"fps": 30|60`,
   `"codec": "h264"|"hevc"`, `"bitrate": number (kbps, 0 = preset)`,
   `"transport": "webrtc"|"ws"` (default webrtc)
2. bridge → `{ "type": "iceServers", "iceServers": [RTCIceServer...] }`
   (webrtc only, only when TURN is configured — short-lived credentials
   minted from Cloudflare TURN via CHIAKI_TURN_KEY_ID/CHIAKI_TURN_API_TOKEN;
   sent before the offer so the browser can construct its pc with them)
3. bridge → `{ "type": "streamInfo", "codec": "h264"|"hevc",
   "width", "height", "fps" }`
4. bridge → `{ "type": "offer", "sdp" }` (webrtc only; bridge is always the
   offerer; all media/channels are in this offer)
5. client → `{ "type": "answer", "sdp" }` (webrtc only)
6. both → `{ "type": "candidate", "candidate", "mid" }` (trickle ICE, webrtc)
7. bridge → `{ "type": "status", "state": "connecting"|"connected" }`,
   `{ "type": "quit", "reason", "error": bool }`
8. client → `{ "type": "stop" }` ends the session.
9. client → `{ "type": "switchTransport", "transport": "ws" }` swaps the
   media path mid-session WITHOUT restarting the console session (used by
   the frontend's automatic WebRTC→WebSocket fallback; the console holds
   its Remote Play slot for ~25 s after a disconnect, so a full restart
   would be painful). The bridge answers with `audioInfo` +
   `status: connected` and internally requests an IDR (with SPS/PPS
   prepended) so the fresh decoder can sync. Transport-level frameIds
   restart at 0 — the client must reset its reassembler.

Only one streaming client at a time; a second `start` gets
`{ "type": "quit", "reason": "busy", "error": true }`.

## PeerConnection layout

Order in SDP (bridge side): audio track added BEFORE DataChannels are created
(libdatachannel publishes the offer on the first `createDataChannel`).

- **Audio**: RTP Opus track, sendonly, payload type 111,
  `minptime=10;useinbandfec=1;stereo=1;sprop-stereo=1`, SSRC declared in the
  description. 48 kHz stereo. Browser plays it via an `<audio>`/`AudioContext`
  attached to the received MediaStreamTrack — the browser's jitter buffer,
  FEC and PLC do the work.
- **DataChannel "video"**: `negotiated: true, id: 0, ordered: true,
  maxRetransmits: 3`. Binary. Carries fragmented video frames (below).
- **DataChannel "input"**: `negotiated: true, id: 2` (defaults: reliable,
  ordered). JSON text, bidirectional.
- **DataChannel "mic"** (browser → bridge): `negotiated: true, id: 4,
  ordered: false, maxRetransmits: 0`. Binary: raw 48 kHz mono s16le PCM,
  ideally 480-sample (960-byte) chunks; the bridge re-frames and feeds
  libchiaki's Opus encoder (which produces the exact 40-byte CBR frames the
  console requires). Enable/disable via `{"t":"micOn"}` / `{"t":"micOff"}`
  on the input DC — the bridge sends `connect_microphone` on first enable
  and mute/unmute toggles after.

The frontend must create the same negotiated channels with identical ids and
reliability settings.

## WebSocket transport (`"transport": "ws"`)

Fallback for UDP-hostile networks: no PeerConnection at all — media and input
are multiplexed over the signaling WebSocket (plain TCP, traverses whatever
proxy/tunnel served the page). Binary WS messages carry a 1-byte channel
prefix:

| chan | direction        | payload |
|------|------------------|---------|
| 0x01 | bridge → browser | video: 17-byte fragment header (identical format to the video DC, always `totalChunks=1`) + complete Annex-B AU |
| 0x02 | bridge → browser | audio: interleaved s16le PCM — the bridge decodes the console's Opus (no RTP jitter buffer to lean on; the browser plays it via an AudioWorklet with its own jitter buffer) |
| 0x03 | browser → bridge | mic: 48 kHz mono s16le PCM chunks (same payload as the mic DC) |

Audio format is announced via `{ "type": "audioInfo", "rate", "channels" }`.
Input/event JSON rides the same socket as text; `"t"`-keyed input messages
and `"type"`-keyed signaling share it without collision. The bridge applies
TCP backpressure policy: past ~1 MiB of send backlog delta frames are dropped
(an IDR is requested on drain), stale audio is dropped earlier, and only a
saturated socket (8 MiB) drops keyframes.

## Video fragment format (DC "video", binary, big-endian)

17-byte header per fragment, then payload:

| off | size | field |
|-----|------|-------|
| 0   | u32  | frameId (monotonic, wraps) |
| 4   | u16  | chunkIndex |
| 6   | u16  | totalChunks |
| 8   | u8   | flags: bit0 = keyframe (IDR or contains SPS/PPS/VPS) |
| 9   | u32  | payloadSize (bytes of payload in THIS fragment) |
| 13  | u32  | backendTs (ms, monotonic clock at send time; same for all chunks of a frame) |

Chunk payload cap: 16000 bytes. Reassembled payload = one complete Annex-B
access unit (start codes `00 00 00 01`/`00 00 01`). The first video frame(s)
carry codec parameter sets (SPS/PPS, +VPS for HEVC) — parameter sets may also
arrive as a standalone AU when the stream reconfigures; frontend feeds
everything to VideoDecoder in Annex-B form.

Loss handling: if the frontend observes a frameId gap or a decode error, it
sends `{ "t": "idr" }` on the input DC; the bridge answers by failing the next
video sample callback once, which makes the console send a recovery keyframe.

## Input DC messages

Browser → bridge:

- Controller state (send at input-poll rate; bridge paces the wire):
  `{ "t": "cs", "b": u32, "l2": 0-255, "r2": 0-255,
     "lx": i16, "ly": i16, "rx": i16, "ry": i16,
     "tp": [[id, x, y], ...] }`
  `tp` optional, up to 2 touches on the 1920×942 touchpad plane.
  `b` is the chiaki button bitmask:
  CROSS 1<<0, MOON(circle) 1<<1, BOX(square) 1<<2, PYRAMID(triangle) 1<<3,
  DPAD_LEFT 1<<4, DPAD_RIGHT 1<<5, DPAD_UP 1<<6, DPAD_DOWN 1<<7,
  L1 1<<8, R1 1<<9, L3 1<<10, R3 1<<11, OPTIONS 1<<12, SHARE 1<<13,
  TOUCHPAD 1<<14, PS 1<<15. (L2/R2 are the analog fields.)
- Motion (optional): `{ "t": "mo", "gx","gy","gz","ax","ay","az",
  "ox","oy","oz","ow" }` (gyro rad/s, accel g, orientation quaternion)
- `{ "t": "idr" }` — request recovery keyframe
- `{ "t": "pin", "pin": "1234" }` — login PIN reply

Bridge → browser:

- `{ "t": "rumble", "l": 0-255, "r": 0-255 }`
- `{ "t": "led", "r", "g", "b" }`
- `{ "t": "pinRequest", "incorrect": bool }` — show PIN dialog
- `{ "t": "nickname", "name" }`
- `{ "t": "stats", ... }` (periodic, informational)
