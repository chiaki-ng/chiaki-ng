#include "metalfxinterpolator.h"

#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

extern "C" {
#include <libavutil/pixfmt.h>
}

// MetalFX frame interpolation needs macOS 26 and VTOpticalFlow macOS 15.4. The app
// targets macOS 12, so every use sits behind @available and both are weak-linked.

namespace {

// Width of the frames given to VTOpticalFlow. Its output is 1/4 of this anyway, and in
// tests 640 kept the interpolation quality of full 1080p input at half the cost.
constexpr int kFlowWidth = 640;

NSString *const kShaders = @R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Matrix { float kr; float kb; };

// Video-range BT.601/709/2020 YCbCr -> non-linear RGB
kernel void nv12_to_rgb(texture2d<float, access::sample> luma [[texture(0)]],
                        texture2d<float, access::sample> chroma [[texture(1)]],
                        texture2d<float, access::write> out [[texture(2)]],
                        constant Matrix &m [[buffer(0)]],
                        uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= out.get_width() || gid.y >= out.get_height())
        return;
    constexpr sampler s(coord::normalized, filter::linear, address::clamp_to_edge);
    float2 uv = (float2(gid) + 0.5) / float2(out.get_width(), out.get_height());
    float y = (luma.sample(s, uv).r - 16.0 / 255.0) * (255.0 / 219.0);
    float2 c = (chroma.sample(s, uv).rg - 128.0 / 255.0) * (255.0 / 224.0);
    float kg = 1.0 - m.kr - m.kb;
    float r = y + 2.0 * (1.0 - m.kr) * c.y;
    float b = y + 2.0 * (1.0 - m.kb) * c.x;
    float g = (y - m.kr * r - m.kb * b) / kg;
    out.write(float4(clamp(float3(r, g, b), 0.0, 1.0), 1.0), gid);
}

kernel void downscale(texture2d<float, access::sample> src [[texture(0)]],
                      texture2d<float, access::write> dst [[texture(1)]],
                      uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= dst.get_width() || gid.y >= dst.get_height())
        return;
    constexpr sampler s(coord::normalized, filter::linear, address::clamp_to_edge);
    dst.write(src.sample(s, (float2(gid) + 0.5) / float2(dst.get_width(), dst.get_height())), gid);
}

// Backward optical flow (in flow-image pixels) -> MetalFX motion vectors (in output
// pixels), pointing from the current frame to the previous one.
kernel void flow_to_motion(texture2d<float, access::sample> flow [[texture(0)]],
                           texture2d<float, access::write> motion [[texture(1)]],
                           constant float2 &scale [[buffer(0)]],
                           uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= motion.get_width() || gid.y >= motion.get_height())
        return;
    constexpr sampler s(coord::normalized, filter::linear, address::clamp_to_edge);
    float2 uv = (float2(gid) + 0.5) / float2(motion.get_width(), motion.get_height());
    motion.write(float4(flow.sample(s, uv).rg * scale, 0.0, 0.0), gid);
}

// Non-linear RGB -> video-range NV12. One thread writes one chroma sample and the
// four luma samples it covers.
kernel void rgb_to_nv12(texture2d<float, access::read> rgb [[texture(0)]],
                        texture2d<float, access::write> luma [[texture(1)]],
                        texture2d<float, access::write> chroma [[texture(2)]],
                        constant Matrix &m [[buffer(0)]],
                        uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= chroma.get_width() || gid.y >= chroma.get_height())
        return;
    float kg = 1.0 - m.kr - m.kb;
    float2 csum = 0.0;
    for (uint dy = 0; dy < 2; dy++) {
        for (uint dx = 0; dx < 2; dx++) {
            uint2 p = min(gid * 2 + uint2(dx, dy), uint2(rgb.get_width() - 1, rgb.get_height() - 1));
            float3 c = rgb.read(p).rgb;
            float y = m.kr * c.r + kg * c.g + m.kb * c.b;
            luma.write(float4(16.0 / 255.0 + y * (219.0 / 255.0)), p);
            csum += float2((c.b - y) / (2.0 * (1.0 - m.kb)), (c.r - y) / (2.0 * (1.0 - m.kr)));
        }
    }
    chroma.write(float4(128.0 / 255.0 + (csum * 0.25) * (224.0 / 255.0), 0.0, 0.0), gid);
}
)MSL";

struct Matrix {
	float kr;
	float kb;
};

Matrix matrix_for(AVColorSpace space)
{
	if (space == AVCOL_SPC_BT2020_NCL || space == AVCOL_SPC_BT2020_CL)
		return {0.2627f, 0.0593f};
	if (space == AVCOL_SPC_BT470BG || space == AVCOL_SPC_SMPTE170M)
		return {0.299f, 0.114f};
	return {0.2126f, 0.0722f}; // BT.709, also for unknown
}

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
	out->color_range = AVCOL_RANGE_MPEG;
	out->buf[0] = buf;
	for (int plane = 0; plane < 2; plane++) {
		out->data[plane] = static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pb, plane));
		out->linesize[plane] = int(CVPixelBufferGetBytesPerRowOfPlane(pb, plane));
	}
	out->crop_top = out->crop_bottom = out->crop_left = out->crop_right = 0;
	return out;
}

MTLSize grid_for(id<MTLComputePipelineState> pso, NSUInteger w, NSUInteger h, MTLSize *tg)
{
	NSUInteger tw = pso.threadExecutionWidth;
	NSUInteger th = std::max<NSUInteger>(1, pso.maxTotalThreadsPerThreadgroup / tw);
	*tg = MTLSizeMake(tw, th, 1);
	return MTLSizeMake((w + tw - 1) / tw, (h + th - 1) / th, 1);
}

void dispatch(id<MTLComputeCommandEncoder> enc, id<MTLComputePipelineState> pso, NSUInteger w, NSUInteger h)
{
	MTLSize tg;
	MTLSize grid = grid_for(pso, w, h, &tg);
	[enc setComputePipelineState:pso];
	[enc dispatchThreadgroups:grid threadsPerThreadgroup:tg];
}

} // namespace

struct MetalFXFrameInterpolator::Impl
{
	id<MTLDevice> device = nil;
	id<MTLCommandQueue> queue = nil;
	id<MTLComputePipelineState> to_rgb = nil, down = nil, to_motion = nil, to_nv12 = nil;
	CVMetalTextureCacheRef cache = nullptr;

	// Per-size state
	int width = 0, height = 0, flow_w = 0, flow_h = 0;
	id interpolator = nil;  // id<MTLFXFrameInterpolator>
	id flow = nil;          // VTFrameProcessor
	id<MTLTexture> rgb[2] = {nil, nil};
	id<MTLTexture> motion = nil, depth = nil, interpolated = nil;
	CVPixelBufferPoolRef flow_in_pool = nullptr, flow_out_pool = nullptr, out_pool = nullptr;
	CVPixelBufferRef flow_in[2] = {nullptr, nullptr};

	int slot = 0;           // slot of the newest frame
	bool have_previous = false;
	bool reset_history = true;
	int64_t sequence = 0;
	bool broken = false;
	// Command buffers submitted but not finished. New work is refused while one is still
	// running (at most two in flight), so a busy GPU drops interpolation instead of queuing work without bound
	// and starving the renderer.
	std::shared_ptr<std::atomic<int>> in_flight = std::make_shared<std::atomic<int>>(0);

	bool Init()
	{
		device = MTLCreateSystemDefaultDevice();
		if (!device)
			return false;
		queue = [device newCommandQueue];
		NSError *err = nil;
		id<MTLLibrary> lib = [device newLibraryWithSource:kShaders options:nil error:&err];
		if (!lib) {
			NSLog(@"MetalFXFrameInterpolator: shader compile failed: %@", err);
			return false;
		}
		to_rgb = [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"nv12_to_rgb"] error:&err];
		down = [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"downscale"] error:&err];
		to_motion = [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"flow_to_motion"] error:&err];
		to_nv12 = [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rgb_to_nv12"] error:&err];
		if (!to_rgb || !down || !to_motion || !to_nv12)
			return false;
		return CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &cache) == kCVReturnSuccess;
	}

	void ReleaseConfig()
	{
		if (flow) {
			if (@available(macOS 15.4, *))
				[(VTFrameProcessor *)flow endSession];
		}
		flow = nil;
		interpolator = nil;
		rgb[0] = rgb[1] = nil;
		motion = depth = interpolated = nil;
		for (auto &pb : flow_in) {
			if (pb)
				CVPixelBufferRelease(pb);
			pb = nullptr;
		}
		for (CVPixelBufferPoolRef *pool : {&flow_in_pool, &flow_out_pool, &out_pool}) {
			if (*pool)
				CVPixelBufferPoolRelease(*pool);
			*pool = nullptr;
		}
		width = height = 0;
		have_previous = false;
		reset_history = true;
	}

	~Impl()
	{
		ReleaseConfig();
		if (cache)
			CFRelease(cache);
	}

	id<MTLTexture> NewTexture(MTLPixelFormat fmt, int w, int h, MTLTextureUsage usage, MTLStorageMode mode)
	{
		MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:NO];
		td.storageMode = mode;
		td.usage = usage;
		return [device newTextureWithDescriptor:td];
	}

	id<MTLTexture> Wrap(CVPixelBufferRef pb, size_t plane, MTLPixelFormat fmt, std::vector<CVMetalTextureRef> &hold)
	{
		const bool planar = CVPixelBufferIsPlanar(pb);
		const size_t w = planar ? CVPixelBufferGetWidthOfPlane(pb, plane) : CVPixelBufferGetWidth(pb);
		const size_t h = planar ? CVPixelBufferGetHeightOfPlane(pb, plane) : CVPixelBufferGetHeight(pb);
		CVMetalTextureRef ref = nullptr;
		if (CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, cache, pb, nullptr, fmt, w, h, plane, &ref) != kCVReturnSuccess)
			return nil;
		hold.push_back(ref);
		return CVMetalTextureGetTexture(ref);
	}

	bool Configure(int w, int h)
	{
		ReleaseConfig();
		if (@available(macOS 26.0, *)) {
			flow_w = std::min(kFlowWidth, w) & ~1;
			flow_h = int(int64_t(flow_w) * h / w) & ~1;

			VTOpticalFlowConfiguration *fc = [[VTOpticalFlowConfiguration alloc] initWithFrameWidth:flow_w frameHeight:flow_h
				qualityPrioritization:VTOpticalFlowConfigurationQualityPrioritizationNormal
				revision:VTOpticalFlowConfiguration.defaultRevision];
			VTFrameProcessor *proc = [VTFrameProcessor new];
			NSError *err = nil;
			if (!fc || ![proc startSessionWithConfiguration:fc error:&err]) {
				NSLog(@"MetalFXFrameInterpolator: optical flow session failed: %@", err);
				return false;
			}
			flow = proc;
			if (CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr, (__bridge CFDictionaryRef)fc.sourcePixelBufferAttributes, &flow_in_pool) != kCVReturnSuccess
				|| CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr, (__bridge CFDictionaryRef)fc.destinationPixelBufferAttributes, &flow_out_pool) != kCVReturnSuccess)
				return false;

			MTLFXFrameInterpolatorDescriptor *desc = [MTLFXFrameInterpolatorDescriptor new];
			desc.inputWidth = w;
			desc.inputHeight = h;
			desc.outputWidth = w;
			desc.outputHeight = h;
			desc.colorTextureFormat = MTLPixelFormatRGBA16Float;
			desc.outputTextureFormat = MTLPixelFormatRGBA16Float;
			desc.depthTextureFormat = MTLPixelFormatR32Float;
			desc.motionTextureFormat = MTLPixelFormatRG16Float;
			if ([desc respondsToSelector:@selector(setRequiresPrevColorTexture:)])
				[desc setValue:@YES forKey:@"requiresPrevColorTexture"];
			id<MTLFXFrameInterpolator> fi = [desc newFrameInterpolatorWithDevice:device];
			if (!fi)
				return false;
			interpolator = fi;

			const MTLTextureUsage color_usage = fi.colorTextureUsage | MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
			rgb[0] = NewTexture(MTLPixelFormatRGBA16Float, w, h, color_usage, MTLStorageModePrivate);
			rgb[1] = NewTexture(MTLPixelFormatRGBA16Float, w, h, color_usage, MTLStorageModePrivate);
			motion = NewTexture(MTLPixelFormatRG16Float, w, h, fi.motionTextureUsage | MTLTextureUsageShaderWrite, MTLStorageModePrivate);
			interpolated = NewTexture(MTLPixelFormatRGBA16Float, w, h, fi.outputTextureUsage | MTLTextureUsageShaderRead, MTLStorageModePrivate);
			depth = NewTexture(MTLPixelFormatR32Float, w, h, fi.depthTextureUsage, MTLStorageModeShared);
			if (!rgb[0] || !rgb[1] || !motion || !interpolated || !depth)
				return false;
			std::vector<float> constant_depth(size_t(w) * h, 0.5f);
			[depth replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:constant_depth.data() bytesPerRow:size_t(w) * 4];

			NSDictionary *out_attrs = @{
				(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
				(id)kCVPixelBufferWidthKey: @(w),
				(id)kCVPixelBufferHeightKey: @(h),
				(id)kCVPixelBufferMetalCompatibilityKey: @YES,
				(id)kCVPixelBufferIOSurfacePropertiesKey: @{},
			};
			if (CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr, (__bridge CFDictionaryRef)out_attrs, &out_pool) != kCVReturnSuccess)
				return false;

			width = w;
			height = h;
			return true;
		}
		return false;
	}
};

MetalFXFrameInterpolator::MetalFXFrameInterpolator() : impl(new Impl)
{
	if (!IsSupported() || !impl->Init())
		impl->broken = true;
}

MetalFXFrameInterpolator::~MetalFXFrameInterpolator()
{
	delete impl;
}

bool MetalFXFrameInterpolator::IsSupported()
{
	if (@available(macOS 26.0, *)) {
		id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
		return dev && [MTLFXFrameInterpolatorDescriptor supportsDevice:dev] && VTOpticalFlowConfiguration.isSupported;
	}
	return false;
}

void MetalFXFrameInterpolator::Reset()
{
	impl->have_previous = false;
	impl->reset_history = true;
}

bool MetalFXFrameInterpolator::Submit(const AVFrame *src, bool discontinuity, int multiplier, Callback done)
{
	(void)multiplier; // always 2; see MaxMultiplier()
	if (impl->broken || !src || src->format != AV_PIX_FMT_VIDEOTOOLBOX || !src->data[3])
		return false;
	CVPixelBufferRef current = reinterpret_cast<CVPixelBufferRef>(src->data[3]);
	if (CVPixelBufferGetPixelFormatType(current) != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
		Reset();
		return false;
	}
	const int w = int(CVPixelBufferGetWidth(current)), h = int(CVPixelBufferGetHeight(current));
	if ((w != impl->width || h != impl->height) && !impl->Configure(w, h)) {
		NSLog(@"MetalFXFrameInterpolator: cannot interpolate %dx%d, disabling", w, h);
		impl->ReleaseConfig();
		impl->broken = true;
		return false;
	}
	if (discontinuity)
		Reset();
	if (impl->in_flight->load() >= 2) {
		// GPU still busy with two earlier frames: skip, and restart history next time.
		Reset();
		return false;
	}

	if (@available(macOS 26.0, *)) {
		@autoreleasepool {
			const int cur = impl->have_previous ? 1 - impl->slot : impl->slot;
			const int prev = 1 - cur;
			const bool interpolate = impl->have_previous;
			const Matrix m = matrix_for(src->colorspace);

			// GPU resources that must outlive this call; released when the GPU is done.
			auto hold = std::make_shared<std::vector<CVMetalTextureRef>>();
			std::vector<CVPixelBufferRef> retained;

			id<MTLTexture> y_tex = impl->Wrap(current, 0, MTLPixelFormatR8Unorm, *hold);
			id<MTLTexture> c_tex = impl->Wrap(current, 1, MTLPixelFormatRG8Unorm, *hold);
			CVPixelBufferRef flow_in = nullptr;
			CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, impl->flow_in_pool, &flow_in);
			id<MTLTexture> flow_in_tex = flow_in ? impl->Wrap(flow_in, 0, MTLPixelFormatRGBA16Float, *hold) : nil;
			if (!y_tex || !c_tex || !flow_in_tex) {
				for (CVMetalTextureRef r : *hold)
					CFRelease(r);
				if (flow_in)
					CVPixelBufferRelease(flow_in);
				return false;
			}
			retained.push_back(CVPixelBufferRetain(current));

			id<MTLCommandBuffer> cb = [impl->queue commandBuffer];
			id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
			[enc setTexture:y_tex atIndex:0];
			[enc setTexture:c_tex atIndex:1];
			[enc setTexture:impl->rgb[cur] atIndex:2];
			[enc setBytes:&m length:sizeof(m) atIndex:0];
			dispatch(enc, impl->to_rgb, w, h);
			[enc setTexture:impl->rgb[cur] atIndex:0];
			[enc setTexture:flow_in_tex atIndex:1];
			dispatch(enc, impl->down, impl->flow_w, impl->flow_h);
			[enc endEncoding];

			CVPixelBufferRef out_pb = nullptr;
			if (interpolate) {
				CVPixelBufferRef fwd = nullptr, bwd = nullptr;
				CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, impl->flow_out_pool, &fwd);
				CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, impl->flow_out_pool, &bwd);
				CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, impl->out_pool, &out_pb);
				id<MTLTexture> bwd_tex = bwd ? impl->Wrap(bwd, 0, MTLPixelFormatRG16Float, *hold) : nil;
				id<MTLTexture> out_y = out_pb ? impl->Wrap(out_pb, 0, MTLPixelFormatR8Unorm, *hold) : nil;
				id<MTLTexture> out_c = out_pb ? impl->Wrap(out_pb, 1, MTLPixelFormatRG8Unorm, *hold) : nil;
				VTFrameProcessorFrame *prev_frame = [[VTFrameProcessorFrame alloc] initWithBuffer:impl->flow_in[prev] presentationTimeStamp:CMTimeMake(impl->sequence, 120)];
				VTFrameProcessorFrame *cur_frame = [[VTFrameProcessorFrame alloc] initWithBuffer:flow_in presentationTimeStamp:CMTimeMake(impl->sequence + 1, 120)];
				VTFrameProcessorOpticalFlow *of = (fwd && bwd) ? [[VTFrameProcessorOpticalFlow alloc] initWithForwardFlow:fwd backwardFlow:bwd] : nil;
				VTOpticalFlowParameters *op = (prev_frame && cur_frame && of) ? [[VTOpticalFlowParameters alloc] initWithSourceFrame:prev_frame nextFrame:cur_frame
					submissionMode:(impl->reset_history ? VTOpticalFlowParametersSubmissionModeRandom : VTOpticalFlowParametersSubmissionModeSequential)
					destinationOpticalFlow:of] : nil;
				if (fwd) retained.push_back(fwd);
				if (bwd) retained.push_back(bwd);
				if (bwd_tex && out_y && out_c && op) {
					[(VTFrameProcessor *)impl->flow processWithCommandBuffer:cb parameters:op];

					const simd_float2 scale = {float(w) / float(CVPixelBufferGetWidth(bwd)), float(h) / float(CVPixelBufferGetHeight(bwd))};
					enc = [cb computeCommandEncoder];
					[enc setTexture:bwd_tex atIndex:0];
					[enc setTexture:impl->motion atIndex:1];
					[enc setBytes:&scale length:sizeof(scale) atIndex:0];
					dispatch(enc, impl->to_motion, w, h);
					[enc endEncoding];

					id<MTLFXFrameInterpolator> fi = impl->interpolator;
					fi.colorTexture = impl->rgb[cur];
					fi.prevColorTexture = impl->rgb[prev];
					fi.depthTexture = impl->depth;
					fi.motionTexture = impl->motion;
					fi.outputTexture = impl->interpolated;
					fi.motionVectorScaleX = 1.0f;
					fi.motionVectorScaleY = 1.0f;
					fi.deltaTime = 1.0f / 60.0f;
					fi.nearPlane = 0.1f;
					fi.farPlane = 100.0f;
					fi.fieldOfView = 60.0f;
					fi.aspectRatio = float(w) / float(h);
					fi.jitterOffsetX = 0.0f;
					fi.jitterOffsetY = 0.0f;
					fi.shouldResetHistory = impl->reset_history;
					[fi encodeToCommandBuffer:cb];

					enc = [cb computeCommandEncoder];
					[enc setTexture:impl->interpolated atIndex:0];
					[enc setTexture:out_y atIndex:1];
					[enc setTexture:out_c atIndex:2];
					[enc setBytes:&m length:sizeof(m) atIndex:0];
					dispatch(enc, impl->to_nv12, (w + 1) / 2, (h + 1) / 2);
					[enc endEncoding];
					impl->reset_history = false;
				} else if (out_pb) {
					CVPixelBufferRelease(out_pb);
					out_pb = nullptr;
				}
			}

			AVFrame *props = nullptr;
			if (out_pb) {
				props = av_frame_alloc();
				if (props) {
					av_frame_copy_props(props, src);
					props->width = src->width;
					props->height = src->height;
				}
			}
			auto callback = out_pb ? std::make_shared<Callback>(std::move(done)) : nullptr;
			auto in_flight = impl->in_flight;
			in_flight->fetch_add(1);
			[cb addCompletedHandler:^(id<MTLCommandBuffer> finished) {
				in_flight->fetch_sub(1);
				for (CVMetalTextureRef r : *hold)
					CFRelease(r);
				for (CVPixelBufferRef pb : retained)
					CVPixelBufferRelease(pb);
				if (!callback)
					return;
				AVFrame *mid = nullptr;
				if (finished.status == MTLCommandBufferStatusCompleted && props)
					mid = wrap_nv12(CVPixelBufferRetain(out_pb), props);
				else if (finished.status != MTLCommandBufferStatusCompleted)
					NSLog(@"MetalFXFrameInterpolator: command buffer failed: %@", finished.error);
				CVPixelBufferRelease(out_pb);
				AVFrame *p = props;
				av_frame_free(&p);
				std::vector<AVFrame *> frames;
				if (mid)
					frames.push_back(mid);
				(*callback)(std::move(frames));
			}];
			[cb commit];

			// The new frame's flow input becomes the previous one for the next call.
			if (impl->flow_in[cur])
				CVPixelBufferRelease(impl->flow_in[cur]);
			impl->flow_in[cur] = flow_in;
			impl->slot = cur;
			impl->have_previous = true;
			impl->sequence++;
			return out_pb != nullptr;
		}
	}
	return false;
}
