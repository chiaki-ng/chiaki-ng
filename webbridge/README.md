# chiaki-web

Play PlayStation Remote Play in any modern browser (desktop or mobile).

A small headless bridge embeds libchiaki and runs the actual Remote Play
session; the browser is a thin client. Architecture modeled on
[moonlight-web](https://github.com/linckosz/moonlight-web):

- **Video**: complete Annex-B H.264/HEVC access units from libchiaki's video
  callback, fragmented over a partially-reliable WebRTC DataChannel
  (ordered, 3 retransmits), decoded in the browser with **WebCodecs** in a
  worker, rendered to an OffscreenCanvas.
- **Audio**: the Opus frames from libchiaki's audio sink are sent as a native
  **RTP Opus track** on the same PeerConnection — the browser's jitter
  buffer, in-band FEC and packet-loss concealment handle the lossy path.
- **Input**: JSON over a reliable DataChannel; keyboard, mouse→touchpad,
  Gamepad API (with rumble), and a multi-touch on-screen controller on
  mobile. The bridge feeds `ChiakiControllerState`; libchiaki paces the wire.
- **Everything else** (discovery, wakeup, pairing/registration) is REST.

See `PROTOCOL.md` for the exact wire contract.

## Build

Requires: system libdatachannel, opus, plus chiaki-ng's usual lib deps
(openssl, curl≥8.11 with WebSocket, json-c, miniupnpc, libevent, protoc +
python-protobuf for nanopb). Two header-only deps — nlohmann/json and
cpp-httplib — are fetched and pinned at configure time via CMake
`FetchContent` (network access needed on first configure), not vendored.

```
cmake -B build -G Ninja \
    -DCHIAKI_ENABLE_WEBBRIDGE=ON \
    -DCHIAKI_ENABLE_GUI=OFF -DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_TESTS=OFF \
    -DCHIAKI_USE_SYSTEM_CURL=ON
ninja -C build chiaki-webbridge
```

## Run

```
./build/webbridge/chiaki-webbridge
```

Then open `http://<bridge-host>:9080/`. Options: `--http-port`, `--ws-port`,
`--bind`, `--frontend <dir>`, `--config <file>`, `--verbose`.

First-time setup, all from the web UI:

1. Put the console into pairing mode (Settings → Remote Play → Link Device).
2. "Register" on the console card: enter the PIN and your PSN Account ID
   (base64, same value the chiaki-ng GUI uses).
3. Connect.

Config (registration keys) is stored in `~/.config/chiaki-web/config.json`.

**Demo mode**: the "Demo" button streams an ffmpeg test pattern + Opus test
tone through the full pipeline without a console — useful to validate a
browser/network before touching real hardware.

## Exposing publicly (Cloudflare Tunnel)

The bridge serves HTTP on one port and the signaling WebSocket on another;
for a single public hostname, route the `/ws` path to the WS port (the
frontend automatically uses same-origin `/ws` when served on standard
ports). Example cloudflared ingress:

```yaml
ingress:
  - hostname: playstation.example.com
    path: ^/ws.*
    service: http://localhost:9081
  - hostname: playstation.example.com
    service: http://localhost:9080
  - service: http_status:404
```

Copy `.env.example` to `.env` (in the bridge's working directory) to configure
optional credentials — PSN account ID, an access token, and Cloudflare TURN
keys. `.env` is gitignored; keep real secrets out of the tree.

Set `CHIAKI_WEB_TOKEN=<secret>` in `.env` to require an access token for
everything that touches the console (`/api/hosts|wakeup|register|config` via
`X-Auth-Token`, and the `start` signaling message). The frontend prompts for
it once and stores it in localStorage. This is defense in depth — put a real
auth gate (e.g. Cloudflare Access) in front for production use. Note that
WebRTC media does not traverse the tunnel: the browser connects to the
bridge host directly (LAN or STUN-friendly NAT; no TURN yet).

## ICE backend (advanced: TURN-relayed remote clients)

The ICE engine is compiled into `libdatachannel`, so the bridge uses whichever
`libdatachannel.so` the dynamic linker loads — no bridge-side option selects
it. The default system build uses **libjuice**, which is fine for LAN and
STUN-friendly NAT.

Relay-only clients (UDP-restricted networks, forced-TURN) need a
**libnice**-backed `libdatachannel`: the libjuice build mishandles
TURN-relayed connectivity checks (rejects relayed binding requests with 400).
If you have such a build, select it with the standard loader mechanism — no
wrapper script required:

```
LD_LIBRARY_PATH=/path/to/libnice-datachannel/lib ./chiaki-webbridge
```

## Status / limitations

- LAN-focused: ICE uses host candidates + public STUN. No TURN, no built-in
  remote access — front it with your own reverse proxy/VPN for now (the
  signaling and media are standard WebRTC, so a TURN server drops in).
- PSN remote connection (holepunch) path not wired up yet.
- One streaming client at a time.
- Motion, adaptive triggers, and PS5 haptics are not available in browsers;
  rumble maps to Gamepad `dual-rumble` where supported.
- HTTPS/WSS not terminated by the bridge; use a reverse proxy if you need
  secure origins (required for gamepad on some browsers when not localhost).
