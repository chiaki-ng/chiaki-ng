# Frame Generation

chiaki-ng can generate extra frames between the frames sent by the console
(for example 60 fps stream → 120 fps on a 120 Hz display). Two backends are
available in Windows builds made by the `Build chiaki-ng Windows x86_64 (VC)`
workflow:

| Backend | GPU | Multiplier |
|---|---|---|
| NVIDIA DLSS Frame Generation | GeForce RTX 40 series or newer | 2x (3x / 4x on RTX 50 series) |
| AMD FSR 3.1 Frame Generation | any GPU with Vulkan | 2x |

Both runtimes (`nvngx_dlssg.dll`, `amd_fidelityfx_vk.dll`) ship next to
`chiaki.exe`; nothing extra has to be downloaded.

## Requirements

- Renderer Backend: **Vulkan**
- Settings → Video → Frame Mapping: **Direct Mapping** (the default)
- An SDR stream (HDR streams are shown without frame generation)
- A display refresh rate of at least multiplier × stream fps
  (the multiplier is reduced automatically otherwise)

## Usage

Settings → Video → **Frame Generation**: pick DLSS or FSR, then the
**Frame Generation Multiplier** (DLSS only). Works together with
[RTX Video Super Resolution](rtxvideo.md): frames are upscaled first, then
generated.

## Latency

Frame generation has to wait for the next stream frame before it can show the
frames in between, so it adds about half a stream frame interval (≈8 ms at
60 fps) plus a few milliseconds of GPU time. Turn it off for latency
sensitive games.

## Limitations

The stream carries no motion vectors or depth, so both backends get zero
motion vectors and a constant depth and rely on their own optical flow. Fast
motion and on-screen UI can show artifacts.

## If it crashes

Frame generation calls into the NVIDIA and AMD runtimes. A crash inside them is
caught where possible and only turns frame generation off for the session. If
chiaki-ng still closes while frame generation starts, it is switched off
automatically on the next launch (the log says so); turn it back on in the
settings once you want to retry.

The first frames log each step (`[framegen] frame N: ...`), which shows where
it stopped.

## Log messages

- `[framegen] ... initialized` and `Frame generation active: ... Nx` → working.
- `DLSS Frame Generation is not available on this GPU/driver` → RTX 40+ GPU and a current driver are needed.
- `Could not load ...amd_fidelityfx_vk.dll` → the DLL is missing next to `chiaki.exe`.
- `Frame generation requires Direct Mapping` → switch Frame Mapping to Direct Mapping.
