// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>

// A read-only CPU view of a decoded CVPixelBuffer, not a Metal texture import.
// The AVFrame mapping owns a reference to the CVPixelBuffer until unref.
// Never detach the planes from that reference or retain them after freeing it.
static inline AVFrame *chiaki_videotoolbox_map(const AVFrame *source)
{
    if (!source || source->format != AV_PIX_FMT_VIDEOTOOLBOX || !source->hw_frames_ctx)
        return NULL;
    AVFrame *mapped = av_frame_alloc();
    if (!mapped)
        return NULL;
    if (av_hwframe_map(mapped, source, AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT) < 0) {
        av_frame_free(&mapped);
        return NULL;
    }
    return mapped;
}
#ifdef __cplusplus
}
#endif
