#pragma once

#include "frameinterpolator.h"

/**
 * Doubles the frame rate of VideoToolbox-decoded video with
 * VTLowLatencyFrameInterpolation (macOS 26+).
 *
 * For each new frame, Submit() starts an asynchronous interpolation between
 * the previous frame and the new one. The callback receives the frame halfway
 * between them as a software NV12 AVFrame backed by an IOSurface, or nullptr
 * on failure. The caller shows that frame first and the new frame half a
 * frame interval later.
 *
 * Supports 8-bit video-range 4:2:0 input, which is what the PlayStation
 * streams use for SDR. Submit() returns false for other formats.
 *
 * Submit() and Reset() must be called from one thread. The callback runs on a
 * VideoToolbox thread.
 */
class VTFrameInterpolator : public FrameInterpolator
{
public:
	VTFrameInterpolator();
	~VTFrameInterpolator() override;
	VTFrameInterpolator(const VTFrameInterpolator &) = delete;
	VTFrameInterpolator &operator=(const VTFrameInterpolator &) = delete;

	/** True on macOS 26+ when this Mac supports low-latency frame interpolation. */
	static bool IsSupported();

	/**
	 * Remembers `src` as the newest frame and, when possible, starts
	 * interpolating between the previous frame and `src`. Returns true when
	 * `done` will be called (exactly once). Returns false, without calling
	 * `done`, for the first frame, after `discontinuity`, after a size or
	 * format change, or for unsupported input.
	 */
	int MaxMultiplier() const override { return 4; }
	bool Submit(const AVFrame *src, bool discontinuity, int multiplier, Callback done) override;

	/** Forgets the previous frame, for example when a stream ends. */
	void Reset() override;

private:
	struct Impl;
	Impl *impl;
};
