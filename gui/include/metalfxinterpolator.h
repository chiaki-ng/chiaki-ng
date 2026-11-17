#pragma once

#include "frameinterpolator.h"

/**
 * Frame interpolation with MetalFX (macOS 26+).
 *
 * Each pair of frames goes through one Metal command buffer: YCbCr to RGB,
 * a 640-pixel-wide copy for VTOpticalFlow, optical flow (encoded into the same
 * command buffer), flow to motion vectors, the MetalFX frame interpolator, and
 * back to NV12. Nothing blocks the calling thread.
 *
 * MetalFX expects depth from a game engine; video has none, so a constant
 * depth is used and the motion comes from optical flow.
 *
 * Supports 8-bit video-range 4:2:0 input.
 */
class MetalFXFrameInterpolator : public FrameInterpolator
{
public:
	MetalFXFrameInterpolator();
	~MetalFXFrameInterpolator() override;
	MetalFXFrameInterpolator(const MetalFXFrameInterpolator &) = delete;
	MetalFXFrameInterpolator &operator=(const MetalFXFrameInterpolator &) = delete;

	/** True on macOS 26+ when MetalFX interpolation and VTOpticalFlow are both available. */
	static bool IsSupported();

	/** MetalFX only makes the frame halfway between two frames. */
	int MaxMultiplier() const override { return 2; }
	bool Submit(const AVFrame *src, bool discontinuity, int multiplier, Callback done) override;
	void Reset() override;

private:
	struct Impl;
	Impl *impl;
};
