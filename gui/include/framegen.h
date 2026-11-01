#ifndef CHIAKI_GUI_FRAMEGEN_H
#define CHIAKI_GUI_FRAMEGEN_H

#include <libplacebo/renderer.h>
#include <libplacebo/vulkan.h>

#include <memory>
#include <vector>

// Frame generation (interpolation) for the video stream on libplacebo's
// Vulkan device. Each real frame is rendered to an RGBA8 texture at display
// size and handed to a backend, which interpolates between it and the
// previous real frame. Video has no motion vectors or depth, so the backends
// get zero motion vectors and a constant depth and rely on their own optical
// flow.
//
// Backends (each only functional when compiled in):
//  - DLSS Frame Generation through NGX (CHIAKI_HAVE_NGX), RTX 40 series or
//    newer, up to 3 generated frames per real frame on RTX 50 series.
//  - AMD FidelityFX FSR 3.1 frame generation (CHIAKI_HAVE_FFX), any GPU,
//    1 generated frame per real frame.
class FrameGen
{
public:
    enum class Backend {
        None = 0,
        Dlss = 1,
        Fsr = 2,
    };

    static bool compiledIn();
    static bool backendCompiledIn(Backend backend);
    static const char *backendName(Backend backend);

    explicit FrameGen(pl_log log);
    ~FrameGen();
    FrameGen(const FrameGen &) = delete;
    FrameGen &operator=(const FrameGen &) = delete;

    // Lazily initializes `backend`; switching to a different backend tears the
    // previous one down first. After a failure the same backend is not retried.
    bool ensureInitialized(pl_vulkan vk, Backend backend);
    Backend backend() const;
    // Whether `backend` failed to start or crashed and is not retried.
    bool backendFailed(Backend backend) const;

    // Highest output multiplier the active backend supports (2 = one
    // generated frame per real frame).
    int maxMultiplier() const;

    // Average GPU time of the backend's generation work per stream frame in
    // milliseconds (0 if unknown).
    float gpuTimeMs() const;

    // The next frame starts a new sequence: nothing is interpolated across it.
    void reset();

    // Waits for in-flight work and releases every Vulkan and backend resource.
    // Must be called before the pl_vulkan device is destroyed.
    void destroy();

    // Renders `src` at `out_w` x `out_h` and generates `multiplier - 1` frames
    // between the previous real frame and this one. `outputs` receives the
    // frames in presentation order: generated frames first, the real frame
    // last; without a previous frame only the real frame is returned. The
    // textures stay valid until `process` has been called twice more.
    bool process(const struct pl_frame *src, int out_w, int out_h, int multiplier,
                 float frame_interval_ms, std::vector<struct pl_frame> &outputs);

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

#endif // CHIAKI_GUI_FRAMEGEN_H
