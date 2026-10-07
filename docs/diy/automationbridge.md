# Automation Bridge Protocol v1

Local IPC interface for external automation clients: decoded frame capture,
virtual controller input, and an OSD debug overlay. Supported on Linux and
macOS; Windows is experimental and disabled by default there.
Enabled by setting the environment variable `CHIAKI_AUTOMATION` when starting
the GUI (any value, including `0`).

## Transport

- Newline-delimited JSON (NDJSON, UTF-8,
  one message per line, max 64 KiB per message).
- Linux/macOS: Unix domain socket (stream) at `$XDG_RUNTIME_DIR/chiaki-ng/automation.sock`
  (fallback `/tmp/chiaki-ng-$USER/automation.sock`); directory mode 0700.
- Windows: named pipe `\\.\pipe\chiaki-ng-automation` (`chiaki-ng-automation` with `QLocalSocket`).
- Never listen on TCP.
- Video frames are NOT sent over the socket; they are published through a
  shared-memory ring (see below). The socket carries control and events only.

## Commands (client → server)

```json
{"cmd":"hello","client":"<name>","version":1}
{"cmd":"ping"}
{"cmd":"set_controller","buttons":["CROSS","L1"],"lx":0,"ly":0,"rx":0,"ry":0,"l2":0,"r2":0,"hold_ms":0}
{"cmd":"controller_idle"}
{"cmd":"subscribe_frames","format":"nv12","max_fps":20}
{"cmd":"unsubscribe_frames"}
{"cmd":"osd_text","lines":["line1","line2"]}
{"cmd":"osd_markers","rects":[{"x":0.5,"y":0.3,"w":0.05,"h":0.2,"color":"#00ff00","label":"pick"}]}
{"cmd":"osd_clear"}
```

Notes on `set_controller`:

- `buttons`: subset of `CROSS MOON BOX PYRAMID DPAD_LEFT DPAD_RIGHT DPAD_UP
  DPAD_DOWN L1 R1 L3 R3 OPTIONS SHARE TOUCHPAD PS` (names match
  `CHIAKI_CONTROLLER_BUTTON_*` in `lib/include/chiaki/controller.h`).
- `lx`/`ly`/`rx`/`ry`: int16 (-32767..32767); `l2`/`r2`: 0..255.
  Omitted fields mean neutral.
- The state stays active until replaced. `{"cmd":"controller_idle"}` or
  `set_controller` with all-neutral fields releases it.
- `set_controller` is rejected with an error while no session is active
  (`session` = `idle`).
- `hold_ms > 0`: server auto-releases to idle after N milliseconds (clamped to 60000).
- When dpad touch gesture conversion is enabled in the GUI settings, virtual
  dpad buttons also trigger the touchpad swipe conversion.

Notes on `subscribe_frames`:

- `max_fps` omitted or <= 0 is treated as 30 (clamped to 1..60).

Notes on `osd_markers`:

- `x`/`y`/`w`/`h` are normalized video-frame coordinates, clamped to 0..1.

## Events (server → client)

```json
{"event":"hello","version":1,"session":"idle|connecting|connected"}
{"event":"pong"}
{"event":"ok","cmd":"<cmd>"}
{"event":"error","cmd":"<cmd>","message":"..."}
{"event":"frames","shm":"/dev/shm/chiaki-ab-f<pid>","slots":4,"slot_size":3110424,"width":1920,"height":1080,"format":"nv12"}
{"event":"frame","index":123,"slot":1,"width":1920,"height":1080,"pts":12.34}
{"event":"session","state":"connected"}
{"event":"input_blocked","blocked":true}
```

`frames` is the reply to `subscribe_frames`; `frame` is emitted per published
frame (advisory; readers should primarily watch the ring itself). In `frames`,
`width`/`height` are 0 until the first frame is published — take them from
the ring header or `frame` events instead.

## Shared-memory frame ring

- Shared-memory object name: `chiaki-ab-f<pid>` (short, to fit the macOS
  30-byte `shm_open` name limit). The advertised locator is platform-specific
  and always announced in the `frames` event — clients must not hardcode it:
  - Linux: `/dev/shm/chiaki-ab-f<pid>` (open + mmap)
  - macOS: `/chiaki-ab-f<pid>` (`shm_open` by name; no `/dev/shm` listing
    exists — use `_posixshmem` / `multiprocessing.shared_memory`)
  - Windows: `Local\chiaki-ab-f<pid>` (named file mapping; Python:
    `mmap.mmap(-1, size, tagname="Local\\chiaki-ab-f<pid>")`)
- Windows lifecycle is kernel-managed (object dies with the last handle);
  on POSIX a stale object from a crashed writer is removed and recreated
  once on startup (the embedded pid makes a live-owner collision impossible).
- Header (64 bytes, little-endian): `char magic[8] = "CHIAFRM1"` @0,
  `u32 version` @8, `u32 slot_count` @12, `u32 slot_size` @16,
  `u32 width` @20, `u32 height` @24, `u32 reserved` (0) @28,
  `u64 write_index` @32, `u32 creator_pid` @40, zero padding to 64.
- Each slot: `u64 seq` (seqlock: odd while a write is in progress),
  `u64 frame_index`, `double pts`, then the NV12 payload (`width*height*1.5`
  bytes).
- Reader discipline: read `seq`, copy payload, re-read `seq`; retry if it
  changed or is odd. Frames may be dropped for slow consumers.
- The server MUST NOT block the decoder thread on a slow or absent reader.

## Safety rules

- One control client at a time: the first client to send a control command
  (`set_controller`, `controller_idle`, `osd_text`, `osd_markers`,
  `osd_clear`) takes the control role; control commands from other clients
  get an error until the holder disconnects.
- Control client disconnect ⇒ automation controller state resets to idle.
  Otherwise the state persists until replaced; use `hold_ms` for
  time-limited presses.
- `input_blocked` resets to false when the session ends.
- Automation input is OR-merged with physical input (buttons OR, sticks
  max-abs, triggers max) — physical input cannot override an actively driven
  automation control. Clients must send `controller_idle` when done.
- Hold buttons for at least 30–50 ms; expect ~100 ms end-to-end latency on
  streamed input→video loops.
