// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "framegen.h"

#include <libplacebo/shaders/custom.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Shared GLSL helpers. Names prefixed fg_ are #defined by pl_shader_custom to
// the real uniform/descriptor identifiers.
const char *kLumaBody = R"(
    vec3 c = texture(fg_src, fg_pos).rgb;
    float l = fg_from_rgb != 0 ? dot(c, vec3(0.2126, 0.7152, 0.0722)) : c.r;
    color = vec4(l, 0.0, 0.0, 1.0);
)";

// Symmetric block matching: for every pixel of the in-between frame, find the
// vector v so that A(p - v/2) matches B(p + v/2). Estimating at the midpoint
// (instead of forward from A) means every output pixel gets its own vector and
// the warp has no holes.
const char *kFlowHeader = R"(
float fg_cost(vec2 uv, vec2 half_v)
{
    float s = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            vec2 o = vec2(float(x), float(y)) * fg_inv_size;
            s += abs(texture(fg_a, uv - half_v + o).r - texture(fg_b, uv + half_v + o).r);
        }
    }
    return s * (1.0 / 25.0);
}

void fg_try(vec2 uv, vec2 v, vec2 ref, float penalty, inout vec2 best_v, inout float best_c)
{
    vec2 hv = 0.5 * v * fg_inv_size;
    // Smoothness prior (in full-res px): at the top level prefer small motion,
    // below that prefer agreeing with the coarser estimate. Keeps flat or
    // repetitive areas from latching onto far-away false matches.
    penalty += fg_motion_cost * length(v - ref) * fg_scale;
    // Matches that read past the frame edge only "fit" because of clamping
    vec2 lo = min(uv - hv, uv + hv), hi = max(uv - hv, uv + hv);
    if (any(lessThan(lo, vec2(0.0))) || any(greaterThan(hi, vec2(1.0))))
        penalty += 0.05;
    float c = fg_cost(uv, hv) + penalty;
    if (c < best_c) {
        best_c = c;
        best_v = v;
    }
}
)";

const char *kFlowBody = R"(
    vec2 uv = fg_pos;
    vec2 best_v = vec2(0.0);
    // Zero motion gets no penalty so static HUD elements stay locked
    float raw_zero = fg_cost(uv, vec2(0.0));
    vec2 pred = vec2(0.0);
    if (fg_has_coarse != 0)
        pred = texture(fg_coarse, uv).xy / fg_scale;
    // Zero motion only pays the smoothness term, so static HUD elements lock
    float best_c = raw_zero + fg_motion_cost * length(pred) * fg_scale;

    if (fg_has_coarse != 0) {
        fg_try(uv, pred, pred, 0.0, best_v, best_c);
        // Vectors from nearby pixels let object edges pick up the background
        // (or foreground) motion instead of the blurred coarse estimate.
        // These are not tied to pred, so the smoothness term is waived.
        for (int r = 0; r < 2; r++) {
            vec2 d = (r == 0 ? 3.0 : 8.0) * fg_inv_size;
            for (int i = 0; i < 8; i++) {
                float ang = float(i) * 0.785398;
                vec2 nv = texture(fg_coarse, uv + vec2(cos(ang), sin(ang)) * d).xy / fg_scale;
                fg_try(uv, nv, nv, 0.002, best_v, best_c);
            }
        }
    }

    vec2 center = best_v;
    for (int y = -fg_radius; y <= fg_radius; y++) {
        for (int x = -fg_radius; x <= fg_radius; x++) {
            if (x == 0 && y == 0)
                continue;
            vec2 o = vec2(float(x), float(y));
            fg_try(uv, center + o, pred, 0.001, best_v, best_c);
        }
    }

    if (fg_subpixel != 0) {
        vec2 c2 = best_v;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                if (x == 0 && y == 0)
                    continue;
                fg_try(uv, c2 + 0.5 * vec2(float(x), float(y)), pred, 0.0, best_v, best_c);
            }
        }
    }

    // .z = match cost (lower is better), .w = how much better than no motion
    color = vec4(best_v * fg_scale, best_c, max(raw_zero - best_c, 0.0));
)";

const char *kMedianBody = R"(
    vec4 s[9];
    int k = 0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++)
            s[k++] = texture(fg_src, fg_pos + vec2(float(x), float(y)) * fg_inv_size);
    int best = 4;
    float best_d = 1e20;
    for (int i = 0; i < 9; i++) {
        float d = 0.0;
        for (int j = 0; j < 9; j++)
            d += length(s[i].xy - s[j].xy);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    color = vec4(s[best].xy, s[4].z, s[4].w);
)";

const char *kWarpBody = R"(
    vec2 uv = fg_pos;
    vec4 f = texture(fg_flow, uv);
    vec2 v = f.xy * fg_inv_size;
    float cost = f.z;
    float t = fg_t;

    vec3 ca = texture(fg_a, uv - t * v).rgb;
    vec3 cb = texture(fg_b, uv + (1.0 - t) * v).rgb;
    vec3 res = mix(ca, cb, t);

    // Occlusion: the two motion-compensated samples disagree, so one of them
    // is hidden. Prefer the frame that is closer in time.
    float d = abs(dot(ca - cb, vec3(0.2126, 0.7152, 0.0722)));
    float occl = smoothstep(0.06, 0.20, d);
    res = mix(res, t < 0.5 ? ca : cb, occl);

    // Unreliable vectors (scene cut, particles, heavy compression): show the
    // nearest real frame instead of inventing garbage.
    float bad = smoothstep(fg_bad_lo, fg_bad_hi, cost);
    vec3 nearest = t < 0.5 ? texture(fg_a, uv).rgb : texture(fg_b, uv).rgb;
    res = mix(res, nearest, bad);

    color = vec4(res, 1.0);
)";

int levelDim(int full, int level)
{
    int d = full;
    for (int i = 0; i <= level; i++)
        d = (d + 1) / 2;
    return std::max(d, 1);
}

struct pl_shader_desc sampled(const char *name, pl_tex tex)
{
    struct pl_shader_desc d = {};
    d.desc.name = name;
    d.desc.type = PL_DESC_SAMPLED_TEX;
    d.binding.object = tex;
    d.binding.sample_mode = PL_TEX_SAMPLE_LINEAR;
    d.binding.address_mode = PL_TEX_ADDRESS_CLAMP;
    return d;
}

struct pl_shader_var shaderVar(struct pl_var var, const void *data, bool dynamic = false)
{
    struct pl_shader_var v = {};
    v.var = var;
    v.data = data;
    v.dynamic = dynamic;
    return v;
}

const float kCorners[4][2] = { {0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f} };

} // namespace

FrameGen::FrameGen(pl_log log_, pl_gpu gpu_)
    : log(log_), gpu(gpu_)
{
    dp = pl_dispatch_create(log, gpu);
    conv = pl_renderer_create(log, gpu);
    fmt_rgba = pl_find_named_fmt(gpu, "rgba16f");
    fmt_r = pl_find_named_fmt(gpu, "r16f");
#ifdef FRAMEGEN_DEBUG
    fmt_flow = pl_find_named_fmt(gpu, "rgba32f");
#else
    fmt_flow = pl_find_named_fmt(gpu, "rgba16f");
#endif
    fmt_va = pl_find_vertex_fmt(gpu, PL_FMT_FLOAT, 2);

    const unsigned need = PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_LINEAR;
    init_ok = dp && conv && fmt_va &&
              fmt_rgba && (fmt_rgba->caps & need) == need &&
              fmt_r && (fmt_r->caps & need) == need;
    if (init_ok)
        init_ok = ensureTex(&dummy_tex, 1, 1, fmt_flow);
}

FrameGen::~FrameGen()
{
    reset();
    pl_tex_destroy(gpu, &dummy_tex);
    pl_renderer_destroy(&conv);
    pl_dispatch_destroy(&dp);
}

void FrameGen::reset()
{
    for (auto &s : cache_slots) {
        pl_tex_destroy(gpu, &s.rgb);
        for (auto &l : s.luma)
            pl_tex_destroy(gpu, &l);
        s = Slot{};
    }
    for (int i = 0; i < kLevels; i++) {
        pl_tex_destroy(gpu, &flow[i]);
        pl_tex_destroy(gpu, &flow_tmp[i]);
    }
    pl_tex_destroy(gpu, &out_tex);
    flow_valid = false;
}

bool FrameGen::ensureTex(pl_tex *tex, int w, int h, pl_fmt fmt)
{
    struct pl_tex_params p = {};
    p.w = w;
    p.h = h;
    p.format = fmt;
    p.sampleable = true;
    p.renderable = true;
    p.blit_src = (fmt->caps & PL_FMT_CAP_BLITTABLE) != 0;
#ifdef FRAMEGEN_DEBUG
    p.host_readable = (fmt->caps & PL_FMT_CAP_HOST_READABLE) != 0;
#endif
    return pl_tex_recreate(gpu, tex, &p);
}

void FrameGen::addPosAttrib(struct pl_shader_va *va)
{
    *va = {};
    va->attr.name = "fg_pos";
    va->attr.fmt = fmt_va;
    for (int i = 0; i < 4; i++)
        va->data[i] = kCorners[i];
}

bool FrameGen::dispatchLuma(pl_tex src, pl_tex dst, bool from_rgb)
{
    pl_shader sh = pl_dispatch_begin(dp);
    struct pl_shader_desc desc = sampled("fg_src", src);
    int from_rgb_i = from_rgb ? 1 : 0;
    struct pl_shader_var var = shaderVar(pl_var_int("fg_from_rgb"), &from_rgb_i);
    struct pl_shader_va va;
    addPosAttrib(&va);

    struct pl_custom_shader cs = {};
    cs.description = "framegen luma";
    cs.body = kLumaBody;
    cs.input = PL_SHADER_SIG_NONE;
    cs.output = PL_SHADER_SIG_COLOR;
    cs.descriptors = &desc;
    cs.num_descriptors = 1;
    cs.variables = &var;
    cs.num_variables = 1;
    cs.vertex_attribs = &va;
    cs.num_vertex_attribs = 1;
    if (!pl_shader_custom(sh, &cs)) {
        pl_dispatch_abort(dp, &sh);
        return false;
    }
    struct pl_dispatch_params dpp = {};
    dpp.shader = &sh;
    dpp.target = dst;
    return pl_dispatch_finish(dp, &dpp);
}

bool FrameGen::dispatchFlow(int level, const Slot *a, const Slot *b, pl_tex coarse, pl_tex dst, bool subpixel)
{
    pl_shader sh = pl_dispatch_begin(dp);
    const int w = dst->params.w, h = dst->params.h;
    float inv_size[2] = { 1.0f / w, 1.0f / h };
    float scale = std::ldexp(1.0f, level + 1);
    int has_coarse = coarse ? 1 : 0;
    // Wider search at the coarsest level catches fast pans; finer levels refine.
    int radius = coarse ? 2 : 4;
    int subpix = subpixel ? 1 : 0;

    struct pl_shader_desc descs[3] = {
        sampled("fg_a", a->luma[level]),
        sampled("fg_b", b->luma[level]),
        sampled("fg_coarse", coarse ? coarse : dummy_tex),
    };
    float motion_cost = 0.00012f;
    struct pl_shader_var vars[6] = {
        shaderVar(pl_var_vec2("fg_inv_size"), inv_size),
        shaderVar(pl_var_float("fg_scale"), &scale),
        shaderVar(pl_var_float("fg_motion_cost"), &motion_cost),
        shaderVar(pl_var_int("fg_has_coarse"), &has_coarse),
        shaderVar(pl_var_int("fg_radius"), &radius),
        shaderVar(pl_var_int("fg_subpixel"), &subpix),
    };
    struct pl_shader_va va;
    addPosAttrib(&va);

    struct pl_custom_shader cs = {};
    cs.description = "framegen flow";
    cs.header = kFlowHeader;
    cs.body = kFlowBody;
    cs.input = PL_SHADER_SIG_NONE;
    cs.output = PL_SHADER_SIG_COLOR;
    cs.descriptors = descs;
    cs.num_descriptors = 3;
    cs.variables = vars;
    cs.num_variables = 6;
    cs.vertex_attribs = &va;
    cs.num_vertex_attribs = 1;
    if (!pl_shader_custom(sh, &cs)) {
        pl_dispatch_abort(dp, &sh);
        return false;
    }
    struct pl_dispatch_params dpp = {};
    dpp.shader = &sh;
    dpp.target = dst;
    return pl_dispatch_finish(dp, &dpp);
}

bool FrameGen::dispatchMedian(pl_tex src, pl_tex dst)
{
    pl_shader sh = pl_dispatch_begin(dp);
    float inv_size[2] = { 1.0f / src->params.w, 1.0f / src->params.h };
    struct pl_shader_desc desc = sampled("fg_src", src);
    desc.binding.sample_mode = PL_TEX_SAMPLE_NEAREST;
    struct pl_shader_var var = shaderVar(pl_var_vec2("fg_inv_size"), inv_size);
    struct pl_shader_va va;
    addPosAttrib(&va);

    struct pl_custom_shader cs = {};
    cs.description = "framegen median";
    cs.body = kMedianBody;
    cs.input = PL_SHADER_SIG_NONE;
    cs.output = PL_SHADER_SIG_COLOR;
    cs.descriptors = &desc;
    cs.num_descriptors = 1;
    cs.variables = &var;
    cs.num_variables = 1;
    cs.vertex_attribs = &va;
    cs.num_vertex_attribs = 1;
    if (!pl_shader_custom(sh, &cs)) {
        pl_dispatch_abort(dp, &sh);
        return false;
    }
    struct pl_dispatch_params dpp = {};
    dpp.shader = &sh;
    dpp.target = dst;
    return pl_dispatch_finish(dp, &dpp);
}

bool FrameGen::dispatchWarp(const Slot *a, const Slot *b, pl_tex flow_tex, float t, pl_tex dst)
{
    pl_shader sh = pl_dispatch_begin(dp);
    float inv_size[2] = { 1.0f / dst->params.w, 1.0f / dst->params.h };
    float bad_lo = 0.035f, bad_hi = 0.10f;
    struct pl_shader_desc descs[3] = {
        sampled("fg_a", a->rgb),
        sampled("fg_b", b->rgb),
        sampled("fg_flow", flow_tex),
    };
    struct pl_shader_var vars[4] = {
        shaderVar(pl_var_vec2("fg_inv_size"), inv_size),
        shaderVar(pl_var_float("fg_t"), &t, true),
        shaderVar(pl_var_float("fg_bad_lo"), &bad_lo),
        shaderVar(pl_var_float("fg_bad_hi"), &bad_hi),
    };
    struct pl_shader_va va;
    addPosAttrib(&va);

    struct pl_custom_shader cs = {};
    cs.description = "framegen warp";
    cs.body = kWarpBody;
    cs.input = PL_SHADER_SIG_NONE;
    cs.output = PL_SHADER_SIG_COLOR;
    cs.descriptors = descs;
    cs.num_descriptors = 3;
    cs.variables = vars;
    cs.num_variables = 4;
    cs.vertex_attribs = &va;
    cs.num_vertex_attribs = 1;
    if (!pl_shader_custom(sh, &cs)) {
        pl_dispatch_abort(dp, &sh);
        return false;
    }
    struct pl_dispatch_params dpp = {};
    dpp.shader = &sh;
    dpp.target = dst;
    return pl_dispatch_finish(dp, &dpp);
}

FrameGen::Slot *FrameGen::prepareSlot(const struct pl_frame *src, uint64_t sig)
{
    const int w = std::max(1, (int) std::lround(pl_rect_w(src->crop) ? std::fabs(pl_rect_w(src->crop))
                                                                       : src->planes[0].texture->params.w));
    const int h = std::max(1, (int) std::lround(pl_rect_h(src->crop) ? std::fabs(pl_rect_h(src->crop))
                                                                       : src->planes[0].texture->params.h));

    for (int i = 0; i < 3; i++) {
        if (cache_slots[i].valid && cache_slots[i].sig == sig && cache_slots[i].w == w && cache_slots[i].h == h) {
            slot_used[i] = ++lru_counter;
            return &cache_slots[i];
        }
    }

    int victim = 0;
    for (int i = 1; i < 3; i++)
        if (slot_used[i] < slot_used[victim])
            victim = i;
    Slot &s = cache_slots[victim];
    s.valid = false;
    if (s.w != w || s.h != h)
        flow_valid = false;
    s.w = w;
    s.h = h;

    if (!ensureTex(&s.rgb, w, h, fmt_rgba))
        return nullptr;
    for (int l = 0; l < kLevels; l++)
        if (!ensureTex(&s.luma[l], levelDim(w, l), levelDim(h, l), fmt_r))
            return nullptr;

    // Plain colour conversion at native size, keeping the source colour space
    // (PQ stays PQ) so HDR passes through untouched.
    struct pl_frame target = {};
    target.num_planes = 1;
    target.planes[0].texture = s.rgb;
    target.planes[0].components = 3;
    target.planes[0].component_mapping[0] = 0;
    target.planes[0].component_mapping[1] = 1;
    target.planes[0].component_mapping[2] = 2;
    target.repr = pl_color_repr_rgb;
    target.color = src->color;
    target.crop = { 0, 0, (float) w, (float) h };

    struct pl_render_params rp = pl_render_fast_params;
    rp.dither_params = nullptr;
    rp.skip_anti_aliasing = true;
    if (!pl_render_image(conv, src, &target, &rp))
        return nullptr;

    if (!dispatchLuma(s.rgb, s.luma[0], true))
        return nullptr;
    for (int l = 1; l < kLevels; l++)
        if (!dispatchLuma(s.luma[l - 1], s.luma[l], false))
            return nullptr;

    s.sig = sig;
    s.color = src->color;
    s.valid = true;
    slot_used[victim] = ++lru_counter;
    return &s;
}

bool FrameGen::computeFlow(const Slot *a, const Slot *b)
{
    // Stop at 1/4 resolution for 1080p+ (one vector per 4x4 px is plenty for
    // a video stream); go to 1/2 for 720p and below.
    finest_level = a->w > 1280 ? 1 : 0;
    pl_tex coarse = nullptr;
    for (int l = kLevels - 1; l >= finest_level; l--) {
        const int w = levelDim(a->w, l), h = levelDim(a->h, l);
        if (!ensureTex(&flow_tmp[l], w, h, fmt_flow) || !ensureTex(&flow[l], w, h, fmt_flow))
            return false;
        // Two passes per level: the second one uses this level's own result
        // as the predictor, which sharpens motion boundaries.
        const int passes = l <= 2 ? 2 : 1;
        for (int p = 0; p < passes; p++) {
            const bool last = p == passes - 1;
            if (!dispatchFlow(l, a, b, coarse, flow_tmp[l], last && l == finest_level))
                return false;
            if (!dispatchMedian(flow_tmp[l], flow[l]))
                return false;
            coarse = flow[l];
        }
    }
    return true;
}

bool FrameGen::interpolate(const struct pl_frame *a, uint64_t sig_a,
                           const struct pl_frame *b, uint64_t sig_b,
                           float t, struct pl_frame *out)
{
    if (!init_ok || !a || !b || a->num_planes <= 0 || b->num_planes <= 0)
        return false;

    Slot *sa = prepareSlot(a, sig_a);
    if (!sa)
        return false;
    Slot *sb = prepareSlot(b, sig_b);
    if (!sb || sb->w != sa->w || sb->h != sa->h)
        return false;

    if (!flow_valid || flow_sig_a != sig_a || flow_sig_b != sig_b) {
        flow_valid = false;
        if (!computeFlow(sa, sb))
            return false;
        flow_sig_a = sig_a;
        flow_sig_b = sig_b;
        flow_valid = true;
    }

    if (!ensureTex(&out_tex, sa->w, sa->h, fmt_rgba))
        return false;
    if (!dispatchWarp(sa, sb, flow[finest_level], std::clamp(t, 0.0f, 1.0f), out_tex))
        return false;

    *out = {};
    out->num_planes = 1;
    out->planes[0].texture = out_tex;
    out->planes[0].components = 3;
    out->planes[0].component_mapping[0] = 0;
    out->planes[0].component_mapping[1] = 1;
    out->planes[0].component_mapping[2] = 2;
    out->repr = pl_color_repr_rgb;
    out->color = sa->color;
    out->crop = { 0, 0, (float) sa->w, (float) sa->h };
    return true;
}
