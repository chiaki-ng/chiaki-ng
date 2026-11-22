// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Optical-flow frame generation for chiaki-ng.
//
// The PS5 encodes whatever the game renders, so a 30 fps game arrives as a
// 30 fps video. This module estimates motion between two decoded frames on the
// GPU (no game data needed) and synthesizes the frame in between, which lets a
// 60/120 Hz display show new motion on every refresh.
//
// Pipeline (all libplacebo shaders, Vulkan or OpenGL):
//   1. YUV -> RGB conversion of each source frame (cached per frame signature)
//   2. Luma pyramid (1/2 .. 1/32 resolution)
//   3. Coarse-to-fine symmetric block matching at the midpoint between frames
//   4. 3x3 vector median to remove outliers
//   5. Bidirectional warp to the requested phase, with occlusion and
//      bad-match fallbacks so HUDs and scene cuts don't smear

#pragma once

#include <cstdint>

#include <libplacebo/dispatch.h>
#include <libplacebo/gpu.h>
#include <libplacebo/renderer.h>

class FrameGen
{
public:
    FrameGen(pl_log log, pl_gpu gpu);
    ~FrameGen();

    FrameGen(const FrameGen &) = delete;
    FrameGen &operator=(const FrameGen &) = delete;

    // Synthesizes the frame at phase t (0 = a, 1 = b) between two decoded
    // frames. `out` stays valid until the next call or reset().
    // Returns false if the GPU work failed; the caller should then render the
    // source frames normally.
    bool interpolate(const struct pl_frame *a, uint64_t sig_a,
                     const struct pl_frame *b, uint64_t sig_b,
                     float t, struct pl_frame *out);

    // Drops all cached textures (call on stream restart or resolution change).
    void reset();

    bool ok() const { return init_ok; }

#ifdef FRAMEGEN_DEBUG
    pl_tex debugFlow(int level) const { return flow[level]; }
    int debugFinestLevel() const { return finest_level; }
#endif

    static constexpr int kLevels = 5;

private:
    struct Slot {
        uint64_t sig = 0;
        bool valid = false;
        int w = 0, h = 0;
        pl_tex rgb = nullptr;
        pl_tex luma[kLevels] = {};
        struct pl_color_space color = {};
    };

    Slot *prepareSlot(const struct pl_frame *src, uint64_t sig);
    bool computeFlow(const Slot *a, const Slot *b);
    bool dispatchLuma(pl_tex src, pl_tex dst, bool from_rgb);
    bool dispatchFlow(int level, const Slot *a, const Slot *b, pl_tex coarse, pl_tex dst, bool subpixel);
    bool dispatchMedian(pl_tex src, pl_tex dst);
    bool dispatchWarp(const Slot *a, const Slot *b, pl_tex flow, float t, pl_tex dst);
    bool ensureTex(pl_tex *tex, int w, int h, pl_fmt fmt);
    void addPosAttrib(struct pl_shader_va *va);

    pl_log log = nullptr;
    pl_gpu gpu = nullptr;
    pl_dispatch dp = nullptr;
    pl_renderer conv = nullptr;
    pl_fmt fmt_rgba = nullptr;
    pl_fmt fmt_r = nullptr;
    pl_fmt fmt_flow = nullptr;
    pl_fmt fmt_va = nullptr;
    bool init_ok = false;

    Slot cache_slots[3];
    uint64_t lru_counter = 0;
    uint64_t slot_used[3] = {};

    pl_tex flow[kLevels] = {};
    pl_tex flow_tmp[kLevels] = {};
    int finest_level = 1;
    uint64_t flow_sig_a = 0, flow_sig_b = 0;
    bool flow_valid = false;

    pl_tex out_tex = nullptr;
    pl_tex dummy_tex = nullptr;
};
