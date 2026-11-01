# RTX Video Super Resolution

chiaki-ng can upscale the stream with NVIDIA RTX Video Super Resolution (VSR), the same
technology the NVIDIA driver uses for browser video. It runs on the GPU right before
the frame is presented and adds roughly 1-5 ms of GPU time per frame (no extra frame
of latency).

## Requirements

- Windows 10/11 x64 build of chiaki-ng compiled with `CHIAKI_ENABLE_NGX=ON`
  (the `Build chiaki-ng Windows x86_64 (VC)` workflow does this)
- NVIDIA GeForce RTX 20 series or newer with a current driver
- Renderer Backend: **Vulkan**
- An SDR stream (HDR streams keep using the regular libplacebo scaler)
- `nvngx_vsr.dll` from the [NVIDIA RTX Video SDK](https://developer.nvidia.com/rtx-video-sdk)
  copied next to `chiaki.exe`. The SDK is a free download with an NVIDIA developer
  account; the DLL is not bundled with chiaki-ng.

## Modes

| Mode | What it does | nvngx_vsr.dll |
|---|---|---|
| Upscale Low … Ultra | AI upscaling when the window is larger than the stream | any (RTX Video SDK 1.1 ships 1.6.0) |
| Artifact Reduction Low … Ultra | removes compression blocking and banding at stream resolution; libplacebo scales afterwards | 1.8.2 or newer (NVIDIA VFX SDK 1.2+) |
| Streaming Medium / Ultra | upscaling plus artifact cleanup tuned for compressed streams, RTX 30 series or newer | 1.8.3 or newer (NVIDIA VFX SDK 1.3+) |

If a mode is not supported by the installed DLL or GPU, VSR turns itself off for
the session and the log says which version is needed.

## Usage

1. Copy `nvngx_vsr.dll` (x64 release build from the SDK) into the chiaki-ng folder.
2. Settings → Video → **RTX Video Super Resolution**: pick a mode (see above).
3. Start a stream. Upscale and Streaming modes are only used when the window/screen
   is larger than the stream resolution (e.g. 1080p stream on a 1440p/4K monitor).
   The stream stats overlay shows the active mode and its GPU time.

The log tells you what happened:

- `[rtx-vsr] RTX Video Super Resolution initialized` and
  `RTX Video Super Resolution active` → VSR is working.
- `FeatureNotFound` → `nvngx_vsr.dll` is missing or in the wrong folder.
- `OutOfDate` → update the NVIDIA driver.

When VSR is active it replaces the spatial upscaler of the selected video preset
(FSR / FSRCNNX); everything else (color mapping, frame mixer, display target) still
applies.
