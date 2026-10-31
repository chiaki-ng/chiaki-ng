// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "videotoolboxmap.h"
#include <libavutil/imgutils.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixdesc.h>
#include <stdio.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #value); return 1; \
} } while (0)

static int check_mapping(enum AVPixelFormat format)
{
    const int width = 130, height = 74; // Exercise row padding as well as pixel data.
    AVBufferRef *device = NULL;
    CHECK(av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0) >= 0);
    AVBufferRef *frames = av_hwframe_ctx_alloc(device);
    CHECK(frames);
    AVHWFramesContext *context = (AVHWFramesContext *)frames->data;
    context->format = AV_PIX_FMT_VIDEOTOOLBOX;
    context->sw_format = format;
    context->width = width;
    context->height = height;
    CHECK(av_hwframe_ctx_init(frames) >= 0);

    AVFrame *software = av_frame_alloc(), *source = av_frame_alloc();
    CHECK(software && source);
    software->format = format;
    software->width = width;
    software->height = height;
    CHECK(av_frame_get_buffer(software, 32) >= 0);
    for (int plane = 0; plane < av_pix_fmt_count_planes(format); ++plane) {
        const int rows = plane ? height / 2 : height;
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < software->linesize[plane]; ++x)
                software->data[plane][y * software->linesize[plane] + x] = (x + y * 7 + plane * 19) & 255;
    }
    CHECK(av_hwframe_get_buffer(frames, source, 0) >= 0);
    CHECK(av_hwframe_transfer_data(source, software, 0) >= 0);
    source->pts = 12345;
    source->color_range = AVCOL_RANGE_MPEG;
    source->colorspace = AVCOL_SPC_BT2020_NCL;
    source->color_primaries = AVCOL_PRI_BT2020;
    source->color_trc = AVCOL_TRC_SMPTE2084;
    source->chroma_location = AVCHROMA_LOC_LEFT;
    source->sample_aspect_ratio = (AVRational){4, 3};
    AVMasteringDisplayMetadata *hdr = av_mastering_display_metadata_create_side_data(source);
    CHECK(hdr);
    hdr->has_luminance = 1;
    hdr->min_luminance = (AVRational){1, 10000};
    hdr->max_luminance = (AVRational){1000, 1};

    AVFrame *mapped = chiaki_videotoolbox_map(source);
    CHECK(mapped);
    CHECK(mapped->format == format && mapped->width == width && mapped->height == height);
    CHECK(mapped->pts == source->pts && mapped->color_range == source->color_range);
    CHECK(mapped->colorspace == source->colorspace && mapped->color_primaries == source->color_primaries);
    CHECK(mapped->color_trc == source->color_trc && mapped->chroma_location == source->chroma_location);
    CHECK(av_cmp_q(mapped->sample_aspect_ratio, source->sample_aspect_ratio) == 0);
    const AVFrameSideData *side = av_frame_get_side_data(mapped, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    CHECK(side && side->size == sizeof(*hdr) && memcmp(side->data, hdr, sizeof(*hdr)) == 0);

    AVFrame *retained = av_frame_clone(mapped);
    CHECK(retained);
    av_frame_free(&mapped);
    av_frame_free(&source);
    av_buffer_unref(&frames);
    av_buffer_unref(&device);
    // Only the cloned mapping now retains the original pixel buffer and context.
    const int size = av_image_get_buffer_size(format, width, height, 1);
    CHECK(size > 0);
    uint8_t *expected = av_malloc(size), *actual = av_malloc(size);
    CHECK(expected && actual);
    CHECK(av_image_copy_to_buffer(expected, size, (const uint8_t *const *)software->data,
        software->linesize, format, width, height, 1) == size);
    CHECK(av_image_copy_to_buffer(actual, size, (const uint8_t *const *)retained->data,
        retained->linesize, format, width, height, 1) == size);
    CHECK(memcmp(expected, actual, size) == 0);
    av_free(expected);
    av_free(actual);
    av_frame_free(&retained);
    av_frame_free(&software);
    printf("%s: pixels, metadata and retained mapping lifetime passed\n", av_get_pix_fmt_name(format));
    return 0;
}

int main(void)
{
    if (av_hwdevice_find_type_by_name("videotoolbox") == AV_HWDEVICE_TYPE_NONE) {
        puts("SKIP: FFmpeg was built without VideoToolbox support");
        return 77;
    }
    CHECK(chiaki_videotoolbox_map(NULL) == NULL);
    AVFrame *invalid = av_frame_alloc();
    CHECK(invalid);
    invalid->format = AV_PIX_FMT_NV12;
    CHECK(chiaki_videotoolbox_map(invalid) == NULL);
    invalid->format = AV_PIX_FMT_VIDEOTOOLBOX;
    CHECK(chiaki_videotoolbox_map(invalid) == NULL); // Missing hardware context.
    av_frame_free(&invalid);
    CHECK(check_mapping(AV_PIX_FMT_NV12) == 0);
    CHECK(check_mapping(AV_PIX_FMT_P010LE) == 0);
    return 0;
}
