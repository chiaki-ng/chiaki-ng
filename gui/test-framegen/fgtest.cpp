// Headless test for FrameGen: renders synthetic frames A (t=0), B (t=1) and
// the ground-truth midpoint M (t=0.5), then compares interpolation quality.
#include "framegen.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdint>
static const int W = 1920, H = 1080;
#ifdef FRAMEGEN_DEBUG
#include "flowdbg.inc"
#endif

#include <libplacebo/log.h>
#include <libplacebo/vulkan.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdint>



static float noise(float x, float y)
{
    // smooth-ish deterministic texture
    return 0.5f + 0.25f * std::sin(x * 0.11f + std::sin(y * 0.05f) * 3.f) * std::cos(y * 0.09f + x * 0.013f)
                + 0.15f * std::sin((x + y) * 0.031f) + 0.1f * std::sin(x * 0.27f) * std::sin(y * 0.23f);
}

// Scene at time s in [0,1]: background pans 24px right, box moves 40px down-left,
// HUD rectangle is static.
static void scene(float s, std::vector<float> &px)
{
    px.assign(W * H * 4, 1.f);
    const float pan = 24.f * s;
    const float bx = 900.f - 40.f * s, by = 400.f + 40.f * s;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float v = noise(x - pan, y);
            float r = v, g = v * 0.8f + 0.1f, b = 1.f - v;
            float lx = x - bx, ly = y - by;
            if (lx >= 0 && lx < 200 && ly >= 0 && ly < 160) {
                float c = noise(lx * 1.3f + 500.f, ly * 1.1f + 300.f);
                r = c; g = 1.f - c; b = 0.2f;
            }
            if (x >= 60 && x < 360 && y >= 960 && y < 1030) {
                { unsigned hsh = (unsigned)((x / 7) * 73856093u ^ (y / 9) * 19349663u); r = g = b = (hsh % 3 == 0) ? 0.95f : 0.08f; }
            }
            float *p = &px[(y * W + x) * 4];
            p[0] = r; p[1] = g; p[2] = b; p[3] = 1.f;
        }
    }
}

static pl_tex upload(pl_gpu gpu, const std::vector<float> &px)
{
    struct pl_tex_params tp = {};
    tp.w = W; tp.h = H;
    tp.format = pl_find_named_fmt(gpu, "rgba32f");
    tp.sampleable = true;
    tp.host_writable = true;
    tp.initial_data = px.data();
    return pl_tex_create(gpu, &tp);
}

static pl_frame as_frame(pl_tex t)
{
    pl_frame f = {};
    f.num_planes = 1;
    f.planes[0].texture = t;
    f.planes[0].components = 3;
    f.planes[0].component_mapping[0] = 0;
    f.planes[0].component_mapping[1] = 1;
    f.planes[0].component_mapping[2] = 2;
    f.repr = pl_color_repr_rgb;
    f.color = pl_color_space_srgb;
    f.crop = {0, 0, (float) W, (float) H};
    return f;
}

static double psnr(const std::vector<float> &a, const std::vector<float> &b, int x0 = 0, int y0 = 0, int x1 = W, int y1 = H)
{
    double se = 0; long n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            for (int c = 0; c < 3; c++) {
                double d = a[(y * W + x) * 4 + c] - b[(y * W + x) * 4 + c];
                se += d * d; n++;
            }
    return 10.0 * std::log10(1.0 / (se / n + 1e-12));
}

static bool download(pl_gpu gpu, pl_log log, const pl_frame *src, std::vector<float> &out)
{
    struct pl_tex_params tp = {};
    tp.w = W; tp.h = H;
    tp.format = pl_find_named_fmt(gpu, "rgba32f");
    tp.host_readable = true;
    tp.renderable = true;
    pl_tex dst = pl_tex_create(gpu, &tp);
    pl_frame target = as_frame(dst);
    pl_renderer rr = pl_renderer_create(log, gpu);
    struct pl_render_params rp = pl_render_fast_params;
    rp.dither_params = nullptr;
    bool ok = pl_render_image(rr, src, &target, &rp);
    out.resize(W * H * 4);
    struct pl_tex_transfer_params xp = {};
    xp.tex = dst;
    xp.ptr = out.data();
    ok = ok && pl_tex_download(gpu, &xp);
    pl_renderer_destroy(&rr);
    pl_tex_destroy(gpu, &dst);
    return ok;
}

static void save_raw(const char *path, const std::vector<float> &px)
{
    FILE *f = std::fopen(path, "wb");
    std::fwrite(px.data(), sizeof(float), px.size(), f);
    std::fclose(f);
}

int main()
{
    struct pl_log_params lp = pl_log_default_params; lp.log_cb = pl_log_simple;
    lp.log_level = PL_LOG_WARN;
    pl_log log = pl_log_create(PL_API_VER, &lp);
    struct pl_vulkan_params vp = pl_vulkan_default_params;
    vp.allow_software = true;
    pl_vulkan vk = pl_vulkan_create(log, &vp);
    if (!vk) { std::puts("no vulkan"); return 1; }
    pl_gpu gpu = vk->gpu;

    std::vector<float> A, B, M;
    scene(0.f, A); scene(1.f, B); scene(0.5f, M);
    pl_tex ta = upload(gpu, A), tb = upload(gpu, B);
    pl_frame fa = as_frame(ta), fb = as_frame(tb);

    FrameGen fg(log, gpu);
    if (!fg.ok()) { std::puts("framegen init failed"); return 1; }

    pl_frame out;
    auto t0 = std::chrono::steady_clock::now();
    if (!fg.interpolate(&fa, 1, &fb, 2, 0.5f, &out)) { std::puts("interpolate failed"); return 1; }
    pl_gpu_finish(gpu);
    auto t1 = std::chrono::steady_clock::now();
    // second call reuses cached flow: only the warp pass runs
    if (!fg.interpolate(&fa, 1, &fb, 2, 0.5f, &out)) { std::puts("interpolate2 failed"); return 1; }
    pl_gpu_finish(gpu);
    auto t2 = std::chrono::steady_clock::now();

#ifdef FRAMEGEN_DEBUG
    dump_flow(gpu, fg);
#endif
    std::vector<float> I;
    if (!download(gpu, log, &out, I)) { std::puts("download failed"); return 1; }

    {
        pl_frame o0; std::vector<float> I0;
        fg.interpolate(&fa, 1, &fb, 2, 0.0f, &o0); download(gpu, log, &o0, I0);
        std::printf("t=0 vs A: %.2f dB\n", psnr(I0, A));
        // flipped check
        std::vector<float> Af(A.size());
        for (int y = 0; y < H; y++) for (int x = 0; x < W*4; x++) Af[y*W*4+x] = A[(H-1-y)*W*4+x];
        std::printf("t=0 vs A flipped: %.2f dB\n", psnr(I0, Af));
    }
    std::vector<float> blend(A.size());
    for (size_t i = 0; i < A.size(); i++) blend[i] = 0.5f * (A[i] + B[i]);

    const int m = 32; // ignore border where content enters/leaves
    std::printf("full frame PSNR (dB, higher=better)\n");
    std::printf("  nearest frame : %.2f\n", psnr(A, M, m, m, W - m, H - m));
    std::printf("  50/50 blend   : %.2f\n", psnr(blend, M, m, m, W - m, H - m));
    std::printf("  framegen      : %.2f\n", psnr(I, M, m, m, W - m, H - m));
    std::printf("moving box region\n");
    std::printf("  blend %.2f  framegen %.2f\n", psnr(blend, M, 860, 400, 1100, 600), psnr(I, M, 860, 400, 1100, 600));
    std::printf("box interior  blend %.2f  nearest %.2f  framegen %.2f\n", psnr(blend, M, 900, 440, 1060, 560), psnr(A, M, 900, 440, 1060, 560), psnr(I, M, 900, 440, 1060, 560));
    std::printf("hud interior  framegen %.2f\n", psnr(I, M, 72, 970, 348, 1020));
    std::printf("bg only       framegen %.2f\n", psnr(I, M, 1300, 100, 1800, 350));
    std::printf("static HUD region\n");
    std::printf("  blend %.2f  framegen %.2f\n", psnr(blend, M, 60, 960, 360, 1030), psnr(I, M, 60, 960, 360, 1030));
    std::printf("cpu-vulkan time: first %.0f ms, cached %.0f ms\n",
                std::chrono::duration<double, std::milli>(t1 - t0).count(),
                std::chrono::duration<double, std::milli>(t2 - t1).count());

    save_raw("A.raw", A); save_raw("B.raw", B); save_raw("M.raw", M); save_raw("I.raw", I); save_raw("blend.raw", blend);
    pl_tex_destroy(gpu, &ta); pl_tex_destroy(gpu, &tb);
    return 0;
}
