#include "framegen.h"
#include "ngxcore.h"
#include "gputimer.h"
#include "vkguard.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui);

#if defined(CHIAKI_HAVE_NGX) || defined(CHIAKI_HAVE_FFX)
#define CHIAKI_HAVE_FRAMEGEN 1
#endif

#if defined(CHIAKI_HAVE_FRAMEGEN)

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>

#include <vulkan/vulkan.h>

#if defined(CHIAKI_HAVE_NGX)
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_dlssg_vk.h>
#endif

#if defined(CHIAKI_HAVE_FFX)
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/vk/ffx_api_vk.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>

namespace {

constexpr VkImageLayout kLayout = VK_IMAGE_LAYOUT_GENERAL;
// Output textures of a call stay untouched for this many calls; the real frame
// is presented last, up to one stream frame interval after its call.
constexpr size_t kFrameSets = 3;
constexpr int kMaxGenerated = 3;
constexpr size_t kCommandSlots = 4;
constexpr uint64_t kWaitTimeoutNs = 2000000000ull;
// Video has no depth; a constant mid-range value keeps disocclusion logic neutral.
constexpr float kConstantDepth = 0.5f;
constexpr int kMaxConsecutiveFailures = 3;

} // namespace

struct FgImage
{
    pl_tex tex = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    int w = 0;
    int h = 0;
    // Signalled by libplacebo when the image is handed to us (pl_vulkan_hold_ex).
    VkSemaphore sem = VK_NULL_HANDLE;
    uint64_t value = 0;
};

class FgBackendImpl
{
public:
    virtual ~FgBackendImpl() = default;
    virtual bool init(FrameGen::Impl &fg) = 0;
    virtual int maxGenerated() const = 0;
    // Records the generation of `count` frames between the `color` of the
    // previous call and this one into `outputs`.
    virtual bool record(FrameGen::Impl &fg, VkCommandBuffer cmd, FgImage &color,
                        FgImage *const *outputs, int count, bool reset, float frame_interval_ms) = 0;
    // Drops size dependent state; called while no generation work is pending.
    virtual void invalidate() = 0;
    virtual void shutdown() = 0;
};

struct FrameGen::Impl
{
    struct FrameSet
    {
        FgImage real;
        std::array<FgImage, kMaxGenerated> generated;
    };

    struct CommandSlot
    {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        uint64_t done_value = 0;
    };

    pl_log log = nullptr;
    pl_vulkan vk = nullptr;
    pl_gpu gpu = nullptr;
    pl_renderer renderer = nullptr;
    pl_fmt fmt_color = nullptr;
    pl_fmt fmt_mvec = nullptr;
    pl_fmt fmt_depth = nullptr;
    bool common_ready = false;
    bool hdr_warned = false;

    FrameGen::Backend backend = FrameGen::Backend::None;
    std::unique_ptr<FgBackendImpl> backend_impl;
    std::array<bool, 3> backend_failed = {};
    int consecutive_failures = 0;

    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::array<CommandSlot, kCommandSlots> cmd_slots = {};
    size_t next_slot = 0;
    VkGpuTimer timer;
    VkSemaphore sem_done = VK_NULL_HANDLE;
    uint64_t done_value = 0;

    std::array<FrameSet, kFrameSets> sets;
    size_t next_set = 0;
    FgImage mvec;
    FgImage depth;
    int width = 0;
    int height = 0;
    bool have_history = false;
    bool reset_pending = true;
    // The first calls after a backend starts are logged step by step.
    int trace_frames = 0;

    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
    PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
    PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
    PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
    PFN_vkQueueSubmit vkQueueSubmit = nullptr;
    PFN_vkWaitSemaphores vkWaitSemaphores = nullptr;
    PFN_vkCreateImageView vkCreateImageView = nullptr;
    PFN_vkDestroyImageView vkDestroyImageView = nullptr;

    bool initCommon(pl_vulkan vk);
    void shutdownCommon();
    void destroyBackend();
    bool loadFunctions();
    bool waitDone(uint64_t value);
    bool ensureImage(FgImage &img, int w, int h, pl_fmt fmt, bool renderable, bool clearable);
    void destroyImage(FgImage &img);
    void destroyImages();
    bool ensureSize(int w, int h, const std::function<void(const char *)> &trace);
    bool fillConstant(FgImage &img, const float value[4], const void *texel, size_t texel_size);
    bool hold(FgImage &img);
    void release(FgImage &img, VkSemaphore sem, uint64_t value);
    bool submit(VkCommandBuffer cmd, FgImage *const *waits, size_t num_waits);
    static void fillFrame(const FgImage &img, struct pl_frame *frame);
};

bool FrameGen::Impl::loadFunctions()
{
    vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        vk->get_proc_addr(vk->instance, "vkGetDeviceProcAddr"));
    if (!vkGetDeviceProcAddr)
        return false;
#define LOAD_DEVICE_PROC(name_) \
    name_ = reinterpret_cast<PFN_##name_>(vkGetDeviceProcAddr(vk->device, #name_)); \
    if (!name_) { \
        qCWarning(chiakiGui) << "[framegen] Missing Vulkan function" << #name_; \
        return false; \
    }
    LOAD_DEVICE_PROC(vkGetDeviceQueue)
    LOAD_DEVICE_PROC(vkCreateCommandPool)
    LOAD_DEVICE_PROC(vkDestroyCommandPool)
    LOAD_DEVICE_PROC(vkAllocateCommandBuffers)
    LOAD_DEVICE_PROC(vkBeginCommandBuffer)
    LOAD_DEVICE_PROC(vkEndCommandBuffer)
    LOAD_DEVICE_PROC(vkResetCommandBuffer)
    LOAD_DEVICE_PROC(vkQueueSubmit)
    LOAD_DEVICE_PROC(vkWaitSemaphores)
    LOAD_DEVICE_PROC(vkCreateImageView)
    LOAD_DEVICE_PROC(vkDestroyImageView)
#undef LOAD_DEVICE_PROC
    return true;
}

bool FrameGen::Impl::initCommon(pl_vulkan vk_)
{
    if (common_ready)
        return true;
    vk = vk_;
    gpu = vk->gpu;
    if (!loadFunctions())
        return false;

    const auto find_fmt = [this](const char *name, enum pl_fmt_caps caps) -> pl_fmt {
        pl_fmt fmt = pl_find_named_fmt(gpu, name);
        if (!fmt || (fmt->caps & caps) != caps) {
            qCWarning(chiakiGui) << "[framegen] Texture format" << name << "lacks the required capabilities";
            return nullptr;
        }
        return fmt;
    };
    const auto sampled_storage = static_cast<enum pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_STORABLE);
    fmt_color = find_fmt("rgba8", static_cast<enum pl_fmt_caps>(sampled_storage | PL_FMT_CAP_RENDERABLE));
    fmt_mvec = find_fmt("rg16f", sampled_storage);
    fmt_depth = find_fmt("r32f", sampled_storage);
    if (!fmt_color || !fmt_mvec || !fmt_depth)
        return false;

    queue_family = vk->queue_graphics.index;
    vkGetDeviceQueue(vk->device, queue_family, 0, &queue);
    if (!queue)
        return false;

    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family;
    if (vkCreateCommandPool(vk->device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
        pool = VK_NULL_HANDLE;
        return false;
    }
    std::array<VkCommandBuffer, kCommandSlots> cmds = {};
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = static_cast<uint32_t>(cmds.size());
    if (vkAllocateCommandBuffers(vk->device, &alloc_info, cmds.data()) != VK_SUCCESS)
        return false;
    for (size_t i = 0; i < cmds.size(); i++)
        cmd_slots[i].cmd = cmds[i];

    struct pl_vulkan_sem_params sem_params = {};
    sem_params.type = VK_SEMAPHORE_TYPE_TIMELINE;
    sem_done = pl_vulkan_sem_create(gpu, &sem_params);
    if (!sem_done)
        return false;

    renderer = pl_renderer_create(log, gpu);
    if (!renderer)
        return false;
    // Timing is optional; without it the overlay just shows no GPU time.
    timer.init(vk, vkGetDeviceProcAddr, static_cast<uint32_t>(cmd_slots.size()));
    common_ready = true;
    return true;
}

bool FrameGen::Impl::waitDone(uint64_t value)
{
    if (!sem_done || value == 0)
        return true;
    VkSemaphoreWaitInfo wait_info = {};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores = &sem_done;
    wait_info.pValues = &value;
    const VkResult res = vkWaitSemaphores(vk->device, &wait_info, kWaitTimeoutNs);
    if (res != VK_SUCCESS) {
        qCWarning(chiakiGui) << "[framegen] Waiting for frame generation work failed, result" << static_cast<int>(res);
        return false;
    }
    return true;
}

bool FrameGen::Impl::ensureImage(FgImage &img, int w, int h, pl_fmt fmt, bool renderable, bool clearable)
{
    if (img.tex && img.w == w && img.h == h)
        return true;
    destroyImage(img);

    struct pl_tex_params tex_params = {};
    tex_params.w = w;
    tex_params.h = h;
    tex_params.format = fmt;
    tex_params.sampleable = true;
    tex_params.storable = true;
    tex_params.renderable = renderable;
    // Constant inputs are filled on the GPU (clear or buffer copy) instead of a
    // host upload: host image copies of large float images stalled the render
    // thread on some drivers.
    tex_params.blit_dst = clearable && (fmt->caps & PL_FMT_CAP_BLITTABLE);
    img.tex = pl_tex_create(gpu, &tex_params);
    if (!img.tex)
        return false;
    img.image = pl_vulkan_unwrap(gpu, img.tex, &img.format, nullptr);
    if (!img.image) {
        destroyImage(img);
        return false;
    }

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = img.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = img.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk->device, &view_info, nullptr, &img.view) != VK_SUCCESS) {
        img.view = VK_NULL_HANDLE;
        destroyImage(img);
        return false;
    }

    struct pl_vulkan_sem_params sem_params = {};
    sem_params.type = VK_SEMAPHORE_TYPE_TIMELINE;
    img.sem = pl_vulkan_sem_create(gpu, &sem_params);
    if (!img.sem) {
        destroyImage(img);
        return false;
    }
    img.w = w;
    img.h = h;
    return true;
}

void FrameGen::Impl::destroyImage(FgImage &img)
{
    // Callers make sure no generation work referencing the image is pending.
    if (img.view)
        vkDestroyImageView(vk->device, img.view, nullptr);
    if (img.tex)
        pl_tex_destroy(gpu, &img.tex);
    if (img.sem)
        pl_vulkan_sem_destroy(gpu, &img.sem);
    img = FgImage();
}

void FrameGen::Impl::destroyImages()
{
    for (auto &set : sets) {
        destroyImage(set.real);
        for (auto &img : set.generated)
            destroyImage(img);
    }
    destroyImage(mvec);
    destroyImage(depth);
    width = height = 0;
}

bool FrameGen::Impl::fillConstant(FgImage &img, const float value[4], const void *texel, size_t texel_size)
{
    if (img.tex->params.blit_dst) {
        union pl_clear_color color = {};
        for (int i = 0; i < 4; i++)
            color.f[i] = value[i];
        pl_tex_clear_ex(gpu, img.tex, color);
        return true;
    }
    // Fallback: copy from a device buffer through the command queue.
    std::vector<uint8_t> data(static_cast<size_t>(img.w) * img.h * texel_size);
    for (size_t off = 0; off < data.size(); off += texel_size)
        std::copy_n(static_cast<const uint8_t *>(texel), texel_size, data.begin() + off);
    struct pl_buf_params buf_params = {};
    buf_params.size = data.size();
    buf_params.initial_data = data.data();
    pl_buf buf = pl_buf_create(gpu, &buf_params);
    if (!buf)
        return false;
    struct pl_tex_transfer_params transfer = {};
    transfer.tex = img.tex;
    transfer.buf = buf;
    const bool ok = pl_tex_upload(gpu, &transfer);
    pl_buf_destroy(gpu, &buf);
    return ok;
}

bool FrameGen::Impl::ensureSize(int w, int h, const std::function<void(const char *)> &trace)
{
    if (w == width && h == height && mvec.tex)
        return true;
    if (!waitDone(done_value))
        return false;
    if (backend_impl)
        backend_impl->invalidate();
    destroyImages();

    for (auto &set : sets) {
        if (!ensureImage(set.real, w, h, fmt_color, true, false))
            return false;
        for (auto &img : set.generated) {
            if (!ensureImage(img, w, h, fmt_color, true, false))
                return false;
        }
    }
    trace("color textures created");
    if (!ensureImage(mvec, w, h, fmt_mvec, false, true) || !ensureImage(depth, w, h, fmt_depth, false, true))
        return false;
    trace(mvec.tex->params.blit_dst && depth.tex->params.blit_dst
              ? "motion/depth textures created (gpu clear)"
              : "motion/depth textures created (buffer copy)");
    const float zero_value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float depth_clear[4] = { kConstantDepth, 0.0f, 0.0f, 0.0f };
    const uint16_t zero_mvec[2] = { 0, 0 };
    const float depth_value = kConstantDepth;
    if (!fillConstant(mvec, zero_value, zero_mvec, sizeof(zero_mvec)))
        return false;
    trace("motion vectors filled");
    if (!fillConstant(depth, depth_clear, &depth_value, sizeof(depth_value)))
        return false;
    trace("depth filled");

    width = w;
    height = h;
    have_history = false;
    return true;
}

bool FrameGen::Impl::hold(FgImage &img)
{
    struct pl_vulkan_hold_params hold_params = {};
    hold_params.tex = img.tex;
    hold_params.layout = kLayout;
    hold_params.qf = VK_QUEUE_FAMILY_IGNORED;
    hold_params.semaphore = { img.sem, img.value + 1 };
    if (!pl_vulkan_hold_ex(gpu, &hold_params))
        return false;
    img.value++;
    return true;
}

void FrameGen::Impl::release(FgImage &img, VkSemaphore sem, uint64_t value)
{
    struct pl_vulkan_release_params release_params = {};
    release_params.tex = img.tex;
    release_params.layout = kLayout;
    release_params.qf = VK_QUEUE_FAMILY_IGNORED;
    release_params.semaphore = { sem, value };
    pl_vulkan_release_ex(gpu, &release_params);
}

bool FrameGen::Impl::submit(VkCommandBuffer cmd, FgImage *const *waits, size_t num_waits)
{
    std::vector<VkSemaphore> wait_sems(num_waits);
    std::vector<uint64_t> wait_values(num_waits);
    std::vector<VkPipelineStageFlags> wait_stages(num_waits, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    for (size_t i = 0; i < num_waits; i++) {
        wait_sems[i] = waits[i]->sem;
        wait_values[i] = waits[i]->value;
    }
    const uint64_t signal_value = done_value + 1;

    VkTimelineSemaphoreSubmitInfo timeline_info = {};
    timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.waitSemaphoreValueCount = static_cast<uint32_t>(num_waits);
    timeline_info.pWaitSemaphoreValues = wait_values.data();
    timeline_info.signalSemaphoreValueCount = 1;
    timeline_info.pSignalSemaphoreValues = &signal_value;

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = &timeline_info;
    submit_info.waitSemaphoreCount = static_cast<uint32_t>(num_waits);
    submit_info.pWaitSemaphores = wait_sems.data();
    submit_info.pWaitDstStageMask = wait_stages.data();
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &sem_done;

    vk->lock_queue(vk, queue_family, 0);
    const VkResult res = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vk->unlock_queue(vk, queue_family, 0);
    if (res != VK_SUCCESS) {
        qCWarning(chiakiGui) << "[framegen] vkQueueSubmit failed, result" << static_cast<int>(res);
        return false;
    }
    done_value = signal_value;
    return true;
}

void FrameGen::Impl::fillFrame(const FgImage &img, struct pl_frame *frame)
{
    *frame = {};
    frame->num_planes = 1;
    frame->planes[0].texture = img.tex;
    frame->planes[0].components = 3;
    frame->planes[0].component_mapping[0] = PL_CHANNEL_R;
    frame->planes[0].component_mapping[1] = PL_CHANNEL_G;
    frame->planes[0].component_mapping[2] = PL_CHANNEL_B;
    frame->repr = pl_color_repr_rgb;
    frame->color = pl_color_space_bt709;
    frame->crop = { 0.0f, 0.0f, static_cast<float>(img.w), static_cast<float>(img.h) };
}

void FrameGen::Impl::destroyBackend()
{
    if (backend_impl) {
        waitDone(done_value);
        backend_impl->shutdown();
        backend_impl.reset();
    }
    backend = FrameGen::Backend::None;
    have_history = false;
}

void FrameGen::Impl::shutdownCommon()
{
    if (!vk)
        return;
    if (vkWaitSemaphores)
        waitDone(done_value);
    destroyBackend();
    if (vkDestroyImageView)
        destroyImages();
    if (pool)
        vkDestroyCommandPool(vk->device, pool, nullptr);
    pool = VK_NULL_HANDLE;
    cmd_slots = {};
    if (sem_done)
        pl_vulkan_sem_destroy(gpu, &sem_done);
    timer.destroy();
    pl_renderer_destroy(&renderer);
    done_value = 0;
    common_ready = false;
    vk = nullptr;
    gpu = nullptr;
}

#if defined(CHIAKI_HAVE_NGX)

namespace {

NVSDK_NGX_Resource_VK makeNgxResource(const FgImage &img, bool read_write)
{
    NVSDK_NGX_Resource_VK res = {};
    res.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    res.ReadWrite = read_write;
    res.Resource.ImageViewInfo.ImageView = img.view;
    res.Resource.ImageViewInfo.Image = img.image;
    res.Resource.ImageViewInfo.SubresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    res.Resource.ImageViewInfo.SubresourceRange.levelCount = 1;
    res.Resource.ImageViewInfo.SubresourceRange.layerCount = 1;
    res.Resource.ImageViewInfo.Format = img.format;
    res.Resource.ImageViewInfo.Width = static_cast<unsigned int>(img.w);
    res.Resource.ImageViewInfo.Height = static_cast<unsigned int>(img.h);
    return res;
}

// DLSS Frame Generation through NGX. Its runtime is nvngx_dlssg.dll from the
// public DLSS SDK, looked up next to chiaki.exe.
class DlssBackend : public FgBackendImpl
{
public:
    bool init(FrameGen::Impl &fg) override
    {
        if (!NgxCore::acquire(fg.vk))
            return false;
        ngx_acquired = true;

        NVSDK_NGX_Parameter *caps = nullptr;
        NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&caps);
        if (NVSDK_NGX_FAILED(result) || !caps) {
            qCWarning(chiakiGui) << "[framegen] DLSS capability query failed:" << NgxCore::resultString(result);
            return false;
        }
        unsigned int available = 0;
        if (NVSDK_NGX_FAILED(NVSDK_NGX_Parameter_GetUI(caps, NVSDK_NGX_Parameter_FrameGeneration_Available, &available))) {
            int available_i = 0;
            NVSDK_NGX_Parameter_GetI(caps, NVSDK_NGX_Parameter_FrameGeneration_Available, &available_i);
            available = available_i > 0 ? 1 : 0;
        }
        if (!available) {
            int needs_driver = 0;
            NVSDK_NGX_Parameter_GetI(caps, NVSDK_NGX_Parameter_FrameGeneration_NeedsUpdatedDriver, &needs_driver);
            qCWarning(chiakiGui) << "[framegen] DLSS Frame Generation is not available on this GPU/driver"
                                 << (needs_driver ? "(driver update required)" : "(RTX 40 series or newer and nvngx_dlssg.dll are required)");
            NVSDK_NGX_VULKAN_DestroyParameters(caps);
            return false;
        }
        unsigned int multi_frame_max = 0;
        if (NVSDK_NGX_SUCCEED(NVSDK_NGX_Parameter_GetUI(caps, NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &multi_frame_max))
            && multi_frame_max > 0)
            max_generated = std::min(static_cast<int>(multi_frame_max), kMaxGenerated);
        NVSDK_NGX_VULKAN_DestroyParameters(caps);

        result = NVSDK_NGX_VULKAN_AllocateParameters(&params);
        if (NVSDK_NGX_FAILED(result) || !params) {
            params = nullptr;
            return false;
        }
        qCInfo(chiakiGui) << "[framegen] DLSS Frame Generation available, up to" << max_generated << "generated frames";
        return true;
    }

    int maxGenerated() const override { return max_generated; }

    bool record(FrameGen::Impl &fg, VkCommandBuffer cmd, FgImage &color,
                FgImage *const *outputs, int count, bool reset, float frame_interval_ms) override
    {
        (void)frame_interval_ms;
        if (!handle || width != color.w || height != color.h) {
            if (handle)
                NVSDK_NGX_VULKAN_ReleaseFeature(handle);
            handle = nullptr;
            NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_DLSSG_Parameter_ResourceAlwaysProvided_Flags,
                NVSDK_NGX_DLSSG_ResourceFlags_Backbuffer | NVSDK_NGX_DLSSG_ResourceFlags_MVecs
                | NVSDK_NGX_DLSSG_ResourceFlags_Depth | NVSDK_NGX_DLSSG_ResourceFlags_OutputInterpolated);
            NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_DLSSG_Parameter_ResourceNeverProvided_Flags,
                NVSDK_NGX_DLSSG_ResourceFlags_HUDLess | NVSDK_NGX_DLSSG_ResourceFlags_UI
                | NVSDK_NGX_DLSSG_ResourceFlags_UIAlpha | NVSDK_NGX_DLSSG_ResourceFlags_BidirectionalDistortionField);
            NVSDK_NGX_DLSSG_Create_Params create_params = {};
            create_params.Width = static_cast<unsigned int>(color.w);
            create_params.Height = static_cast<unsigned int>(color.h);
            create_params.NativeBackbufferFormat = static_cast<unsigned int>(color.format);
            create_params.RenderWidth = create_params.Width;
            create_params.RenderHeight = create_params.Height;
            create_params.DynamicResolutionScaling = false;
            const NVSDK_NGX_Result result = NGX_VK_CREATE_DLSSG(cmd, 1, 1, &handle, params, &create_params);
            if (NVSDK_NGX_FAILED(result) || !handle) {
                qCWarning(chiakiGui).nospace() << "[framegen] Creating DLSS Frame Generation failed: "
                    << NgxCore::resultString(result) << " (0x" << Qt::hex << static_cast<unsigned>(result) << ")";
                handle = nullptr;
                return false;
            }
            width = color.w;
            height = color.h;
            reset = true;
        }

        NVSDK_NGX_Resource_VK color_res = makeNgxResource(color, false);
        NVSDK_NGX_Resource_VK mvec_res = makeNgxResource(fg.mvec, false);
        NVSDK_NGX_Resource_VK depth_res = makeNgxResource(fg.depth, false);
        NVSDK_NGX_Parameter_SetULL(params, NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID, ++frame_id);

        for (int i = 0; i < count; i++) {
            NVSDK_NGX_Resource_VK out_res = makeNgxResource(*outputs[i], true);
            NVSDK_NGX_VK_DLSSG_Eval_Params eval_params = {};
            eval_params.pBackbuffer = &color_res;
            eval_params.pDepth = &depth_res;
            eval_params.pMVecs = &mvec_res;
            eval_params.pOutputInterpFrame = &out_res;

            NVSDK_NGX_DLSSG_Opt_Eval_Params opt = {};
            // The count excludes the real frame; indices are one based.
            opt.multiFrameCount = static_cast<unsigned int>(count);
            opt.multiFrameIndex = static_cast<unsigned int>(i + 1);
            fillCamera(opt, color.w, color.h);
            opt.mvecScale[0] = 1.0f;
            opt.mvecScale[1] = 1.0f;
            opt.colorBuffersHDR = false;
            opt.depthInverted = false;
            opt.cameraMotionIncluded = true;
            opt.reset = reset;
            opt.motionVectorsInvalidValue = 3.402823466e38f;
            const NVSDK_NGX_Dimensions size = { static_cast<unsigned int>(color.w), static_cast<unsigned int>(color.h) };
            opt.backbufferSubrectSize = size;
            opt.outputInterpSubrectSize = size;
            opt.mvecsSubrectSize = size;
            opt.depthSubrectSize = size;

            const NVSDK_NGX_Result result = NGX_VK_EVALUATE_DLSSG(cmd, handle, params, &eval_params, &opt);
            if (NVSDK_NGX_FAILED(result)) {
                qCWarning(chiakiGui).nospace() << "[framegen] Evaluating DLSS Frame Generation failed: "
                    << NgxCore::resultString(result) << " (0x" << Qt::hex << static_cast<unsigned>(result) << ")";
                return false;
            }
        }
        return true;
    }

    void invalidate() override
    {
        if (handle)
            NVSDK_NGX_VULKAN_ReleaseFeature(handle);
        handle = nullptr;
        width = height = 0;
    }

    void shutdown() override
    {
        invalidate();
        if (params)
            NVSDK_NGX_VULKAN_DestroyParameters(params);
        params = nullptr;
        if (ngx_acquired)
            NgxCore::release();
        ngx_acquired = false;
    }

private:
    // A stationary virtual camera: video carries no camera, but the
    // projection matrices and the camera basis still have to agree.
    static void fillCamera(NVSDK_NGX_DLSSG_Opt_Eval_Params &opt, int w, int h)
    {
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                const float v = r == c ? 1.0f : 0.0f;
                opt.cameraViewToClip[r][c] = v;
                opt.clipToCameraView[r][c] = v;
                opt.clipToLensClip[r][c] = v;
                opt.clipToPrevClip[r][c] = v;
                opt.prevClipToClip[r][c] = v;
            }
        }
        opt.cameraNear = 0.1f;
        opt.cameraFar = 1000.0f;
        opt.cameraFOV = 1.5707963f; // pi/2, so tan(fov / 2) = 1
        opt.cameraAspectRatio = static_cast<float>(w) / static_cast<float>(h);
        opt.cameraUp[1] = 1.0f;
        opt.cameraRight[0] = 1.0f;
        opt.cameraFwd[2] = 1.0f;
        const float proj_x = 1.0f / opt.cameraAspectRatio;
        const float proj_z = opt.cameraFar / (opt.cameraFar - opt.cameraNear);
        const float proj_w = -opt.cameraNear * proj_z;
        opt.cameraViewToClip[0][0] = proj_x;
        opt.cameraViewToClip[2][2] = proj_z;
        opt.cameraViewToClip[2][3] = 1.0f;
        opt.cameraViewToClip[3][2] = proj_w;
        opt.cameraViewToClip[3][3] = 0.0f;
        opt.clipToCameraView[0][0] = 1.0f / proj_x;
        opt.clipToCameraView[2][2] = 0.0f;
        opt.clipToCameraView[2][3] = 1.0f / proj_w;
        opt.clipToCameraView[3][2] = 1.0f;
        opt.clipToCameraView[3][3] = -proj_z / proj_w;
    }

    bool ngx_acquired = false;
    NVSDK_NGX_Parameter *params = nullptr;
    NVSDK_NGX_Handle *handle = nullptr;
    int max_generated = 1;
    int width = 0;
    int height = 0;
    unsigned long long frame_id = 0;
};

} // namespace

#endif // CHIAKI_HAVE_NGX

#if defined(CHIAKI_HAVE_FFX)

namespace {

const char *ffxResultString(ffxReturnCode_t code)
{
    switch (code) {
    case FFX_API_RETURN_OK: return "OK";
    case FFX_API_RETURN_ERROR: return "ERROR";
    case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "ERROR_UNKNOWN_DESCTYPE";
    case FFX_API_RETURN_ERROR_RUNTIME_ERROR: return "ERROR_RUNTIME_ERROR";
    case FFX_API_RETURN_NO_PROVIDER: return "NO_PROVIDER";
    case FFX_API_RETURN_ERROR_MEMORY: return "ERROR_MEMORY";
    case FFX_API_RETURN_ERROR_PARAMETER: return "ERROR_PARAMETER";
    default: return "OTHER";
    }
}

struct FfxApiResource makeFfxResource(const FgImage &img, uint32_t format, bool read_write)
{
    struct FfxApiResource res = {};
    res.resource = reinterpret_cast<void *>(img.image);
    res.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    res.description.format = format;
    res.description.width = static_cast<uint32_t>(img.w);
    res.description.height = static_cast<uint32_t>(img.h);
    res.description.depth = 1;
    res.description.mipCount = 1;
    res.description.flags = FFX_API_RESOURCE_FLAGS_NONE;
    res.description.usage = read_write ? FFX_API_RESOURCE_USAGE_UAV : FFX_API_RESOURCE_USAGE_READ_ONLY;
    // COMMON maps to VK_IMAGE_LAYOUT_GENERAL in the FidelityFX Vulkan backend.
    res.state = FFX_API_RESOURCE_STATE_COMMON;
    return res;
}

// AMD FidelityFX FSR 3.1 frame generation, used without its swapchain
// replacement. Its runtime is the signed amd_fidelityfx_vk.dll from the
// FidelityFX SDK, loaded from next to chiaki.exe.
class FsrBackend : public FgBackendImpl
{
public:
    bool init(FrameGen::Impl &fg) override
    {
        vk = fg.vk;
        const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("amd_fidelityfx_vk.dll"));
#if defined(_WIN32)
        module = LoadLibraryW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()));
#else
        module = dlopen(path.toUtf8().constData(), RTLD_NOW);
#endif
        if (!module) {
            qCWarning(chiakiGui) << "[framegen] Could not load" << path;
            return false;
        }
        create_context = reinterpret_cast<PfnFfxCreateContext>(symbol("ffxCreateContext"));
        destroy_context = reinterpret_cast<PfnFfxDestroyContext>(symbol("ffxDestroyContext"));
        configure = reinterpret_cast<PfnFfxConfigure>(symbol("ffxConfigure"));
        dispatch = reinterpret_cast<PfnFfxDispatch>(symbol("ffxDispatch"));
        if (!create_context || !destroy_context || !configure || !dispatch) {
            qCWarning(chiakiGui) << "[framegen] amd_fidelityfx_vk.dll is missing FFX API entry points";
            return false;
        }
        qCInfo(chiakiGui) << "[framegen] FSR 3.1 Frame Generation runtime loaded";
        return true;
    }

    int maxGenerated() const override { return 1; }

    bool record(FrameGen::Impl &fg, VkCommandBuffer cmd, FgImage &color,
                FgImage *const *outputs, int count, bool reset, float frame_interval_ms) override
    {
        if (count < 1)
            return false;
        if (!context || width != color.w || height != color.h) {
            invalidate();
            if (!createContext(fg, color.w, color.h))
                return false;
            reset = true;
        }

        const struct FfxApiRect2D rect = { 0, 0, color.w, color.h };

        ffxConfigureDescFrameGeneration config = {};
        config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        config.swapChain = nullptr;
        config.frameGenerationEnabled = true;
        config.allowAsyncWorkloads = false;
        config.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
        config.onlyPresentGenerated = false;
        config.generationRect = rect;
        config.frameID = frame_id;
        if (!check(configure(&context, &config.header), "Configure"))
            return false;

        ffxDispatchDescFrameGenerationPrepare prepare = {};
        prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
        prepare.frameID = frame_id;
        prepare.flags = 0;
        prepare.commandList = cmd;
        prepare.renderSize = { static_cast<uint32_t>(color.w), static_cast<uint32_t>(color.h) };
        prepare.jitterOffset = { 0.0f, 0.0f };
        prepare.motionVectorScale = { 1.0f, 1.0f };
        prepare.frameTimeDelta = std::isfinite(frame_interval_ms) && frame_interval_ms > 0.0f ? frame_interval_ms : 16.6667f;
        prepare.cameraNear = 0.1f;
        prepare.cameraFar = 1000.0f;
        prepare.cameraFovAngleVertical = 1.0f;
        prepare.viewSpaceToMetersFactor = 1.0f;
        prepare.depth = makeFfxResource(fg.depth, FFX_API_SURFACE_FORMAT_R32_FLOAT, false);
        prepare.motionVectors = makeFfxResource(fg.mvec, FFX_API_SURFACE_FORMAT_R16G16_FLOAT, false);
        if (!check(dispatch(&context, &prepare.header), "Prepare"))
            return false;

        ffxDispatchDescFrameGeneration generate = {};
        generate.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
        generate.commandList = cmd;
        generate.presentColor = makeFfxResource(color, FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM, false);
        generate.outputs[0] = makeFfxResource(*outputs[0], FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM, true);
        generate.numGeneratedFrames = 1;
        generate.reset = reset;
        generate.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        generate.minMaxLuminance[0] = 0.0f;
        generate.minMaxLuminance[1] = 1000.0f;
        generate.generationRect = rect;
        generate.frameID = frame_id;
        if (!check(dispatch(&context, &generate.header), "Dispatch"))
            return false;
        // FFX requires the frame id to advance by exactly one per frame.
        frame_id++;
        return true;
    }

    void invalidate() override
    {
        if (context)
            destroy_context(&context, nullptr);
        context = nullptr;
        width = height = 0;
    }

    void shutdown() override
    {
        if (destroy_context)
            invalidate();
        // The runtime module stays loaded: unloading it is not supported while
        // the process keeps the Vulkan device.
    }

private:
    void *symbol(const char *name)
    {
#if defined(_WIN32)
        return reinterpret_cast<void *>(GetProcAddress(module, name));
#else
        return dlsym(module, name);
#endif
    }

    bool check(ffxReturnCode_t code, const char *stage)
    {
        if (code == FFX_API_RETURN_OK)
            return true;
        qCWarning(chiakiGui) << "[framegen] FSR" << stage << "failed:" << ffxResultString(code) << code;
        return false;
    }

    bool createContext(FrameGen::Impl &fg, int w, int h)
    {
        ffxCreateBackendVKDesc backend_desc = {};
        backend_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend_desc.vkDevice = vk->device;
        backend_desc.vkPhysicalDevice = vk->phys_device;
        backend_desc.vkDeviceProcAddr = fg.vkGetDeviceProcAddr;

        ffxCreateContextDescFrameGeneration desc = {};
        desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        desc.header.pNext = &backend_desc.header;
        desc.flags = FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
        desc.displaySize = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
        desc.maxRenderSize = desc.displaySize;
        desc.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        if (!check(create_context(&context, &desc.header, nullptr), "CreateContext")) {
            context = nullptr;
            return false;
        }
        width = w;
        height = h;
        frame_id = 0;
        return true;
    }

    pl_vulkan vk = nullptr;
#if defined(_WIN32)
    HMODULE module = nullptr;
#else
    void *module = nullptr;
#endif
    PfnFfxCreateContext create_context = nullptr;
    PfnFfxDestroyContext destroy_context = nullptr;
    PfnFfxConfigure configure = nullptr;
    PfnFfxDispatch dispatch = nullptr;
    ffxContext context = nullptr;
    int width = 0;
    int height = 0;
    uint64_t frame_id = 0;
};

} // namespace

#endif // CHIAKI_HAVE_FFX

bool FrameGen::compiledIn()
{
    return true;
}

bool FrameGen::backendCompiledIn(Backend backend)
{
    switch (backend) {
#if defined(CHIAKI_HAVE_NGX)
    case Backend::Dlss: return true;
#endif
#if defined(CHIAKI_HAVE_FFX)
    case Backend::Fsr: return true;
#endif
    default: return false;
    }
}

const char *FrameGen::backendName(Backend backend)
{
    switch (backend) {
    case Backend::Dlss: return "DLSS Frame Generation";
    case Backend::Fsr: return "FSR 3.1 Frame Generation";
    default: return "None";
    }
}

FrameGen::FrameGen(pl_log log)
    : impl(std::make_unique<Impl>())
{
    impl->log = log;
}

FrameGen::~FrameGen()
{
    destroy();
}

bool FrameGen::ensureInitialized(pl_vulkan vk, Backend backend)
{
    Impl &d = *impl;
    if (backend == Backend::None || !backendCompiledIn(backend) || !vk)
        return false;
    if (d.backend == backend && d.backend_impl)
        return true;
    const size_t backend_index = static_cast<size_t>(backend);
    if (d.backend_failed[backend_index])
        return false;

    d.destroyBackend();
    if (!d.initCommon(vk)) {
        qCWarning(chiakiGui) << "[framegen] Frame generation is unavailable on this Vulkan device";
        d.shutdownCommon();
        d.backend_failed.fill(true);
        return false;
    }

    std::unique_ptr<FgBackendImpl> impl_backend;
    switch (backend) {
#if defined(CHIAKI_HAVE_NGX)
    case Backend::Dlss: impl_backend = std::make_unique<DlssBackend>(); break;
#endif
#if defined(CHIAKI_HAVE_FFX)
    case Backend::Fsr: impl_backend = std::make_unique<FsrBackend>(); break;
#endif
    default: break;
    }
    unsigned long seh_code = 0;
    const bool init_ok = impl_backend && guardedCall([&]() { return impl_backend->init(d); }, &seh_code);
    if (seh_code) {
        qCCritical(chiakiGui).nospace() << "[framegen] " << backendName(backend)
            << " crashed while starting (exception 0x" << Qt::hex << seh_code << ")";
        (void)impl_backend.release();
    }
    if (!init_ok) {
        if (impl_backend)
            impl_backend->shutdown();
        qCWarning(chiakiGui) << "[framegen]" << backendName(backend) << "is unavailable";
        d.backend_failed[backend_index] = true;
        return false;
    }
    d.backend_impl = std::move(impl_backend);
    d.backend = backend;
    d.consecutive_failures = 0;
    d.reset_pending = true;
    d.trace_frames = 0;
    qCInfo(chiakiGui) << "[framegen]" << backendName(backend) << "initialized";
    return true;
}

FrameGen::Backend FrameGen::backend() const
{
    return impl->backend;
}

bool FrameGen::backendFailed(Backend backend) const
{
    const size_t index = static_cast<size_t>(backend);
    return index < impl->backend_failed.size() && impl->backend_failed[index];
}

float FrameGen::gpuTimeMs() const
{
    return impl->timer.averageMs();
}

int FrameGen::maxMultiplier() const
{
    return impl->backend_impl ? impl->backend_impl->maxGenerated() + 1 : 1;
}

void FrameGen::reset()
{
    impl->reset_pending = true;
}

void FrameGen::destroy()
{
    impl->shutdownCommon();
}

bool FrameGen::process(const struct pl_frame *src, int out_w, int out_h, int multiplier,
                       float frame_interval_ms, std::vector<struct pl_frame> &outputs)
{
    Impl &d = *impl;
    outputs.clear();
    if (!d.backend_impl || !src || src->num_planes <= 0 || out_w <= 0 || out_h <= 0)
        return false;
    if (pl_color_space_is_hdr(&src->color)) {
        // The generation path here is 8-bit SDR; leave HDR streams alone.
        if (!d.hdr_warned) {
            qCInfo(chiakiGui) << "[framegen] HDR stream detected, frame generation is skipped for HDR content";
            d.hdr_warned = true;
        }
        return false;
    }
    const int count = std::clamp(multiplier - 1, 1, d.backend_impl->maxGenerated());
    const bool trace = d.trace_frames < 4;
    QElapsedTimer trace_timer;
    if (trace) {
        d.trace_frames++;
        trace_timer.start();
        qCInfo(chiakiGui).nospace() << "[framegen] frame " << d.trace_frames << ": " << out_w << "x" << out_h
                                    << " generating " << count << ", preparing textures";
    }
    auto trace_step = [&](const char *step) {
        if (trace)
            qCInfo(chiakiGui).nospace() << "[framegen] frame " << d.trace_frames << ": " << step
                                        << " (" << trace_timer.elapsed() << " ms)";
    };
    if (!d.ensureSize(out_w, out_h, trace_step)) {
        qCWarning(chiakiGui) << "[framegen] Could not allocate frame generation textures";
        return false;
    }
    trace_step("textures ready");

    Impl::FrameSet &set = d.sets[d.next_set];
    d.next_set = (d.next_set + 1) % d.sets.size();

    struct pl_frame target = {};
    target.num_planes = 1;
    target.planes[0].texture = set.real.tex;
    target.planes[0].components = 4;
    for (int i = 0; i < 4; i++)
        target.planes[0].component_mapping[i] = i;
    target.repr = pl_color_repr_rgb;
    target.color = pl_color_space_bt709;
    struct pl_render_params render_params = pl_render_default_params;
    render_params.frame_mixer = nullptr;
    if (!pl_render_image(d.renderer, src, &target, &render_params)) {
        qCWarning(chiakiGui) << "[framegen] Failed to prepare the frame generation input";
        return false;
    }
    trace_step("input rendered");

    const uint32_t slot_index = static_cast<uint32_t>(d.next_slot);
    Impl::CommandSlot &slot = d.cmd_slots[d.next_slot];
    d.next_slot = (d.next_slot + 1) % d.cmd_slots.size();
    if (!d.waitDone(slot.done_value))
        return false;
    d.timer.collect(slot_index);

    std::array<FgImage *, 3 + kMaxGenerated> held = {};
    size_t num_held = 0;
    FgImage *generated[kMaxGenerated] = {};
    bool ok = true;
    auto hold = [&](FgImage &img) {
        if (ok && d.hold(img))
            held[num_held++] = &img;
        else
            ok = false;
    };
    hold(set.real);
    hold(d.mvec);
    hold(d.depth);
    for (int i = 0; i < count; i++) {
        generated[i] = &set.generated[static_cast<size_t>(i)];
        hold(*generated[i]);
    }

    trace_step(ok ? "images held" : "holding images failed");
    const bool reset = d.reset_pending || !d.have_history;
    bool backend_crashed = false;
    if (ok)
        ok = d.vkResetCommandBuffer(slot.cmd, 0) == VK_SUCCESS;
    if (ok) {
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ok = d.vkBeginCommandBuffer(slot.cmd, &begin_info) == VK_SUCCESS;
        if (ok) {
            d.timer.begin(slot.cmd, slot_index);
            unsigned long seh_code = 0;
            {
                // The runtimes may submit to libplacebo's queues themselves.
                VkQueuesLock queues_lock(d.vk);
                ok = guardedCall([&]() {
                    return d.backend_impl->record(d, slot.cmd, set.real, generated, count, reset, frame_interval_ms);
                }, &seh_code);
            }
            if (seh_code) {
                qCCritical(chiakiGui).nospace() << "[framegen] " << backendName(d.backend)
                    << " crashed inside its runtime (exception 0x" << Qt::hex << seh_code << ")";
                backend_crashed = true;
                ok = false;
            }
            trace_step(ok ? "backend recorded" : "backend failed");
            d.timer.end(slot.cmd, slot_index);
            ok = d.vkEndCommandBuffer(slot.cmd) == VK_SUCCESS && ok;
        }
    }
    ok = ok && d.submit(slot.cmd, held.data(), num_held);
    if (ok)
        slot.done_value = d.done_value;
    trace_step(ok ? "submitted" : "not submitted");
    // Hand every image back to libplacebo; after a failure only libplacebo's
    // own hold signal has to be waited for.
    for (size_t i = 0; i < num_held; i++) {
        if (ok)
            d.release(*held[i], d.sem_done, d.done_value);
        else
            d.release(*held[i], held[i]->sem, held[i]->value);
    }

    if (!ok) {
        d.have_history = false;
        if (backend_crashed) {
            // Its state is unknown: never call into this runtime again, and
            // leak it rather than risk a second crash while shutting it down.
            qCWarning(chiakiGui) << "[framegen] Disabling" << backendName(d.backend) << "for this session";
            d.backend_failed[static_cast<size_t>(d.backend)] = true;
            (void)d.backend_impl.release();
            d.backend = FrameGen::Backend::None;
        } else if (++d.consecutive_failures >= kMaxConsecutiveFailures) {
            qCWarning(chiakiGui) << "[framegen] Disabling" << backendName(d.backend) << "after repeated failures";
            d.backend_failed[static_cast<size_t>(d.backend)] = true;
            d.destroyBackend();
        }
        return false;
    }
    d.consecutive_failures = 0;
    d.have_history = true;
    d.reset_pending = false;

    if (!reset) {
        for (int i = 0; i < count; i++) {
            outputs.emplace_back();
            Impl::fillFrame(*generated[i], &outputs.back());
        }
    }
    outputs.emplace_back();
    Impl::fillFrame(set.real, &outputs.back());
    return true;
}

#else // !CHIAKI_HAVE_FRAMEGEN

struct FrameGen::Impl {};

bool FrameGen::compiledIn() { return false; }
bool FrameGen::backendCompiledIn(Backend) { return false; }
const char *FrameGen::backendName(Backend) { return "None"; }
FrameGen::FrameGen(pl_log) {}
FrameGen::~FrameGen() = default;
bool FrameGen::ensureInitialized(pl_vulkan, Backend) { return false; }
FrameGen::Backend FrameGen::backend() const { return Backend::None; }
bool FrameGen::backendFailed(Backend) const { return true; }
int FrameGen::maxMultiplier() const { return 1; }
float FrameGen::gpuTimeMs() const { return 0.0f; }
void FrameGen::reset() {}
void FrameGen::destroy() {}
bool FrameGen::process(const struct pl_frame *, int, int, int, float, std::vector<struct pl_frame> &) { return false; }

#endif // CHIAKI_HAVE_FRAMEGEN
