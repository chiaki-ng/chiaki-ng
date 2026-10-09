#pragma once

#include "streamsession.h"
#include "settings.h"

#include <QMutex>
#include <QAtomicInteger>
#include <QWindow>
#include <QQuickWindow>
#include <QLoggingCategory>
#include <deque>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libplacebo/opengl.h>
#include <libplacebo/options.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/utils/frame_queue.h>
#include <libplacebo/log.h>
#include <libplacebo/cache.h>
}

#include <vulkan/vulkan.h>
#if defined(Q_OS_LINUX)
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_wayland.h>
#elif defined(Q_OS_MACOS)
#include <vulkan/vulkan_metal.h>
#elif defined(Q_OS_WIN)
#include <vulkan/vulkan_win32.h>
#endif

Q_DECLARE_LOGGING_CATEGORY(chiakiGui);

class Settings;
class StreamSession;
class QmlBackend;
class AutomationOsdRenderer;
class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QWidget;
class QLabel;
class QVBoxLayout;
class QHBoxLayout;
class QFrame;
class StatsOverlayWidget;
class BufferedPlaybackPacerThread;
class DeferredPresentPacerThread;
class DeferredSwapThread;

class QmlMainWindow : public QWindow
{
    Q_OBJECT
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY droppedFramesChanged)
    Q_PROPERTY(bool keepVideo READ keepVideo WRITE setKeepVideo NOTIFY keepVideoChanged)
    Q_PROPERTY(bool loadingTransitionComplete READ loadingTransitionComplete NOTIFY loadingTransitionCompleteChanged)
    Q_PROPERTY(VideoMode videoMode READ videoMode WRITE setVideoMode NOTIFY videoModeChanged)
    Q_PROPERTY(float ZoomFactor READ zoomFactor WRITE setZoomFactor NOTIFY zoomFactorChanged)
    Q_PROPERTY(VideoPreset videoPreset READ videoPreset WRITE setVideoPreset NOTIFY videoPresetChanged)
    Q_PROPERTY(bool directStream READ directStream NOTIFY directStreamChanged)
    Q_PROPERTY(int runtimeRendererBackend READ runtimeRendererBackend CONSTANT)
    Q_PROPERTY(double queueDepthAverage READ queueDepthAverage NOTIFY queueDepthAverageChanged)
    Q_PROPERTY(double pendingFrameAge READ pendingFrameAge NOTIFY pendingFrameAgeChanged)

public:
    enum class UpdateRequestReason {
        Unknown,
        SceneChanged,
        RenderRequested,
        Timer,
        QueueStoredFrame,
        QueueReset,
        PendingFrame,
        Replay,
        PlaceboReset,
        FinalizePending,
        NoSwapchain,
        VSync
    };
    Q_ENUM(UpdateRequestReason);

    enum class VideoMode {
        Normal,
        Stretch,
        Zoom
    };
    Q_ENUM(VideoMode);

    enum class VideoPreset {
        Fast,
        Default,
        HighQuality,
        HighQualitySpatial,
        HighQualityAdvancedSpatial,
        Custom
    };
    Q_ENUM(VideoPreset);

    QmlMainWindow(Settings *settings,  bool exit_app_on_stream_exit = false);
    QmlMainWindow(const StreamSessionConnectInfo &connect_info);
    ~QmlMainWindow();
    void updateWindowType(WindowType type);
    void setSettings(Settings *new_settings);

    bool hasVideo() const;
    int droppedFrames() const;
    void increaseDroppedFrames();

    bool directStream() const;
    int runtimeRendererBackend() const { return static_cast<int>(render_backend); }
    bool loadingTransitionComplete() const { return loading_transition_complete.loadAcquire() != 0; }

    bool keepVideo() const;
    void setKeepVideo(bool keep);

    VideoMode videoMode() const;
    void setVideoMode(VideoMode mode);

    float zoomFactor() const;
    void setZoomFactor(float factor);

    double queueDepthAverage() const;
    double pendingFrameAge() const;
    QString describePlaceboDiscardReason(qint64 now_us) const;

    bool amdCard() const;
    bool nvidiaCard() const;
    bool wasMaximized() const { return was_maximized; };
    bool isWindowAdjustable() const { return is_window_adjustable; }
    void setWindowAdjustable(bool adjustable) { is_window_adjustable = adjustable; }

    void fullscreenTime();
    void normalTime();

    bool isStreamWindowAdjustable() { return is_stream_window_adjustable; }
    void setStreamWindowAdjustable(bool adjustable) { is_stream_window_adjustable = adjustable; }

    QmlBackend *getBackend();

    VideoPreset videoPreset() const;
    void setVideoPreset(VideoPreset mode);

    Q_INVOKABLE void grabInput();
    Q_INVOKABLE void releaseInput();
    Q_INVOKABLE void requestOverlayUpdate();
    Q_INVOKABLE void setOverlayInteractionActive(bool active);
    Q_INVOKABLE void setStatsOverlayActive(bool active);
    Q_INVOKABLE void noteLoadingTransitionComplete();
    Q_INVOKABLE void presentStartupWarmupFrame(unsigned width, unsigned height, bool hdr);
    void armVerbosePlaceboQuietWindow();
    bool startupWarmupFrameActive() const { return startup_warmup_frame_active; }

public slots:
    void resetPlaceboQueue();
    void schedulePlaceboReset();
    void queuePlaceboReset(bool preserve_timeline);

    void updatePlacebo();
    void updateVSync();
    void updateVulkanDeferredSwap();
    void show();
    void presentFrame(ChiakiFfmpegFrame frame, int32_t frames_lost, qint64 decoder_delivery_us = 0);

    AVBufferRef *vulkanHwDeviceCtx();

signals:
    void hasVideoChanged();
    void droppedFramesChanged();
    void keepVideoChanged();
    void videoModeChanged();
    void zoomFactorChanged();
    void videoPresetChanged();
    void menuRequested();
    void directStreamChanged();
    void queueDepthAverageChanged();
    void pendingFrameAgeChanged();
    void loadingTransitionCompleteChanged();
    void statsOverlayActiveChanged();

private:
    friend class BufferedPlaybackPacerThread;
    friend class DeferredPresentPacerThread;
    friend class DeferredSwapThread;

    struct PendingFrameEntry;

    bool makeOpenGLContextCurrent();
    void doneOpenGLContextCurrent();

    void init(Settings *settings, bool exit_app_on_stream_exit = false);
    pl_gpu placeboGpu() const;
    void update();
    void scheduleUpdate(bool force = false);
    void scheduleUpdate(bool force, UpdateRequestReason reason);
    void scheduleBufferedUpdate();
    void scheduleBufferedUpdate(UpdateRequestReason reason);
    void updateStatsOverlayGeometry();
    bool statsOverlayActive() const { return stats_overlay_visible; }
    void armQuickNeedSync(const char *reason);
    void scheduleRenderIfBacklog(UpdateRequestReason reason = UpdateRequestReason::PendingFrame);
    void handleBufferedPlaybackWake(qint64 timer_fire_us);
    void completeStartupVideoVisibility(quint64 generation);
    bool throttleFramePresentation(double interval_s);
    void handleDeferredPresentWake(qint64 timer_fire_us);
    bool enqueueDeferredSwap(qint64 submit_begin_us,
                             int queue_depth_at_submit,
                             int depth_limit,
                             bool pending_frame_waiting,
                             bool pending_overflow_waiting,
                             qint64 present_interval_us,
                             qint64 present_submit_interval_us);
    void finalizeDeferredPresentIfIdle();
    void processDeferredSwapTask(qint64 submit_begin_us,
                                 int queue_depth_at_submit,
                                 int depth_limit,
                                 bool pending_frame_waiting,
                                 bool pending_overflow_waiting,
                                 qint64 present_interval_us,
                                 qint64 present_submit_interval_us);
    void drainDeferredSwaps();
    void setStreamMaxFPS(unsigned int max_fps);
    void createSwapchain();
    void destroySwapchain();
    void resizeSwapchain();
    void updateSwapchain();
    void drainRenderThread();
    void sync();
    void beginFrame();
    void endFrame();
    void render();
    void handleVulkanDeviceLost(const QString &reason);
    void handleVulkanRendererFallback(const QString &title, const QString &message, const QString &fallback_reason);
    void applyPendingFrame();
    void queuePendingFrameRelease();
    bool applyPendingFrameIfQueueHasCapacity();
    bool hasPendingFrame() const;
    bool hasPendingFrameOverflow();
    bool pendingFrameOverflowEnabled() const;
    int effectiveQueueDepthLimit() const;
    int pendingFrameOverflowLimit(int submission_depth_limit) const;
    void clearPendingFrameStateLocked();
    void prunePendingFramesBeforeLocked(double cutoff_pts);
    void insertPendingOverflowLocked(PendingFrameEntry entry);
    bool takePendingFrameLocked(PendingFrameEntry &entry);
    bool dropPendingFrameLocked();
    void resetQueueDepthTracking();
    bool promotePendingFrameFromOverflowLocked();
    bool storePendingFrame(ChiakiFfmpegFrame &frame, bool take_ownership = false, bool synthetic_warmup = false);
    bool storeResetSeedFrame(const AVFrame *frame, double pts, float duration, quint64 generation);
    bool storeResetSeedFromPendingFrame(quint64 generation);
    bool cloneNewestPendingFrameLocked(AVFrame *&clone, double &pts, float &duration, uint64_t *stored_us = nullptr);
    bool applyKeptFrameSnapshot();
    bool applyResetSeedFrame(quint64 generation);
    bool queueStoredFrame(AVFrame *frame, double pts, float duration,
                          void (*discard_cb)(const struct pl_source_frame *),
                          bool synthetic_warmup = false);
    void refreshPendingFrameAge();
    void snapshotPendingFrame();
    bool handleShortcut(QKeyEvent *event);
    bool event(QEvent *event) override;
    QObject *focusObject() const override;
    void updateQueueDepthAverage(int depth);
    void updatePendingFrameAge(double age);
    void snapshotLastFrame(AVFrame *frame, double pts, float duration, bool take_ownership = false);
    void clearSnapshotFrame();
    bool hasBufferedWork();
    bool enqueueKeptFrame(double queue_pts_origin_hint, bool deinterlace_enabled, double &used_origin);
    double queuePtsOriginCached() const;
    void setQueuePtsOriginCached(double queue_pts_origin);
    const struct pl_filter_config *effectiveFrameMixerConfig(const struct pl_render_params *render_params = nullptr) const;
    bool effectiveFrameMixerEnabled(const struct pl_render_params *render_params = nullptr) const;
    bool configuredFrameMixerEnabledForScheduling() const;
    bool has_video = false;
    struct pl_queue_params qparams;
    struct pl_frame_mix frame_mix;
    // Direct rendering retains the displayed mapping and replaces only pending input.
    bool bypass_frame_queue = true;
    QMutex direct_frame_mutex;
    AVFrame *direct_pending_frame = nullptr;
    struct pl_frame direct_frame = {};
    QAtomicInteger<int> direct_frame_reset = 0;
    uint64_t ts_start = 0;
    double queue_pts_origin = -1.0;
    double newest_queued_frame_pts = -1.0;
    bool playback_started = false;
    bool preserve_playback_timeline = false;
    bool was_maximized = false;
    bool amd_card = false;
    bool nvidia_card = false;
    bool direct_stream = false;
    std::atomic<qint64> queue_pts_origin_cached_us = -1;
    uint64_t next_frame_target_us = 0;
    double last_throttle_interval_s = 0.0;
    QAtomicInteger<int> present_backpressure_active = 0;
    QAtomicInteger<int> present_pace_timer_rearm = 0;
    QAtomicInteger<int> present_pacing_reset_pending = 0;
    QAtomicInteger<int> ui_priority_update_pending = 0;
    QAtomicInteger<int> overlay_interaction_active = 0;
    bool keep_video = false;
    RenderBackend render_backend = RenderBackend::Vulkan;
    int grab_input = 0;
    int dropped_frames = 0;
    bool is_window_adjustable = false;
    bool is_stream_window_adjustable = false;
    QAtomicInteger<int> dropped_frames_current = 0;
    bool going_full = false;
    VideoMode video_mode = VideoMode::Normal;
    float zoom_factor = 0;
    VideoPreset video_preset = VideoPreset::HighQuality;
    Settings *settings = {};

    QmlBackend *backend = {};
    StreamSession *session = {};
    AVBufferRef *vulkan_hw_dev_ctx = nullptr;
    std::atomic<double> queue_depth_average{0.0};
    QAtomicInteger<int> queue_depth_cached = 0;
    double pending_frame_age = 0.0;
    uint64_t last_placebo_reset_ts = 0;
    uint64_t pending_frame_stored_us = 0;
    mutable QMutex pending_frame_age_mutex;
    bool startup_warmup_preserve_next_session_change = false;

    pl_cache placebo_cache = {};
    pl_log placebo_log = {};
    pl_vk_inst placebo_vk_inst = {};
    pl_vulkan placebo_vulkan = {};
    pl_opengl placebo_opengl = {};
    pl_swapchain placebo_swapchain = {};
#ifdef CHIAKI_GUI_ENABLE_AUTOMATION
    AutomationOsdRenderer *automation_osd = nullptr;
#endif
    pl_renderer placebo_renderer = {};
    pl_queue placebo_queue = {};
    std::array<pl_tex, 8> placebo_tex{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    int vk_decode_queue_index = -1;
    QSize swapchain_size;
    QThread *render_thread = {};
    bool owns_render_thread = false;
    QMutex render_schedule_mutex;
    QMutex placebo_state_mutex;
    QMutex placebo_swapchain_mutex;
    bool render_scheduled = false;
    bool render_pending = false;
    QAtomicInteger<int> render_pending_during_cycle = 0;
    QAtomicInteger<int> last_update_request_reason = 0;
    QAtomicInteger<int> queue_stored_frame_pending = 0;
    QAtomicInteger<int> pending_frame_submission_active = 0;
    QMutex pending_frame_mutex;
    struct PendingFrameEntry {
        AVFrame *frame = nullptr;
        double pts = 0.0;
        float duration = 0.0f;
        double queue_origin = 0.0;
        uint64_t stored_us = 0;
        bool synthetic_warmup = false;
    };
    AVFrame *pending_frame = nullptr;
    std::deque<PendingFrameEntry> pending_frame_overflow;
    bool pending_frame_synthetic_warmup = false;
    double pending_pts = 0.0;
    float pending_duration = 0.0f;
    double pending_frame_queue_origin = 0.0;
    QAtomicInteger<int> pending_frame_present = 0;
    QAtomicInteger<int> pending_frame_release_queued = 0;
    QAtomicInteger<qint64> pending_frame_release_queued_us = 0;
    QMutex reset_seed_mutex;
    AVFrame *reset_seed_frame = nullptr;
    double reset_seed_pts = 0.0;
    float reset_seed_duration = 0.0f;
    quint64 reset_seed_generation = 0;
    QAtomicInteger<quint64> snapshot_generation = 0;
    QAtomicInteger<quint64> pending_reset_snapshot_generation = 0;
    QAtomicInteger<quint64> last_reset_snapshot_generation = 0;
    QAtomicInteger<int> stream_session_active = 0;
    QMutex kept_frame_mutex;
    AVFrame *kept_frame = nullptr;
    double kept_frame_pts = 0.0;
    float kept_frame_duration = 0.0f;
    AVFrame *fallback_frame = nullptr;
    QAtomicInteger<int> swapchain_recreate_pending = 0;
    QAtomicInteger<int> vulkan_device_lost = 0;
    QAtomicInteger<int> renderer_cache_flush_pending = 0;
    QAtomicInteger<int> placebo_reset_pending = 0;
    QAtomicInteger<int> placebo_reset_preserve_timeline = 0;
    QAtomicInteger<int> reset_seed_capture_active = 0;
    QAtomicInteger<quint64> reset_seed_capture_generation = 0;
    QAtomicInteger<quint64> placebo_reset_throttle_generation = 0;
    QAtomicInteger<int> render_active = 0;
    QAtomicInteger<int> deferred_present_in_flight = 0;
    QAtomicInteger<int> startup_video_visible_pending = 0;
    QAtomicInteger<int> startup_first_real_frame_queued_pending_visibility = 0;
    QAtomicInteger<int> loading_transition_complete = 0;
    QAtomicInteger<int> startup_video_visible_refresh_pending = 0;
    QAtomicInteger<int> stats_overlay_active = 0;
    StatsOverlayWidget *stats_overlay_widget = nullptr;
    bool stats_overlay_visible = false;
    QAtomicInteger<quint64> startup_video_visible_generation = 0;
    bool startup_warmup_frame_active = false;
    bool vulkan_deferred_swap_enabled = false;
    bool present_vsync_enabled = true;

    QVulkanInstance *qt_vk_inst = {};
    QOpenGLContext *qt_gl_context = {};
    QOffscreenSurface *qt_gl_offscreen_surface = {};
    QQmlEngine *qml_engine = {};
    QQuickWindow *quick_window = {};
    QQuickRenderControl *quick_render = {};
    QQuickItem *quick_item = {};
    BufferedPlaybackPacerThread *buffered_pace_thread = {};
    DeferredPresentPacerThread *present_pace_thread = {};
    DeferredSwapThread *deferred_swap_thread = {};
    VkFormat quick_vk_format = VK_FORMAT_UNDEFINED;
    pl_tex quick_tex = {};
    QOpenGLFramebufferObject *quick_fbo = {};
    VkSemaphore quick_sem = VK_NULL_HANDLE;
    uint64_t quick_sem_value = 0;
    VkImage quick_vk_image = VK_NULL_HANDLE;
    bool quick_frame = false;
    QAtomicInteger<int> quick_need_sync = 0;
    QAtomicInteger<int> quick_need_render = 0;
    qint64 quick_begin_wait_last_us = 0;
    int quick_render_skip_next = 0;
    QAtomicInteger<int> update_pending = 0;
    QAtomicInteger<int> schedule_frame_mixer_active = 0;
    double source_frame_interval_ms = 16.6667;
    double stream_configured_frame_interval_ms = 16.6667;
    QAtomicInteger<qint64> last_schedule_update_us = 0;
    QAtomicInteger<qint64> last_render_dispatch_us = 0;
    QAtomicInteger<qint64> last_render_entry_us = 0;
    QAtomicInteger<qint64> last_render_idle_us = 0;
    QAtomicInteger<qint64> last_swap_return_us = 0;
    QAtomicInteger<qint64> last_present_complete_us = 0;
    QAtomicInteger<qint64> last_present_target_us = 0;
    QAtomicInteger<qint64> last_present_wakeup_target_us = 0;
    QAtomicInteger<qint64> swap_to_start_gap_estimate_us = 0;
    QAtomicInteger<qint64> start_frame_block_estimate_us = 0;
    QAtomicInteger<qint64> render_submit_estimate_us = 0;
    QAtomicInteger<int> surface_create_failures = 0;
    qint64 surface_create_failure_first_us = 0;
    QAtomicInteger<int> swapchain_start_failures = 0;
    qint64 swapchain_start_failure_first_us = 0;
    QAtomicInteger<int> buffered_timer_fired = 0;
    qint64 last_update_us = 0;
    qint64 next_buffered_update_us = 0;
    qint64 last_buffered_interval_us = 0;
    QString pending_renderer_fallback_reason;
    VkPhysicalDeviceProperties vk_device_props = {};
    VkPhysicalDeviceDriverProperties vk_device_driver_props = {};
    pl_options renderparams_opts = {};
    bool renderparams_changed = false;
    const struct pl_hook *fsr_hook = nullptr;
    const struct pl_hook *fsrcnnx_hook_8 = nullptr;
    const struct pl_hook *fsrcnnx_hook_16 = nullptr;

    struct {
        PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
#if defined(Q_OS_LINUX)
        PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;
        PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
#elif defined(Q_OS_MACOS)
        PFN_vkCreateMetalSurfaceEXT vkCreateMetalSurfaceEXT;
#elif defined(Q_OS_WIN)
        PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#endif
        PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
        PFN_vkWaitSemaphores vkWaitSemaphores;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
        PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
    } vk_funcs;

    friend class QmlBackend;
};
