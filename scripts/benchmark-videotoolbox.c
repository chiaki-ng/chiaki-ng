// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Synthetic preparation benchmark; no PS5/network/display latency claim.
#include "videotoolboxmap.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <stdio.h>
#include <string.h>

static enum AVPixelFormat get_format(AVCodecContext *ctx, const enum AVPixelFormat *formats)
{
    (void)ctx;
    for(; *formats != AV_PIX_FMT_NONE; ++formats)
        if(*formats == AV_PIX_FMT_VIDEOTOOLBOX)
            return *formats;
    return AV_PIX_FMT_NONE;
}

#define REQUIRE(x) do { if(!(x)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #x); return 1; } } while(0)

int main(int argc, char **argv)
{
    REQUIRE(argc == 2);
    AVFormatContext *input = NULL;
    REQUIRE(avformat_open_input(&input, argv[1], NULL, NULL) == 0);
    REQUIRE(avformat_find_stream_info(input, NULL) >= 0);
    int stream = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    REQUIRE(stream >= 0);
    const AVCodec *codec = avcodec_find_decoder(input->streams[stream]->codecpar->codec_id);
    AVCodecContext *decoder = avcodec_alloc_context3(codec);
    REQUIRE(decoder);
    REQUIRE(avcodec_parameters_to_context(decoder, input->streams[stream]->codecpar) >= 0);
    REQUIRE(av_hwdevice_ctx_create(&decoder->hw_device_ctx, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0) >= 0);
    decoder->get_format = get_format;
    REQUIRE(avcodec_open2(decoder, codec, NULL) >= 0);
    AVPacket *packet = av_packet_alloc();
    AVFrame *source = av_frame_alloc();
    unsigned count = 0;
    enum AVPixelFormat pixel_format = AV_PIX_FMT_NONE;
    int64_t copy_us = 0, map_us = 0;
    int eof = 0;
    while(!eof) {
        eof = av_read_frame(input, packet) < 0;
        if(!eof && packet->stream_index != stream) { av_packet_unref(packet); continue; }
        REQUIRE(avcodec_send_packet(decoder, eof ? NULL : packet) >= 0);
        av_packet_unref(packet);
        int ret;
        while((ret = avcodec_receive_frame(decoder, source)) >= 0) {
            REQUIRE(source->format == AV_PIX_FMT_VIDEOTOOLBOX);
            AVFrame *copy = NULL, *mapped = NULL;
            // Alternate operation order to limit cache/order bias.
            for(int step = 0; step < 2; ++step) {
                int64_t begin = av_gettime_relative();
                if((count + step) % 2) {
                    mapped = chiaki_videotoolbox_map(source);
                    map_us += av_gettime_relative() - begin;
                    REQUIRE(mapped);
                } else {
                    copy = av_frame_alloc();
                    REQUIRE(copy && av_hwframe_transfer_data(copy, source, 0) >= 0);
                    REQUIRE(av_frame_copy_props(copy, source) >= 0);
                    copy_us += av_gettime_relative() - begin;
                }
            }
            REQUIRE(copy->format == mapped->format);
            pixel_format = mapped->format;
            REQUIRE(copy->width == mapped->width && copy->height == mapped->height);
            REQUIRE(copy->pts == mapped->pts && copy->color_range == mapped->color_range);
            REQUIRE(copy->colorspace == mapped->colorspace && copy->color_trc == mapped->color_trc);
            // Source release must not invalidate the mapped view.
            av_frame_unref(source);
            int size = av_image_get_buffer_size(copy->format, copy->width, copy->height, 1);
            REQUIRE(size > 0);
            uint8_t *a = av_malloc(size), *b = av_malloc(size);
            REQUIRE(a && b);
            REQUIRE(av_image_copy_to_buffer(a, size, (const uint8_t *const *)copy->data,
                copy->linesize, copy->format, copy->width, copy->height, 1) == size);
            REQUIRE(av_image_copy_to_buffer(b, size, (const uint8_t *const *)mapped->data,
                mapped->linesize, mapped->format, mapped->width, mapped->height, 1) == size);
            REQUIRE(memcmp(a, b, size) == 0);
            av_free(a); av_free(b);
            av_frame_free(&copy); av_frame_free(&mapped);
            ++count;
        }
        REQUIRE(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
    }
    REQUIRE(count >= 60);
    printf("{\"codec\":\"%s\",\"pixel_format\":\"%s\",\"frames\":%u,\"copy_mean_us\":%.3f,\"map_mean_us\":%.3f,"
           "\"pixels_equal\":true,\"source_release_safe\":true}\n", codec->name,
           av_get_pix_fmt_name(pixel_format), count, (double)copy_us / count, (double)map_us / count);
    av_frame_free(&source); av_packet_free(&packet);
    avcodec_free_context(&decoder); avformat_close_input(&input);
    return 0;
}
