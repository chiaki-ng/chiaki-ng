#include "metalfxupscaler.h"

#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurfaceRef.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavutil/pixdesc.h>
}

// MetalFX needs macOS 13. The app targets macOS 12, so every MetalFX call
// sits behind an @available check and the framework is weak-linked.

namespace {

// Converts biplanar 4:2:0 YCbCr to non-linear RGB. The transfer function
// (gamma or PQ) stays as it is; only the matrix and the range change.
static NSString *const kShaderSource = @R"MSL(
#include <metal_stdlib>
using namespace metal;

struct YuvParams {
    float3x3 matrix;   // YCbCr (offset removed) -> RGB
    float3 offset;     // subtract from (Y, Cb, Cr) before the matrix
    float3 scale;      // multiply (Y, Cb, Cr) after the offset
};

kernel void yuv_to_rgb(texture2d<float, access::sample> luma [[texture(0)]],
                       texture2d<float, access::sample> chroma [[texture(1)]],
                       texture2d<float, access::write> out [[texture(2)]],
                       constant YuvParams &p [[buffer(0)]],
                       uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= out.get_width() || gid.y >= out.get_height())
        return;
    constexpr sampler s(coord::normalized, filter::linear, address::clamp_to_edge);
    float2 uv = (float2(gid) + 0.5) / float2(out.get_width(), out.get_height());
    float y = luma.sample(s, uv).r;
    float2 c = chroma.sample(s, uv).rg;
    float3 ycc = (float3(y, c.x, c.y) - p.offset) * p.scale;
    float3 rgb = clamp(p.matrix * ycc, 0.0, 1.0);
    out.write(float4(rgb, 1.0), gid);
}

kernel void copy_clamped(texture2d<float, access::read> src [[texture(0)]],
                         texture2d<float, access::write> dst [[texture(1)]],
                         uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= dst.get_width() || gid.y >= dst.get_height())
        return;
    dst.write(clamp(src.read(gid), 0.0, 1.0), gid);
}
)MSL";

struct YuvParams {
	simd_float3x3 matrix;
	simd_float3 offset;
	simd_float3 scale;
};

static YuvParams make_yuv_params(AVColorSpace space, AVColorRange range, bool ten_bit)
{
	// Kr/Kb per matrix. Unknown matrices use BT.709, which PlayStation streams use.
	float kr = 0.2126f, kb = 0.0722f;
	if (space == AVCOL_SPC_BT2020_NCL || space == AVCOL_SPC_BT2020_CL) {
		kr = 0.2627f; kb = 0.0593f;
	} else if (space == AVCOL_SPC_BT470BG || space == AVCOL_SPC_SMPTE170M) {
		kr = 0.299f; kb = 0.114f;
	}
	const float kg = 1.0f - kr - kb;

	YuvParams p = {};
	// Columns: contribution of Y, Cb, Cr.
	p.matrix = simd_matrix(
		simd_make_float3(1.0f, 1.0f, 1.0f),
		simd_make_float3(0.0f, -2.0f * kb * (1.0f - kb) / kg, 2.0f * (1.0f - kb)),
		simd_make_float3(2.0f * (1.0f - kr), -2.0f * kr * (1.0f - kr) / kg, 0.0f));

	// 10-bit samples sit in the top bits of 16-bit words. R16Unorm reads them as
	// code * 64 / 65535, so rescale by 65535 / 65472 to get code / 1023.
	const float container = ten_bit ? 65535.0f / 65472.0f : 1.0f;
	const float max_code = ten_bit ? 1023.0f : 255.0f;

	if (range == AVCOL_RANGE_JPEG) {
		const float half = (ten_bit ? 512.0f : 128.0f) / max_code;
		p.offset = simd_make_float3(0.0f, half, half) / container;
		p.scale = simd_make_float3(1.0f, 1.0f, 1.0f) * container;
	} else {
		const float y_off = (ten_bit ? 64.0f : 16.0f) / max_code;
		const float c_off = (ten_bit ? 512.0f : 128.0f) / max_code;
		const float y_rng = (ten_bit ? 876.0f : 219.0f) / max_code;
		const float c_rng = (ten_bit ? 896.0f : 224.0f) / max_code;
		p.offset = simd_make_float3(y_off, c_off, c_off) / container;
		p.scale = simd_make_float3(1.0f / y_rng, 1.0f / c_rng, 1.0f / c_rng) * container;
	}
	return p;
}

// Stored in AVFrame::buf[1] of every output frame. It marks the frame as ours
// and holds an extra reference to the IOSurface for zero-copy import.
struct IOSurfaceMarker {
	uint64_t magic;
	IOSurfaceRef surface;
	size_t alloc_size;
	CFTypeRef command_buffer; // id<MTLCommandBuffer> that writes the surface; retained
};
static constexpr uint64_t kIOSurfaceMarkerMagic = 0x4d46585355524631ull; // "MFXSURF1"

static void release_marker(void *, uint8_t *data)
{
	auto *m = reinterpret_cast<IOSurfaceMarker *>(data);
	if (m->surface)
		CFRelease(m->surface);
	if (m->command_buffer)
		CFRelease(m->command_buffer);
	av_free(m);
}

static void release_output(void *opaque, uint8_t *)
{
	CVPixelBufferRef pb = static_cast<CVPixelBufferRef>(opaque);
	CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
	CVPixelBufferRelease(pb);
}

static MTLSize grid_for(id<MTLComputePipelineState> pso, NSUInteger w, NSUInteger h, MTLSize *tg)
{
	NSUInteger tw = pso.threadExecutionWidth;
	NSUInteger th = std::max<NSUInteger>(1, pso.maxTotalThreadsPerThreadgroup / tw);
	*tg = MTLSizeMake(tw, th, 1);
	return MTLSizeMake((w + tw - 1) / tw, (h + th - 1) / th, 1);
}

} // namespace

struct MetalFXUpscaler::Impl
{
	id<MTLDevice> device = nil;
	id<MTLCommandQueue> queue = nil;
	id<MTLComputePipelineState> yuv_pso = nil;
	id<MTLComputePipelineState> copy_pso = nil;
	CVMetalTextureCacheRef cache = nullptr;

	// Recreated when the configuration changes.
	id scaler = nil; // id<MTLFXSpatialScaler>
	id<MTLTexture> rgb_tex = nil;
	id<MTLTexture> scaled_tex = nil;
	CVPixelBufferPoolRef pool = nullptr;
	int in_w = 0, in_h = 0, out_w = 0, out_h = 0;
	bool hdr = false;
	bool broken = false;

	bool Init()
	{
		device = MTLCreateSystemDefaultDevice();
		if (!device)
			return false;
		queue = [device newCommandQueue];
		NSError *err = nil;
		id<MTLLibrary> lib = [device newLibraryWithSource:kShaderSource options:nil error:&err];
		if (!lib) {
			NSLog(@"MetalFXUpscaler: shader compile failed: %@", err);
			return false;
		}
		yuv_pso = [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"yuv_to_rgb"] error:&err];
		copy_pso = [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"copy_clamped"] error:&err];
		if (!yuv_pso || !copy_pso)
			return false;
		if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &cache) != kCVReturnSuccess)
			return false;
		return true;
	}

	void ReleaseConfig()
	{
		scaler = nil;
		rgb_tex = nil;
		scaled_tex = nil;
		if (pool) {
			CVPixelBufferPoolRelease(pool);
			pool = nullptr;
		}
		in_w = in_h = out_w = out_h = 0;
	}

	~Impl()
	{
		ReleaseConfig();
		if (cache)
			CFRelease(cache);
	}

	bool Configure(int iw, int ih, int ow, int oh, bool ten_bit)
	{
		if (iw == in_w && ih == in_h && ow == out_w && oh == out_h && ten_bit == hdr && scaler)
			return true;
		ReleaseConfig();
		if (@available(macOS 13.0, *)) {
			const MTLPixelFormat fmt = ten_bit ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
			MTLFXSpatialScalerDescriptor *desc = [MTLFXSpatialScalerDescriptor new];
			desc.inputWidth = iw;
			desc.inputHeight = ih;
			desc.outputWidth = ow;
			desc.outputHeight = oh;
			desc.colorTextureFormat = fmt;
			desc.outputTextureFormat = fmt;
			// Input is gamma- or PQ-encoded, which is what "perceptual" expects.
			desc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
			id<MTLFXSpatialScaler> s = [desc newSpatialScalerWithDevice:device];
			if (!s)
				return false;

			MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:iw height:ih mipmapped:NO];
			td.storageMode = MTLStorageModePrivate;
			td.usage = s.colorTextureUsage | MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
			rgb_tex = [device newTextureWithDescriptor:td];

			td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:ow height:oh mipmapped:NO];
			td.storageMode = MTLStorageModePrivate;
			td.usage = s.outputTextureUsage | MTLTextureUsageShaderRead;
			scaled_tex = [device newTextureWithDescriptor:td];
			if (!rgb_tex || !scaled_tex)
				return false;

			const OSType out_fmt = ten_bit ? kCVPixelFormatType_64RGBALE : kCVPixelFormatType_32BGRA;
			NSDictionary *attrs = @{
				(id)kCVPixelBufferPixelFormatTypeKey: @(out_fmt),
				(id)kCVPixelBufferWidthKey: @(ow),
				(id)kCVPixelBufferHeightKey: @(oh),
				(id)kCVPixelBufferMetalCompatibilityKey: @YES,
				(id)kCVPixelBufferIOSurfacePropertiesKey: @{},
			};
			if (CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr, (__bridge CFDictionaryRef)attrs, &pool) != kCVReturnSuccess)
				return false;

			scaler = s;
			in_w = iw; in_h = ih; out_w = ow; out_h = oh; hdr = ten_bit;
			return true;
		}
		return false;
	}

	id<MTLTexture> Wrap(CVPixelBufferRef pb, size_t plane, MTLPixelFormat fmt, CVMetalTextureRef *holder)
	{
		const size_t w = CVPixelBufferGetWidthOfPlane(pb, plane);
		const size_t h = CVPixelBufferGetHeightOfPlane(pb, plane);
		if (CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, cache, pb, nullptr, fmt, w, h, plane, holder) != kCVReturnSuccess)
			return nil;
		return CVMetalTextureGetTexture(*holder);
	}
};

MetalFXUpscaler::MetalFXUpscaler() : impl(new Impl)
{
	if (!IsSupported() || !impl->Init())
		impl->broken = true;
}

MetalFXUpscaler::~MetalFXUpscaler()
{
	delete impl;
}

void MetalFXUpscaler::WaitFrameReady(const AVFrame *frame)
{
	if (!FrameIOSurface(frame, nullptr))
		return;
	const auto *mk = reinterpret_cast<const IOSurfaceMarker *>(frame->buf[1]->data);
	if (mk->command_buffer)
		[(__bridge id<MTLCommandBuffer>)mk->command_buffer waitUntilCompleted];
}

void *MetalFXUpscaler::FrameIOSurface(const AVFrame *frame, size_t *alloc_size)
{
	if (!frame || (frame->format != AV_PIX_FMT_BGRA && frame->format != AV_PIX_FMT_RGBA64LE))
		return nullptr;
	const AVBufferRef *b = frame->buf[1];
	if (!b || b->size != sizeof(IOSurfaceMarker))
		return nullptr;
	const auto *mk = reinterpret_cast<const IOSurfaceMarker *>(b->data);
	if (mk->magic != kIOSurfaceMarkerMagic)
		return nullptr;
	if (alloc_size)
		*alloc_size = mk->alloc_size;
	return mk->surface;
}

double MetalFXUpscaler::NativePixelScaleAt(double x, double y)
{
	CGDirectDisplayID display = 0;
	uint32_t count = 0;
	if (CGGetDisplaysWithPoint(CGPointMake(x, y), 1, &display, &count) != kCGErrorSuccess || count == 0)
		display = CGMainDisplayID();
	CGDisplayModeRef current = CGDisplayCopyDisplayMode(display);
	if (!current)
		return 1.0;
	const size_t backing = CGDisplayModeGetPixelWidth(current);
	CGDisplayModeRelease(current);

	// kDisplayModeNativeFlag from IOKit/graphics/IOGraphicsTypes.h
	constexpr uint32_t kNativeFlag = 0x02000000;
	size_t native = 0;
	NSDictionary *opts = @{(id)kCGDisplayShowDuplicateLowResolutionModes: @YES};
	CFArrayRef modes = CGDisplayCopyAllDisplayModes(display, (__bridge CFDictionaryRef)opts);
	if (modes) {
		for (CFIndex i = 0; i < CFArrayGetCount(modes); i++) {
			auto mode = (CGDisplayModeRef)CFArrayGetValueAtIndex(modes, i);
			if (CGDisplayModeGetIOFlags(mode) & kNativeFlag)
				native = std::max(native, CGDisplayModeGetPixelWidth(mode));
		}
		CFRelease(modes);
	}
	if (!native || !backing || native >= backing)
		return 1.0;
	return double(native) / double(backing);
}

bool MetalFXUpscaler::IsSupported()
{
	if (@available(macOS 13.0, *)) {
		id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
		return dev && [MTLFXSpatialScalerDescriptor supportsDevice:dev];
	}
	return false;
}

AVFrame *MetalFXUpscaler::Process(const AVFrame *src, int target_width, int target_height)
{
	if (!src || src->format != AV_PIX_FMT_VIDEOTOOLBOX || !src->data[3])
		return nullptr;
	return ProcessPixelBuffer(src->data[3], src, target_width, target_height);
}

AVFrame *MetalFXUpscaler::ProcessPixelBuffer(void *pixel_buffer, const AVFrame *src, int target_width, int target_height)
{
	if (impl->broken || !pixel_buffer || !src)
		return nullptr;
	if (target_width <= 0 || target_height <= 0)
		return nullptr;

	@autoreleasepool {
		CVPixelBufferRef in_pb = static_cast<CVPixelBufferRef>(pixel_buffer);
		const OSType in_fmt = CVPixelBufferGetPixelFormatType(in_pb);
		bool ten_bit;
		switch (in_fmt) {
		case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
		case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
			ten_bit = false;
			break;
		case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
		case kCVPixelFormatType_420YpCbCr10BiPlanarFullRange:
			ten_bit = true;
			break;
		default:
			return nullptr;
		}

		// Visible size. VideoToolbox buffers can be padded; use the frame's size.
		const int iw = src->width, ih = src->height;
		if (iw <= 0 || ih <= 0)
			return nullptr;

		// Fit the target, keep the aspect ratio, clamp to kMaxScale.
		double s = std::min(double(target_width) / iw, double(target_height) / ih);
		s = std::min(s, kMaxScale);
		const int ow = int(std::lround(iw * s)) & ~1;
		const int oh = int(std::lround(ih * s)) & ~1;
		if (ow <= iw || oh <= ih)
			return nullptr; // nothing to upscale

		if (!impl->Configure(iw, ih, ow, oh, ten_bit)) {
			NSLog(@"MetalFXUpscaler: configure %dx%d -> %dx%d failed, disabling", iw, ih, ow, oh);
			impl->ReleaseConfig();
			impl->broken = true;
			return nullptr;
		}

		const MTLPixelFormat luma_fmt = ten_bit ? MTLPixelFormatR16Unorm : MTLPixelFormatR8Unorm;
		const MTLPixelFormat chroma_fmt = ten_bit ? MTLPixelFormatRG16Unorm : MTLPixelFormatRG8Unorm;
		CVMetalTextureRef y_ref = nullptr, c_ref = nullptr, o_ref = nullptr;
		CVPixelBufferRef out_pb = nullptr;
		AVFrame *out = nullptr;

		id<MTLTexture> y_tex = impl->Wrap(in_pb, 0, luma_fmt, &y_ref);
		id<MTLTexture> c_tex = impl->Wrap(in_pb, 1, chroma_fmt, &c_ref);
		if (y_tex && c_tex && CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, impl->pool, &out_pb) == kCVReturnSuccess) {
			const MTLPixelFormat out_mtl = ten_bit ? MTLPixelFormatRGBA16Unorm : MTLPixelFormatBGRA8Unorm;
			id<MTLTexture> o_tex = impl->Wrap(out_pb, 0, out_mtl, &o_ref);
			if (o_tex) {
				YuvParams params = make_yuv_params(src->colorspace, src->color_range, ten_bit);
				if (in_fmt == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange || in_fmt == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange)
					params = make_yuv_params(src->colorspace, AVCOL_RANGE_JPEG, ten_bit);

				id<MTLCommandBuffer> cb = [impl->queue commandBuffer];
				MTLSize tg;
				id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
				[enc setComputePipelineState:impl->yuv_pso];
				[enc setTexture:y_tex atIndex:0];
				[enc setTexture:c_tex atIndex:1];
				[enc setTexture:impl->rgb_tex atIndex:2];
				[enc setBytes:&params length:sizeof(params) atIndex:0];
				MTLSize grid = grid_for(impl->yuv_pso, iw, ih, &tg);
				[enc dispatchThreadgroups:grid threadsPerThreadgroup:tg];
				[enc endEncoding];

				if (@available(macOS 13.0, *)) {
					id<MTLFXSpatialScaler> scaler = impl->scaler;
					scaler.colorTexture = impl->rgb_tex;
					scaler.outputTexture = impl->scaled_tex;
					scaler.inputContentWidth = iw;
					scaler.inputContentHeight = ih;
					[scaler encodeToCommandBuffer:cb];
				}

				enc = [cb computeCommandEncoder];
				[enc setComputePipelineState:impl->copy_pso];
				[enc setTexture:impl->scaled_tex atIndex:0];
				[enc setTexture:o_tex atIndex:1];
				grid = grid_for(impl->copy_pso, ow, oh, &tg);
				[enc dispatchThreadgroups:grid threadsPerThreadgroup:tg];
				[enc endEncoding];

				// Do not wait here: this runs on the decoder thread, and blocking it
				// made the decoder drop frames when packets arrived in bursts. The
				// GPU inputs stay alive until the command buffer completes, and the
				// renderer waits via WaitFrameReady() just before it reads the frame.
				CVPixelBufferRef in_hold = CVPixelBufferRetain(in_pb);
				CVMetalTextureRef y_hold = y_ref, c_hold = c_ref, o_hold = o_ref;
				y_ref = c_ref = o_ref = nullptr;
				[cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
					if (done.status != MTLCommandBufferStatusCompleted)
						NSLog(@"MetalFXUpscaler: command buffer failed: %@", done.error);
					if (o_hold) CFRelease(o_hold);
					if (y_hold) CFRelease(y_hold);
					if (c_hold) CFRelease(c_hold);
					CVPixelBufferRelease(in_hold);
				}];
				[cb commit];

				bool marked = false;
				if (CVPixelBufferLockBaseAddress(out_pb, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess) {
					const int stride = int(CVPixelBufferGetBytesPerRow(out_pb));
					out = av_frame_alloc();
					AVBufferRef *buf = out ? av_buffer_create(
						static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(out_pb)),
						CVPixelBufferGetDataSize(out_pb), release_output, out_pb, AV_BUFFER_FLAG_READONLY) : nullptr;
					if (!buf) {
						CVPixelBufferUnlockBaseAddress(out_pb, kCVPixelBufferLock_ReadOnly);
						av_frame_free(&out);
					} else {
						out_pb = nullptr; // owned by buf now
						av_frame_copy_props(out, src);
						out->format = ten_bit ? AV_PIX_FMT_RGBA64LE : AV_PIX_FMT_BGRA;
						out->width = ow;
						out->height = oh;
						out->buf[0] = buf;
						out->data[0] = buf->data;
						out->linesize[0] = stride;
						out->colorspace = AVCOL_SPC_RGB;
						out->color_range = AVCOL_RANGE_JPEG;
						out->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
						out->crop_top = out->crop_bottom = out->crop_left = out->crop_right = 0;

						CVPixelBufferRef owner = static_cast<CVPixelBufferRef>(av_buffer_get_opaque(buf));
						if (IOSurfaceRef surf = CVPixelBufferGetIOSurface(owner)) {
							auto *mk = static_cast<IOSurfaceMarker *>(av_mallocz(sizeof(IOSurfaceMarker)));
							if (mk) {
								mk->magic = kIOSurfaceMarkerMagic;
								mk->surface = (IOSurfaceRef)CFRetain(surf);
								mk->alloc_size = IOSurfaceGetAllocSize(surf);
								mk->command_buffer = CFBridgingRetain(cb);
								out->buf[1] = av_buffer_create(reinterpret_cast<uint8_t *>(mk), sizeof(*mk),
															   release_marker, nullptr, AV_BUFFER_FLAG_READONLY);
								if (!out->buf[1])
									release_marker(nullptr, reinterpret_cast<uint8_t *>(mk));
								else
									marked = true;
							}
						}
					}
				}
				// Without a marker nobody can wait later, so wait now.
				if (!marked)
					[cb waitUntilCompleted];
			}
		}

		if (o_ref) CFRelease(o_ref);
		if (y_ref) CFRelease(y_ref);
		if (c_ref) CFRelease(c_ref);
		if (out_pb) CVPixelBufferRelease(out_pb);
		return out;
	}
}
