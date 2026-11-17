#pragma once

#include <functional>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/frame.h>
#ifdef __cplusplus
}
#endif

/**
 * Makes the frame halfway between consecutive decoded frames.
 *
 * Submit() remembers `src` as the newest frame and, when possible, starts an
 * asynchronous interpolation between the previous frame and `src`. For a
 * multiplier M it makes M - 1 frames at 1/M, 2/M, ... of the way from the
 * previous frame to `src`. It returns true when `done` will be called exactly
 * once, with those frames in time order as software NV12 AVFrames (empty on
 * failure). It returns false, without calling `done`, for the first frame,
 * after `discontinuity`, after a size, format or multiplier change, or for
 * unsupported input.
 *
 * Submit() and Reset() must be called from one thread. `done` runs on another
 * thread.
 */
class FrameInterpolator
{
public:
	using Callback = std::function<void(std::vector<AVFrame *> frames)>;
	virtual ~FrameInterpolator() = default;
	/** Largest supported frame-rate multiplier. */
	virtual int MaxMultiplier() const = 0;
	/** `multiplier` must be between 2 and MaxMultiplier(). */
	virtual bool Submit(const AVFrame *src, bool discontinuity, int multiplier, Callback done) = 0;
	/** Forgets the previous frame, for example when a stream ends. */
	virtual void Reset() = 0;
};
