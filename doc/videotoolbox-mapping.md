# VideoToolbox frame mapping

On macOS, `prepareFrameForPresentation` first attempts a read-only direct mapping
of VideoToolbox frames when the existing **Zero-Copy** setting is enabled.
FFmpeg exposes the CVPixelBuffer's CPU planes and retains the backing surface
through the mapped AVFrame. This avoids the additional full-frame CPU copy made
by `av_hwframe_transfer_data`. Renderer upload and presentation still occur;
this is not Metal texture import or an end-to-end zero-copy pipeline.

Mapping failures use the existing transfer/copy path. Disabling **Zero-Copy**
and starting a new session also restores that path. No decoder, renderer,
queue, networking, or audio settings are changed.

## Correctness test

With tests and the FFmpeg decoder enabled in a macOS build:

```sh
cmake --build build --target chiaki-videotoolbox-map-test
ctest --test-dir build -R videotoolbox-map --output-on-failure
```

The test creates NV12 and P010 VideoToolbox surfaces with padded rows. It checks
pixels, timestamps, color metadata, HDR mastering metadata, and aspect ratio.
It also checks pixel access through a cloned mapping after releasing the source
frame, the original mapping, and the caller's hardware context references.
It requires no console and skips if FFmpeg was built without VideoToolbox.

## Synthetic preparation benchmark

Run from the repository root with FFmpeg development headers and `pkg-config`:

```sh
cc -O2 -Wall -Wextra scripts/benchmark-videotoolbox.c -Igui/include \
  $(pkg-config --cflags --libs libavformat libavcodec libavutil) \
  -o /tmp/chiaki-vt-benchmark
ffmpeg -f lavfi -i testsrc2=size=1920x1080:rate=60 -frames:v 180 \
  -pix_fmt nv12 -c:v h264_videotoolbox -b:v 15M -bf 0 /tmp/chiaki-h264.mp4
/tmp/chiaki-vt-benchmark /tmp/chiaki-h264.mp4
```

For HEVC, use `-c:v hevc_videotoolbox`. For 10-bit HEVC, also use
`-pix_fmt p010le -profile:v main10`. Use a different output path for each fixture.
Inputs must contain at least 60 frames and be supported by VideoToolbox.

The benchmark decodes through VideoToolbox, alternates copy/map order, compares
visible pixels, and checks the mapped data after releasing each source frame.
Reported times include frame allocation and metadata propagation, but exclude
decode, validation, unmapping/freeing, GPU upload, and display presentation.

An Apple M5 arm64 run (macOS 26.6.2, FFmpeg 9.0.1) produced these medians of three
run means, with 180 frames per run at 1080p60:

| Codec / mapped format | Transfer/copy (microseconds) | Direct map (microseconds) |
| --- | ---: | ---: |
| H.264 / NV12 | 277.850 | 3.606 |
| HEVC / NV12 | 266.072 | 3.322 |
| HEVC / P010 | 323.367 | 3.528 |

All nine runs passed pixel equality and source-release checks. These are
preparation microbenchmarks, not measurements of Remote Play latency or total
rendering cost. Live streaming on this isolated change, HDR display output,
Intel Macs, and older macOS versions still need validation. In particular,
direct mappings retain decoder surfaces until renderer references are released.
