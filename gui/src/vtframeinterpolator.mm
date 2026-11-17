#include "vtframeinterpolator.h"

#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

extern "C" {
#include <libavutil/pixfmt.h>
}

// VTLowLatencyFrameInterpolation needs macOS 26. The app targets macOS 12, so
// every use sits behind @available and VideoToolbox symbols are weak-linked.

namespace {

void release_pixel_buffer(void *opaque, uint8_t *)
{
	CVPixelBufferRef pb = static_cast<CVPixelBufferRef>(opaque);
	CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
	CVPixelBufferRelease(pb);
}

// Wraps an NV12 pixel buffer as a software AVFrame without copying.
// Takes over one reference of `pb`.
AVFrame *wrap_nv12(CVPixelBufferRef pb, const AVFrame *props)
{
	if (CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
		CVPixelBufferRelease(pb);
		return nullptr;
	}
	AVFrame *out = av_frame_alloc();
	AVBufferRef *buf = out ? av_buffer_create(static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pb, 0)),
		CVPixelBufferGetDataSize(pb), release_pixel_buffer, pb, AV_BUFFER_FLAG_READONLY) : nullptr;
	if (!buf) {
		av_frame_free(&out);
		release_pixel_buffer(pb, nullptr);
		return nullptr;
	}
	av_frame_copy_props(out, props);
	out->format = AV_PIX_FMT_NV12;
	out->width = props->width;
	out->height = props->height;
	out->buf[0] = buf;
	for (int plane = 0; plane < 2; plane++) {
		out->data[plane] = static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pb, plane));
		out->linesize[plane] = int(CVPixelBufferGetBytesPerRowOfPlane(pb, plane));
	}
	out->crop_top = out->crop_bottom = out->crop_left = out->crop_right = 0;
	return out;
}

} // namespace

struct VTFrameInterpolator::Impl
{
	id processor = nil; // VTFrameProcessor
	CVPixelBufferPoolRef pool = nullptr;
	CVPixelBufferRef previous = nullptr;
	AVFrame *previous_props = nullptr;
	int width = 0, height = 0;
	int multiplier = 0;
	OSType format = 0;
	int64_t sequence = 0;
	bool failed = false;
	// Interpolations started but not finished; see Submit().
	std::shared_ptr<std::atomic<int>> in_flight = std::make_shared<std::atomic<int>>(0);

	void EndSession()
	{
		if (processor) {
			if (@available(macOS 26.0, *))
				[(VTFrameProcessor *)processor endSession];
		}
		processor = nil;
		if (pool) {
			CVPixelBufferPoolRelease(pool);
			pool = nullptr;
		}
		width = height = 0;
		multiplier = 0;
	}

	void ForgetPrevious()
	{
		if (previous) {
			CVPixelBufferRelease(previous);
			previous = nullptr;
		}
		av_frame_free(&previous_props);
	}

	bool StartSession(int w, int h, int mult)
	{
		EndSession();
		if (@available(macOS 26.0, *)) {
			// One interpolated frame gives the 0.5 point; two give 0.25, 0.5 and 0.75. Other
			// points (thirds) are accepted with two but cost more.
			const NSInteger count = mult <= 2 ? 1 : 2;
			VTLowLatencyFrameInterpolationConfiguration *config =
				[[VTLowLatencyFrameInterpolationConfiguration alloc] initWithFrameWidth:w frameHeight:h numberOfInterpolatedFrames:count];
			if (!config)
				return false;
			VTFrameProcessor *p = [VTFrameProcessor new];
			NSError *err = nil;
			if (![p startSessionWithConfiguration:config error:&err]) {
				NSLog(@"VTFrameInterpolator: start session failed: %@", err);
				return false;
			}
			if (CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr,
					(__bridge CFDictionaryRef)config.destinationPixelBufferAttributes, &pool) != kCVReturnSuccess) {
				[p endSession];
				return false;
			}
			processor = p;
			width = w;
			height = h;
			multiplier = mult;
			return true;
		}
		return false;
	}

	~Impl()
	{
		ForgetPrevious();
		EndSession();
	}
};

VTFrameInterpolator::VTFrameInterpolator() : impl(new Impl) {}

VTFrameInterpolator::~VTFrameInterpolator()
{
	delete impl;
}

bool VTFrameInterpolator::IsSupported()
{
	if (@available(macOS 26.0, *))
		return VTLowLatencyFrameInterpolationConfiguration.isSupported;
	return false;
}

void VTFrameInterpolator::Reset()
{
	impl->ForgetPrevious();
}

bool VTFrameInterpolator::Submit(const AVFrame *src, bool discontinuity, int multiplier, Callback done)
{
	if (impl->failed || !src || src->format != AV_PIX_FMT_VIDEOTOOLBOX || !src->data[3])
		return false;

	CVPixelBufferRef current = reinterpret_cast<CVPixelBufferRef>(src->data[3]);
	const OSType format = CVPixelBufferGetPixelFormatType(current);
	const int w = int(CVPixelBufferGetWidth(current));
	const int h = int(CVPixelBufferGetHeight(current));
	if (format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
		impl->ForgetPrevious();
		return false;
	}

	multiplier = std::clamp(multiplier, 2, MaxMultiplier());
	const bool restart = discontinuity || w != impl->width || h != impl->height || format != impl->format
		|| multiplier != impl->multiplier;
	if (w != impl->width || h != impl->height || multiplier != impl->multiplier) {
		if (!impl->StartSession(w, h, multiplier)) {
			NSLog(@"VTFrameInterpolator: cannot interpolate %dx%d, disabling", w, h);
			impl->failed = true;
			impl->ForgetPrevious();
			return false;
		}
	}
	impl->format = format;

	// Keep the new frame as the next "previous" frame either way.
	CVPixelBufferRef previous = impl->previous;
	AVFrame *previous_props = impl->previous_props;
	impl->previous = CVPixelBufferRetain(current);
	impl->previous_props = av_frame_alloc();
	if (impl->previous_props)
		av_frame_copy_props(impl->previous_props, src);
	const int64_t seq = ++impl->sequence;

	// Refuse new work while two interpolations are still running, so a busy
	// GPU drops interpolation instead of queuing work and starving the renderer.
	if (impl->in_flight->load() >= 2) {
		if (previous)
			CVPixelBufferRelease(previous);
		av_frame_free(&previous_props);
		return false;
	}

	if (restart || !previous) {
		if (previous)
			CVPixelBufferRelease(previous);
		av_frame_free(&previous_props);
		return false;
	}
	av_frame_free(&previous_props);

	if (@available(macOS 26.0, *)) {
		const int count = multiplier - 1;
		std::vector<CVPixelBufferRef> outs;
		NSMutableArray<VTFrameProcessorFrame *> *dst_frames = [NSMutableArray arrayWithCapacity:count];
		NSMutableArray<NSNumber *> *phases = [NSMutableArray arrayWithCapacity:count];
		bool ok = true;
		for (int k = 1; k <= count && ok; k++) {
			CVPixelBufferRef out_pb = nullptr;
			if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, impl->pool, &out_pb) != kCVReturnSuccess) {
				ok = false;
				break;
			}
			outs.push_back(out_pb);
			VTFrameProcessorFrame *f = [[VTFrameProcessorFrame alloc] initWithBuffer:out_pb
				presentationTimeStamp:CMTimeMake((seq - 1) * multiplier + k, int32_t(120 * multiplier))];
			if (!f) {
				ok = false;
				break;
			}
			[dst_frames addObject:f];
			[phases addObject:@(float(k) / float(multiplier))];
		}
		// The VTFrameProcessorFrame objects retain the pixel buffers until processing ends.
		VTFrameProcessorFrame *prev_frame = ok ? [[VTFrameProcessorFrame alloc] initWithBuffer:previous presentationTimeStamp:CMTimeMake(seq - 1, 120)] : nil;
		VTFrameProcessorFrame *cur_frame = ok ? [[VTFrameProcessorFrame alloc] initWithBuffer:current presentationTimeStamp:CMTimeMake(seq, 120)] : nil;
		CVPixelBufferRelease(previous);
		VTLowLatencyFrameInterpolationParameters *params = (prev_frame && cur_frame)
			? [[VTLowLatencyFrameInterpolationParameters alloc] initWithSourceFrame:cur_frame previousFrame:prev_frame
															  interpolationPhase:phases destinationFrames:dst_frames]
			: nil;
		if (!params) {
			for (CVPixelBufferRef pb : outs)
				CVPixelBufferRelease(pb);
			return false;
		}
		AVFrame *props = av_frame_alloc();
		if (props) {
			av_frame_copy_props(props, src);
			props->width = src->width;
			props->height = src->height;
		}
		auto callback = std::make_shared<Callback>(std::move(done));
		auto in_flight = impl->in_flight;
		in_flight->fetch_add(1);
		[(VTFrameProcessor *)impl->processor processWithParameters:params
			completionHandler:^(id<VTFrameProcessorParameters>, NSError *err) {
				in_flight->fetch_sub(1);
				std::vector<AVFrame *> frames;
				if (err)
					NSLog(@"VTFrameInterpolator: interpolation failed: %@", err);
				for (CVPixelBufferRef pb : outs) {
					if (!err && props) {
						if (AVFrame *f = wrap_nv12(CVPixelBufferRetain(pb), props))
							frames.push_back(f);
					}
					CVPixelBufferRelease(pb);
				}
				AVFrame *p = props;
				av_frame_free(&p);
				(*callback)(std::move(frames));
			}];
		return true;
	}
	CVPixelBufferRelease(previous);
	return false;
}
