#ifndef CHIAKI_GUI_RTXVSR_H
#define CHIAKI_GUI_RTXVSR_H

#include <libplacebo/renderer.h>
#include <libplacebo/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

// NVIDIA RTX Video Super Resolution (VSR) running on libplacebo's Vulkan device
// through NGX. Only functional in builds configured with CHIAKI_ENABLE_NGX
// (Windows + NVIDIA NGX SDK); otherwise every call is a cheap no-op that
// reports VSR as unavailable so callers can fall back to libplacebo scaling.
class RtxVsr
{
public:
    static bool compiledIn();

    // RTX VSR modes, passed to NGX as VSR.QualityLevel. 1-4 upscale (any
    // nvngx_vsr.dll); the artifact reduction (8-11, output at input size) and
    // streaming modes (21/23, upscale + artifact cleanup, RTX 30 series or
    // newer) need nvngx_vsr.dll 1.8.2 / 1.8.3 or newer (NVIDIA VFX SDK 1.2/1.3).
    static bool isValidQuality(int quality);
    // Whether the mode keeps the input resolution instead of upscaling.
    static bool keepsResolution(int quality);
    // Short human readable name of a mode, e.g. "High" or "AR Medium".
    static const char *qualityName(int quality);

    // Appends the Vulkan instance/device extensions NGX needs. They must be
    // requested as *optional* extensions before the instance/device is created.
    static void appendRequiredExtensions(std::vector<const char *> &instance_exts,
                                         std::vector<const char *> &device_exts);

    explicit RtxVsr(pl_log log);
    ~RtxVsr();
    RtxVsr(const RtxVsr &) = delete;
    RtxVsr &operator=(const RtxVsr &) = delete;

    // Lazily initializes NGX and the VSR feature. After a failure this keeps
    // returning false without retrying, so it is cheap to call every frame.
    bool ensureInitialized(pl_vulkan vk);
    bool failed() const;
    // Whether `quality` cannot be used (rejected by the installed DLL or GPU).
    bool modeFailed(int quality) const;
    // Average GPU time of the VSR evaluation in milliseconds (0 if unknown).
    float gpuTimeMs() const;

    // Waits for in-flight work and releases every Vulkan/NGX resource. Must be
    // called before the pl_vulkan device is destroyed.
    void destroy();

    // Upscales `src` to `out_w` x `out_h` (or cleans it up at its own size for
    // modes that keep the resolution). On success `out_frame` describes an RGB
    // texture owned by this object, valid at least until `process` is called
    // for more distinct frames than the internal cache holds. `quality` is one
    // of the modes accepted by isValidQuality().
    bool process(const struct pl_frame *src, uint64_t signature,
                 int out_w, int out_h, int quality,
                 struct pl_frame *out_frame, uint64_t *out_signature);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif // CHIAKI_GUI_RTXVSR_H
