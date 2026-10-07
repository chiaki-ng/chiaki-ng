# Automation Bridge Protocol v1

Local IPC interface for external automation clients: decoded frame capture,
virtual controller input, and an OSD debug overlay. Supported on Linux and
macOS; Windows is experimental and disabled by default there.
Enabled by setting the environment variable `CHIAKI_AUTOMATION` when starting
the GUI (any value, including `0`).

## Transport

- Newline-delimited JSON (NDJSON, UTF-8,
  one message per line, max 64 KiB excluding the newline). Oversized messages close the connection.
- Linux/macOS: Unix domain socket (stream) at `$XDG_RUNTIME_DIR/chiaki-ng/automation.sock`
  (fallback `/tmp/chiaki-ng-$USER/automation.sock` when `XDG_RUNTIME_DIR` is unset or empty); directory mode 0700.
- Windows: named pipe `\\.\pipe\chiaki-ng-automation` (`chiaki-ng-automation` with `QLocalSocket`).
- Never listen on TCP.
- At most 16 clients are accepted. Each client has a 1 MiB outgoing queue limit. Clients that exceed it are disconnected, releasing their input and frame subscription.
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
- The state stays active until replaced or the session ends. `{"cmd":"controller_idle"}` or
  `set_controller` with all-neutral fields releases it.
- `set_controller` is rejected with an error while no session is active
  (`session` = `idle`).
- `hold_ms > 0`: server auto-releases to idle after N milliseconds (clamped to 1..60000 before truncating fractions).
- When dpad touch gesture conversion is enabled in the GUI settings, virtual
  dpad buttons also trigger the touchpad swipe conversion.

Notes on `subscribe_frames`:

- `max_fps` omitted or <= 0 is treated as 30 (clamped to 1..60).
- The Pi decoder renders directly without CPU frames. Subscriptions fail for Pi decoder sessions. Select the FFmpeg decoder in settings to capture frames.
- Ending a session or starting a Pi decoder session cancels an existing subscription and sends `frames_stopped`. Close the old mapping and subscribe again for the next supported session.

Notes on `osd_markers`:

- `x`/`y`/`w`/`h` are normalized video-frame coordinates, clamped to 0..1.
- Width and height are limited to `1-x` and `1-y` so rectangles stay within the video frame.

## Events (server → client)

```json
{"event":"hello","version":1,"session":"idle|connecting|connected","input_blocked":false}
{"event":"pong"}
{"event":"ok","cmd":"<cmd>"}
{"event":"error","cmd":"<cmd>","message":"..."}
{"event":"frames","shm":"/dev/shm/chiaki-ab-f<pid>","slots":4,"slot_size":3110424,"width":1920,"height":1080,"format":"nv12"}
{"event":"frame","index":123,"slot":1,"width":1920,"height":1080,"pts":12.34}
{"event":"frames_stopped","message":"..."}
{"event":"session","state":"connected"}
{"event":"input_blocked","blocked":true}
```

`hello` reports the current session and `input_blocked` state, including for clients that connect after input becomes blocked. Later changes arrive as `input_blocked` events.

`frames` is the reply to `subscribe_frames`; `frame` events are advisory and may
be coalesced when the GUI is busy. Readers should primarily watch the ring itself.
In `frames`, `width`/`height` are 0 until the first frame is published. Read each
frame’s dimensions from its slot snapshot.

## Shared-memory frame ring (version 2)

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
- Header (64 bytes, little-endian): `char magic[8] = "CHIAFRM2"` @0,
  `u32 version` (2) @8, `u32 slot_count` @12, `u32 slot_size` @16,
  `u32 width` @20, `u32 height` @24, `u32 reserved` (0) @28,
  `u32 write_index` @32, `u32 reserved` (0) @36, `u32 creator_pid` @40, zero padding to 64.
- Each slot: `u32 seq` (seqlock: odd while a write is in progress),
  `u16 width` @4, `u16 height` @6, `u64 frame_index` @8, `double pts` @16, then the NV12 payload (`width*height*1.5`
  bytes).
- Slot dimensions belong to the same seqlock snapshot as the pixels. Use them to decode each slot, including after a resolution change. Header dimensions describe the latest publication only.
- Aligned 32-bit counter accesses are atomic on all supported targets. On 32-bit targets, the 64-bit slot fields can tear, which the sequence recheck detects.
- `write_index` and `seq` wrap modulo 2^32. Use the 64-bit `frame_index` to compare frame freshness.
- Reader discipline: atomically load the aligned `u32 seq` with acquire ordering and retry if it is odd. Copy the slot metadata and payload into private memory, execute an acquire read fence, then atomically reload `seq`. Accept the copy only if both values are equal and even. Frames may be dropped for slow consumers.
- The first acquire load keeps the copy after the initial sequence check. The read fence keeps the copy before the final sequence check. A final acquire load alone does not replace that fence. Both compiler and CPU ordering are required, including on ARM.
- Use native interprocess atomic loads and read barriers. Plain loads, `volatile`, Python `struct.unpack_from`, and the Python GIL do not supply this ordering. For GCC/Clang, use `__atomic_load_n(seq, __ATOMIC_ACQUIRE)`, copy, `__atomic_thread_fence(__ATOMIC_ACQUIRE)`, then `__atomic_load_n(seq, __ATOMIC_RELAXED)`. These operations must be lock-free on the mapped 32-bit counter. If using `write_index` to select a slot, load it with acquire ordering too.
- The server MUST NOT block the decoder thread on a slow or absent reader.

## Safety rules

- One control client at a time: the first client to send a control command
  (`set_controller`, `controller_idle`, `osd_text`, `osd_markers`,
  `osd_clear`) takes the control role; control commands from other clients
  get an error until the holder disconnects.
- Control client disconnect ⇒ automation controller state resets to idle.
  Otherwise the state persists until replaced or the session ends; use `hold_ms` for
  time-limited presses.
- `input_blocked` resets to false when the session ends.
- Automation input is OR-merged with physical input (buttons OR, sticks
  max-abs, triggers max) — physical input cannot override an actively driven
  automation control. Clients must send `controller_idle` when done.
- Hold buttons for at least 30–50 ms; expect ~100 ms end-to-end latency on
  streamed input→video loops.
