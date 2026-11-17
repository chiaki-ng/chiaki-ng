#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/frame.h>
#ifdef __cplusplus
}
#endif

/**
 * Upscales VideoToolbox-decoded frames with the MetalFX spatial scaler (macOS 13+).
 *
 * Input: an AVFrame with format AV_PIX_FMT_VIDEOTOOLBOX (NV12 or 10-bit biplanar 4:2:0).
 * Output: a new software AVFrame (AV_PIX_FMT_BGRA for SDR, AV_PIX_FMT_RGBA64LE for 10-bit),
 * backed by an IOSurface, sized to fit the target while keeping the aspect ratio.
 *
 * The output keeps the source transfer function (gamma or PQ) and primaries.
 * Only the YCbCr matrix is applied, so colorspace becomes AVCOL_SPC_RGB.
 *
 * Not thread-safe. Use one instance from one thread.
 */
class MetalFXUpscaler
{
public:
	MetalFXUpscaler();
	~MetalFXUpscaler();
	MetalFXUpscaler(const MetalFXUpscaler &) = delete;
	MetalFXUpscaler &operator=(const MetalFXUpscaler &) = delete;

	/** True when this Mac has a Metal device that supports the MetalFX spatial scaler. */
	static bool IsSupported();

	/**
	 * Returns a new upscaled frame, or nullptr when the frame is not handled
	 * (wrong format, no upscale needed, or a Metal error). The caller keeps
	 * ownership of src and owns the returned frame.
	 */
	AVFrame *Process(const AVFrame *src, int target_width, int target_height);

	/**
	 * Like Process(), for a frame whose pixels are in an IOSurface-backed
	 * CVPixelBuffer (passed as void* to keep CoreVideo out of this header), such
	 * as an interpolated frame. `src` provides size and color properties.
	 */
	AVFrame *ProcessPixelBuffer(void *pixel_buffer, const AVFrame *src, int target_width, int target_height);

	/**
	 * If `frame` came from Process(), returns its backing IOSurfaceRef (borrowed,
	 * valid while the frame lives) and its allocation size, so a Vulkan renderer
	 * can import it as a texture instead of uploading the pixels. Else nullptr.
	 */
	static void *FrameIOSurface(const AVFrame *frame, size_t *alloc_size);

	/**
	 * Process() returns before the GPU finishes. Call this before reading the
	 * pixels or sampling the IOSurface of a frame it returned. No-op for other
	 * frames. Usually returns at once, because rendering runs a few ms later.
	 */
	static void WaitFrameReady(const AVFrame *frame);

	/**
	 * Ratio of the panel's native pixels to the current backing pixels for the
	 * display at global point (x, y), in points. A "more space" scaled mode on a
	 * 3024-pixel panel renders at 3600 pixels and returns 0.84; upscaling past
	 * the panel only wastes GPU time. Returns 1.0 when unknown.
	 */
	static double NativePixelScaleAt(double x, double y);

	/** Largest upscale factor per axis. Larger targets are clamped to this factor. */
	static constexpr double kMaxScale = 3.0;

private:
	struct Impl;
	Impl *impl;
};
