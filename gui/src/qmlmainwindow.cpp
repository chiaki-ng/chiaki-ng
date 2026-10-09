#include "qmlmainwindow.h"
#include "qmlbackend.h"
#include "qmlsvgprovider.h"
#include "chiaki/log.h"
#include "chiaki/time.h"
#include "streamsession.h"

#include <qpa/qplatformnativeinterface.h>

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

#include <QByteArray>
#include <QPointer>
#include <QColor>
#include <QDebug>
#include <QThread>
#include <QShortcut>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QFile>
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QVulkanInstance>
#include <QQuickItem>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickRenderTarget>
#include <QQuickRenderControl>
#include <QQuickGraphicsDevice>
#include <QTimer>
#include <QWaitCondition>
#include <QProcess>
#include <QProcessEnvironment>
#include <QtGlobal>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <utility>
#include <atomic>
#if defined(Q_OS_MACOS)
#include <objc/message.h>
#endif

Q_LOGGING_CATEGORY(chiakiGui, "chiaki.gui", QtInfoMsg);

class TimedMutexLocker
{
public:
    TimedMutexLocker(QMutex &mutex, const char *label, qint64 threshold_us = 500)
        : mutex(mutex), label(label), threshold_us(threshold_us)
    {
        begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        mutex.lock();
        locked_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        const qint64 wait_us = locked_us - begin_us;
        if (wait_us >= threshold_us) {
            qCDebug(chiakiGui).nospace()
                << "[latency] placebo_mutex_wait_us=" << wait_us
                << " label=" << label;
        }
    }

    ~TimedMutexLocker()
    {
        mutex.unlock();
        const qint64 unlock_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        const qint64 hold_us = unlock_us - locked_us;
        if (hold_us >= threshold_us) {
            qCDebug(chiakiGui).nospace()
                << "[latency] placebo_mutex_hold_us=" << hold_us
                << " label=" << label;
        }
    }

private:
    QMutex &mutex;
    const char *label;
    qint64 threshold_us;
    qint64 begin_us = 0;
    qint64 locked_us = 0;
};

class StatsOverlayWidget : public QWidget
{
public:
    StatsOverlayWidget(QmlMainWindow *owner, QmlBackend *backend)
        : QWidget(nullptr)
        , owner(owner)
        , backend(backend)
    {
        setObjectName(QStringLiteral("statsOverlayWidget"));
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        setAutoFillBackground(false);

        auto *outer = new QHBoxLayout(this);
        outer->setContentsMargins(0, 0, 6, 0);
        outer->setSpacing(0);
        outer->addStretch(1);

        auto *panel = new QFrame(this);
        panel->setObjectName(QStringLiteral("statsPanel"));
        panel->setStyleSheet(QStringLiteral(
            "#statsPanel {"
            "  background: rgba(0, 0, 0, 140);"
            "  border-radius: 10px;"
            "}"
            "QLabel { color: white; }"));
        panel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);

        auto *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(14, 12, 14, 12);
        panelLayout->setSpacing(8);

        auto makeRow = [panelLayout](const QString &title, const QColor &valueColor, QFont titleFont, QFont valueFont, QLabel *&valueLabel) {
            auto *row = new QWidget(panelLayout->parentWidget());
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);
            auto *titleLabel = new QLabel(title, row);
            titleLabel->setFont(titleFont);
            titleLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            titleLabel->setMinimumWidth(110);
            auto *value = new QLabel(row);
            value->setFont(valueFont);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            value->setStyleSheet(QStringLiteral("color: %1;").arg(valueColor.name()));
            value->setTextInteractionFlags(Qt::NoTextInteraction);
            valueLabel = value;
            rowLayout->addWidget(titleLabel);
            rowLayout->addWidget(value, 1);
            panelLayout->addWidget(row);
            return row;
        };

        QFont titleFont;
        titleFont.setPixelSize(15);
        QFont valueFont;
        valueFont.setPixelSize(18);
        valueFont.setBold(true);

        bitrateRow = makeRow(tr("Mbps"), QColor(QStringLiteral("#00a7ff")), titleFont, valueFont, bitrateValue);
        queueRow = makeRow(tr("queue depth avg"), QColor(QStringLiteral("#90caf9")), titleFont, valueFont, queueDepthValue);
        pendingRow = makeRow(tr("pending frame age"), QColor(QStringLiteral("#90caf9")), titleFont, valueFont, pendingAgeValue);
        packetLossRow = makeRow(tr("packet loss"), QColor(QStringLiteral("#ef9a9a")), titleFont, valueFont, packetLossValue);
        droppedFramesRow = makeRow(tr("dropped frames"), QColor(QStringLiteral("#ef9a9a")), titleFont, valueFont, droppedFramesValue);
        lostFramesRow = makeRow(tr("lost frames"), QColor(QStringLiteral("#ef9a9a")), titleFont, valueFont, lostFramesValue);

        outer->addWidget(panel, 0, Qt::AlignRight | Qt::AlignVCenter);

        connect(owner, &QmlMainWindow::queueDepthAverageChanged, this, &StatsOverlayWidget::refresh);
        connect(owner, &QmlMainWindow::pendingFrameAgeChanged, this, &StatsOverlayWidget::refresh);
        connect(owner, &QmlMainWindow::droppedFramesChanged, this, &StatsOverlayWidget::refresh);
        if (backend) {
            connect(backend, &QmlBackend::sessionChanged, this, [this](StreamSession *s) {
                setSession(s);
                refresh();
            });
            setSession(backend->qmlSession());
        }
        refresh();
    }

    void setOverlayVisible(bool visible)
    {
        if (visible) {
            updateTransientParent();
            syncGeometry();
            refresh();
            if (!isVisible())
                show();
            updateTransientParent();
            if (owner)
                owner->requestActivate();
        } else {
            if (QWindow *overlay_window = windowHandle())
                overlay_window->setTransientParent(nullptr);
            hide();
        }
    }

    void syncGeometry()
    {
        if (!owner)
            return;
        const QRect g = owner->geometry();
        if (geometry() != g)
            setGeometry(g);
    }

    void updateTransientParent()
    {
        if (!owner)
            return;
        if (QWindow *overlay_window = windowHandle())
            overlay_window->setTransientParent(owner);
    }

    void setSession(StreamSession *session)
    {
        if (session == current_session)
            return;
        if (current_session)
            QObject::disconnect(current_session, nullptr, this, nullptr);
        current_session = session;
        if (current_session) {
            connect(current_session, &StreamSession::MeasuredBitrateChanged, this, &StatsOverlayWidget::refresh);
            connect(current_session, &StreamSession::AveragePacketLossChanged, this, &StatsOverlayWidget::refresh);
            connect(current_session, &StreamSession::FramesLostChanged, this, &StatsOverlayWidget::refresh);
            connect(current_session, &StreamSession::ConnectedChanged, this, &StatsOverlayWidget::refresh);
        }
    }

    void refresh()
    {
        StreamSession *session = current_session;
        const bool hasSession = session != nullptr;

        bitrateRow->setVisible(hasSession);
        if (hasSession)
            bitrateValue->setText(QString::number(session->GetMeasuredBitrate(), 'f', 1));
        else
            bitrateValue->clear();

        queueRow->setVisible(true);
        queueDepthValue->setText(QString::number(owner ? owner->queueDepthAverage() : 0.0, 'f', 1));
        pendingRow->setVisible(true);
        pendingAgeValue->setText(tr("%1 ms").arg((owner ? owner->pendingFrameAge() : 0.0) * 1000.0, 0, 'f', 0));

        packetLossRow->setVisible(hasSession);
        if (hasSession) {
            const double packetLoss = session->GetAveragePacketLoss();
            packetLossValue->setText(QStringLiteral("%1%").arg((packetLoss * 100.0), 0, 'f', 1));
        } else {
            packetLossValue->clear();
        }

        const int droppedFrames = owner ? owner->droppedFrames() : 0;
        droppedFramesValue->setText(QString::number(droppedFrames));
        droppedFramesRow->setVisible(droppedFrames > 0);

        lostFramesRow->setVisible(hasSession);
        if (hasSession)
            lostFramesValue->setText(QString::number(session->GetFramesLost()));
        else
            lostFramesValue->clear();
    }

private:
    QPointer<QmlMainWindow> owner;
    QPointer<QmlBackend> backend;
    QPointer<StreamSession> current_session;
    QWidget *bitrateRow = nullptr;
    QWidget *queueRow = nullptr;
    QWidget *pendingRow = nullptr;
    QWidget *packetLossRow = nullptr;
    QWidget *droppedFramesRow = nullptr;
    QWidget *lostFramesRow = nullptr;
    QLabel *bitrateValue = nullptr;
    QLabel *queueDepthValue = nullptr;
    QLabel *pendingAgeValue = nullptr;
    QLabel *packetLossValue = nullptr;
    QLabel *droppedFramesValue = nullptr;
    QLabel *lostFramesValue = nullptr;
};

class DeferredPresentPacerThread final : public QThread
{
public:
    explicit DeferredPresentPacerThread(QmlMainWindow *owner) : owner(owner) {}
    // use std instead of qt functions for microsecond waits

    void arm(qint64 target_wakeup_us)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping)
            return;
        if (!armed || target_wakeup_us < wakeup_target_us)
            wakeup_target_us = target_wakeup_us;
        armed = true;
        ++generation;
        condition.notify_one();
    }

    void disarm()
    {
        std::lock_guard<std::mutex> lock(mutex);
        armed = false;
        ++generation;
        condition.notify_one();
    }

    void stopThread()
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        armed = false;
        ++generation;
        condition.notify_one();
    }

protected:
    void run() override
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stopping) {
            if (!armed) {
                condition.wait(lock);
                continue;
            }

            const qint64 target_wakeup_us_now = wakeup_target_us;
            const quint64 observed_generation = generation;
            const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (target_wakeup_us_now <= now_us) {
                armed = false;
                lock.unlock();
                notifyWake(static_cast<qint64>(chiaki_time_now_monotonic_us()));
                lock.lock();
                continue;
            }

            const qint64 wait_us = target_wakeup_us_now - now_us;
            condition.wait_for(lock, std::chrono::microseconds(wait_us),
                [this, observed_generation, target_wakeup_us_now]() {
                    return stopping || !armed || generation != observed_generation || wakeup_target_us != target_wakeup_us_now;
                });
            if (stopping)
                break;
            if (!armed || generation != observed_generation || wakeup_target_us != target_wakeup_us_now)
                continue;

            armed = false;
            lock.unlock();
            notifyWake(static_cast<qint64>(chiaki_time_now_monotonic_us()));
            lock.lock();
        }
    }

private:
    void notifyWake(qint64 timer_fire_us)
    {
        if (!owner)
            return;
        QMetaObject::invokeMethod(owner, [owner = this->owner, timer_fire_us]() {
            if (owner)
                owner->handleDeferredPresentWake(timer_fire_us);
        }, Qt::QueuedConnection);
    }

    QmlMainWindow *owner = nullptr;
    std::mutex mutex;
    std::condition_variable condition;
    bool stopping = false;
    bool armed = false;
    qint64 wakeup_target_us = 0;
    quint64 generation = 0;
};

class BufferedPlaybackPacerThread final : public QThread
{
public:
    explicit BufferedPlaybackPacerThread(QmlMainWindow *owner) : owner(owner) {}

    void arm(qint64 target_wakeup_us)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping)
            return;
        if (!armed || target_wakeup_us < wakeup_target_us)
            wakeup_target_us = target_wakeup_us;
        armed = true;
        ++generation;
        condition.notify_one();
    }

    void disarm()
    {
        std::lock_guard<std::mutex> lock(mutex);
        armed = false;
        ++generation;
        condition.notify_one();
    }

    void stopThread()
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        armed = false;
        ++generation;
        condition.notify_one();
    }

    bool isArmed() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return armed && !stopping;
    }

protected:
    void run() override
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stopping) {
            if (!armed) {
                condition.wait(lock);
                continue;
            }

            const qint64 target_wakeup_us_now = wakeup_target_us;
            const quint64 observed_generation = generation;
            const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (target_wakeup_us_now <= now_us) {
                armed = false;
                lock.unlock();
                notifyWake(static_cast<qint64>(chiaki_time_now_monotonic_us()));
                lock.lock();
                continue;
            }

            const qint64 wait_us = target_wakeup_us_now - now_us;
            condition.wait_for(lock, std::chrono::microseconds(wait_us),
                [this, observed_generation, target_wakeup_us_now]() {
                    return stopping || !armed || generation != observed_generation || wakeup_target_us != target_wakeup_us_now;
                });
            if (stopping)
                break;
            if (!armed || generation != observed_generation || wakeup_target_us != target_wakeup_us_now)
                continue;

            armed = false;
            lock.unlock();
            notifyWake(static_cast<qint64>(chiaki_time_now_monotonic_us()));
            lock.lock();
        }
    }

private:
    void notifyWake(qint64 timer_fire_us)
    {
        if (!owner)
            return;
        QMetaObject::invokeMethod(owner, [owner = this->owner, timer_fire_us]() {
            if (owner)
                owner->handleBufferedPlaybackWake(timer_fire_us);
        }, Qt::QueuedConnection);
    }

    QmlMainWindow *owner = nullptr;
    mutable std::mutex mutex;
    std::condition_variable condition;
    bool stopping = false;
    bool armed = false;
    qint64 wakeup_target_us = 0;
    quint64 generation = 0;
};

struct DeferredSwapTask
{
    qint64 submit_request_us = 0;
    int queue_depth_at_submit = 0;
    int depth_limit = 0;
    bool pending_frame_waiting = false;
    bool pending_overflow_waiting = false;
    qint64 present_interval_us = 0;
    qint64 present_submit_interval_us = 0;
};

class DeferredSwapThread final : public QThread
{
public:
    explicit DeferredSwapThread(QmlMainWindow *owner)
        : owner(owner)
    {
    }

    bool enqueue(const DeferredSwapTask &task)
    {
        QMutexLocker locker(&mutex);
        if (stopping)
            return false;

        pending_task = task;
        has_pending_task = true;
        cond.wakeOne();
        return true;
    }

    bool hasPendingWork()
    {
        QMutexLocker locker(&mutex);
        return has_pending_task || active;
    }

    bool isIdle()
    {
        QMutexLocker locker(&mutex);
        return !has_pending_task && !active;
    }

    void drain()
    {
        QMutexLocker locker(&mutex);
        while (has_pending_task || active)
            drained.wait(&mutex);
    }

    void stopThread()
    {
        QMutexLocker locker(&mutex);
        stopping = true;
        cond.wakeAll();
    }

protected:
    void run() override
    {
        for (;;) {
            DeferredSwapTask task;
            {
                QMutexLocker locker(&mutex);
                while (!stopping && !has_pending_task)
                    cond.wait(&mutex);
                if (stopping)
                    break;
                task = pending_task;
                has_pending_task = false;
                active = true;
            }

            if (owner) {
                owner->processDeferredSwapTask(task.submit_request_us,
                                               task.queue_depth_at_submit,
                                               task.depth_limit,
                                               task.pending_frame_waiting,
                                               task.pending_overflow_waiting,
                                               task.present_interval_us,
                                               task.present_submit_interval_us);
            }

            {
                QMutexLocker locker(&mutex);
                active = false;
                if (!has_pending_task)
                    drained.wakeAll();
            }
            if (owner) {
                QMutexLocker locker(&mutex);
                if (!has_pending_task)
                    owner->deferred_present_in_flight.storeRelease(0);
            }
            if (owner) {
                QMutexLocker locker(&mutex);
                if (!has_pending_task) {
                    QMetaObject::invokeMethod(owner, [owner = this->owner]() {
                        if (!owner)
                            return;
                        if (owner->hasBufferedWork())
                            owner->scheduleUpdate(false, QmlMainWindow::UpdateRequestReason::PendingFrame);
                        else
                            owner->scheduleUpdate(false, QmlMainWindow::UpdateRequestReason::FinalizePending);
                    }, Qt::QueuedConnection);
                }
            }
        }

        QMutexLocker locker(&mutex);
        active = false;
        drained.wakeAll();
    }

private:
    QmlMainWindow *owner = nullptr;
    QMutex mutex;
    QWaitCondition cond;
    QWaitCondition drained;
    DeferredSwapTask pending_task;
    bool stopping = false;
    bool active = false;
    bool has_pending_task = false;
};

static bool detailedVerboseTelemetryEnabled()
{
    return qEnvironmentVariableIntValue("CHIAKI_VERBOSE_LOG_NOISE") == 1;
}

#define CHIAKI_NOISY_DEBUG() if (!detailedVerboseTelemetryEnabled()) {} else qCDebug(chiakiGui)

static std::atomic<uint64_t> placebo_verbose_log_quiet_until_us{0};

void QmlMainWindow::armQuickNeedSync(const char *reason)
{
    if (quick_need_sync.fetchAndStoreRelaxed(1) != 0)
        return;
    if (!detailedVerboseTelemetryEnabled())
        return;

    const bool stream_active = stream_session_active.loadAcquire() != 0;
    const bool pending_visible = startup_video_visible_pending.loadAcquire() != 0;
    const bool overlay_active = overlay_interaction_active.loadAcquire() != 0;
    if (qstrcmp(reason, "sceneChanged") == 0 && overlay_active)
        return;
    bool render_scheduled_now = false;
    bool render_pending_now = false;
    {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled_now = render_scheduled;
        render_pending_now = render_pending;
    }
    CHIAKI_NOISY_DEBUG().nospace()
        << "[latency] quick_need_sync_arm reason=" << reason
        << " stream_active=" << stream_active
        << " has_video=" << has_video
        << " overlay_active=" << overlay_active
        << " startup_pending=" << pending_visible
        << " render_active=" << (render_active.loadAcquire() != 0)
        << " render_scheduled=" << render_scheduled_now
        << " render_pending=" << render_pending_now;
}

static void logPtsRollback(const char *source, double frame_pts, double queue_pts_origin, bool preserve_timeline, double source_frame_interval_ms)
{
    if (!detailedVerboseTelemetryEnabled())
        return;
    if (queue_pts_origin < 0.0)
        return;
    CHIAKI_NOISY_DEBUG().nospace()
        << "[rollback] " << source
        << " frame_pts=" << frame_pts
        << " queue_pts_origin=" << queue_pts_origin
        << " preserve_timeline=" << preserve_timeline
        << " source_frame_interval_ms=" << source_frame_interval_ms;
}

static void logTimerFireInterval(qint64 now_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_fire_us = 0;
    static qint64 last_log_us = 0;
    static qint64 min_interval_us = 0;
    static qint64 max_interval_us = 0;
    static qint64 sum_interval_us = 0;
    static int samples = 0;

    if (last_fire_us != 0) {
        const qint64 interval_us = now_us - last_fire_us;
        if (min_interval_us == 0 || interval_us < min_interval_us)
            min_interval_us = interval_us;
        if (interval_us > max_interval_us)
            max_interval_us = interval_us;
        sum_interval_us += interval_us;
        samples++;
    }
    last_fire_us = now_us;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[timer] interval_us min=" << min_interval_us
            << " max=" << max_interval_us
            << " avg=" << (sum_interval_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_interval_us = 0;
        max_interval_us = 0;
        sum_interval_us = 0;
        samples = 0;
    }
}

static void logSwapInterval(qint64 now_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_swap_us = 0;
    static qint64 last_log_us = 0;
    static qint64 min_interval_us = 0;
    static qint64 max_interval_us = 0;
    static qint64 sum_interval_us = 0;
    static int samples = 0;

    if (last_swap_us != 0) {
        const qint64 interval_us = now_us - last_swap_us;
        if (min_interval_us == 0 || interval_us < min_interval_us)
            min_interval_us = interval_us;
        if (interval_us > max_interval_us)
            max_interval_us = interval_us;
        sum_interval_us += interval_us;
        samples++;
    }
    last_swap_us = now_us;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[swap] interval_us min=" << min_interval_us
            << " max=" << max_interval_us
            << " avg=" << (sum_interval_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_interval_us = 0;
        max_interval_us = 0;
        sum_interval_us = 0;
        samples = 0;
    }
}

static void logPresentCompletionStats(qint64 now_us,
                                      qint64 request_us,
                                      qint64 complete_us,
                                      int queue_depth,
                                      RenderBackend render_backend,
                                      bool deferred_present)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static QMutex present_completion_stats_mutex;
    QMutexLocker lock(&present_completion_stats_mutex);
    static qint64 last_log_us = 0;
    static qint64 last_complete_us = 0;
    static qint64 min_interval_us = 0;
    static qint64 max_interval_us = 0;
    static qint64 sum_interval_us = 0;
    static qint64 min_latency_us = 0;
    static qint64 max_latency_us = 0;
    static qint64 sum_latency_us = 0;
    static int interval_samples = 0;
    static int latency_samples = 0;

    if (last_complete_us != 0 && now_us >= last_complete_us) {
        const qint64 interval_us = now_us - last_complete_us;
        if (min_interval_us == 0 || interval_us < min_interval_us)
            min_interval_us = interval_us;
        if (interval_us > max_interval_us)
            max_interval_us = interval_us;
        sum_interval_us += interval_us;
        interval_samples++;
    }
    last_complete_us = now_us;

    if (complete_us >= request_us) {
        const qint64 latency_us = complete_us - request_us;
        if (min_latency_us == 0 || latency_us < min_latency_us)
            min_latency_us = latency_us;
        if (latency_us > max_latency_us)
            max_latency_us = latency_us;
        sum_latency_us += latency_us;
        latency_samples++;
    }

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && (interval_samples > 0 || latency_samples > 0)) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] present_complete backend=" << static_cast<int>(render_backend)
            << " pacing_mode=" << (deferred_present ? "deferred" : "immediate")
            << " queue_depth=" << queue_depth
            << " complete_interval_us_min=" << min_interval_us
            << " complete_interval_us_max=" << max_interval_us
            << " complete_interval_us_avg=" << (interval_samples > 0 ? (sum_interval_us / interval_samples) : 0)
            << " request_to_complete_us_min=" << min_latency_us
            << " request_to_complete_us_max=" << max_latency_us
            << " request_to_complete_us_avg=" << (latency_samples > 0 ? (sum_latency_us / latency_samples) : 0)
            << " samples=" << latency_samples
            << " interval_samples=" << interval_samples;
        last_log_us = now_us;
        min_interval_us = 0;
        max_interval_us = 0;
        sum_interval_us = 0;
        min_latency_us = 0;
        max_latency_us = 0;
        sum_latency_us = 0;
        interval_samples = 0;
        latency_samples = 0;
    }
}

static void logPresentOutlier(const char *stage,
                              qint64 now_us,
                              qint64 duration_us,
                              bool deferred_present,
                              bool present_vsync_enabled,
                              qint64 present_interval_us,
                              int queue_depth,
                              int depth_limit,
                              bool pending_frame_waiting,
    bool render_active_now,
    bool render_scheduled_now,
    bool render_pending_now)
{
    if (!chiakiGui().isDebugEnabled() || !detailedVerboseTelemetryEnabled())
        return;
    qint64 threshold_us = 0;
    if (qstrcmp(stage, "present_interval") == 0)
        threshold_us = qMax<qint64>(45000, present_interval_us + 4000);
    else if (qstrcmp(stage, "start_frame_block") == 0)
        threshold_us = 12000;
    else if (qstrcmp(stage, "submit_call") == 0)
        threshold_us = 8000;
    else if (qstrcmp(stage, "swap_call") == 0)
        threshold_us = 8000;
    else
        threshold_us = 12000;

    if (duration_us < threshold_us)
        return;

    qCDebug(chiakiGui).nospace()
    << "[outlier] stage=" << stage
        << " duration_us=" << duration_us
        << " threshold_us=" << threshold_us
        << " pacing_mode=" << (deferred_present ? "deferred" : "immediate")
        << " swapchain_mode=" << (present_vsync_enabled ? "FIFO" : "IMMEDIATE")
        << " target_interval_us=" << present_interval_us
        << " queue_depth=" << queue_depth
        << " depth_limit=" << depth_limit
        << " pending_frame_waiting=" << (pending_frame_waiting ? 1 : 0)
        << " render_active=" << (render_active_now ? 1 : 0)
        << " render_scheduled=" << (render_scheduled_now ? 1 : 0)
        << " render_pending=" << (render_pending_now ? 1 : 0)
        << " ts_us=" << now_us;
}

static void logDeferredPresentRetry(qint64 now_us, bool render_active_now, bool render_scheduled_now, bool retry_armed)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static int retries = 0;
    static int armed_retries = 0;

    retries++;
    if (retry_armed)
        armed_retries++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000) {
        qCDebug(chiakiGui).nospace()
            << "[outlier] stage=deferred_present_retry"
            << " retries=" << retries
            << " armed_retries=" << armed_retries
            << " render_active=" << (render_active_now ? 1 : 0)
            << " render_scheduled=" << (render_scheduled_now ? 1 : 0)
            << " ts_us=" << now_us;
        last_log_us = now_us;
        retries = 0;
        armed_retries = 0;
    }
}

static void logDeferredPresentDecision(qint64 now_us,
                                       qint64 target_us,
                                       qint64 target_wakeup_us,
                                       qint64 remaining_to_target_us,
                                       qint64 start_frame_block_estimate_us,
                                       qint64 lead_us,
                                       qint64 extra_lead_us,
                                       bool deferred)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_remaining_us = 0;
    static qint64 max_remaining_us = 0;
    static qint64 min_estimate_us = 0;
    static qint64 max_estimate_us = 0;
    static qint64 min_late_budget_us = 0;
    static qint64 max_late_budget_us = 0;
    static qint64 sum_remaining_us = 0;
    static qint64 sum_estimate_us = 0;
    static int deferred_samples = 0;
    static int immediate_samples = 0;
    static int near_deadline_samples = 0;
    static int samples = 0;

    const qint64 late_budget_us = remaining_to_target_us - qMax<qint64>(0, start_frame_block_estimate_us);

    if (min_remaining_us == 0 || remaining_to_target_us < min_remaining_us)
        min_remaining_us = remaining_to_target_us;
    if (remaining_to_target_us > max_remaining_us)
        max_remaining_us = remaining_to_target_us;
    if (min_estimate_us == 0 || start_frame_block_estimate_us < min_estimate_us)
        min_estimate_us = start_frame_block_estimate_us;
    if (start_frame_block_estimate_us > max_estimate_us)
        max_estimate_us = start_frame_block_estimate_us;
    if (min_late_budget_us == 0 || late_budget_us < min_late_budget_us)
        min_late_budget_us = late_budget_us;
    if (late_budget_us > max_late_budget_us)
        max_late_budget_us = late_budget_us;

    sum_remaining_us += remaining_to_target_us;
    sum_estimate_us += start_frame_block_estimate_us;
    if (deferred)
        deferred_samples++;
    else
        immediate_samples++;
    if (late_budget_us <= 4000)
        near_deadline_samples++;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[defer] decision"
            << " deferred_samples=" << deferred_samples
            << " immediate_samples=" << immediate_samples
            << " near_deadline_samples=" << near_deadline_samples
            << " remaining_us_min=" << min_remaining_us
            << " remaining_us_max=" << max_remaining_us
            << " remaining_us_avg=" << (sum_remaining_us / samples)
            << " estimate_us_min=" << min_estimate_us
            << " estimate_us_max=" << max_estimate_us
            << " estimate_us_avg=" << (sum_estimate_us / samples)
            << " budget_us_min=" << min_late_budget_us
            << " budget_us_max=" << max_late_budget_us
            << " last_target_us=" << target_us
            << " last_wakeup_target_us=" << target_wakeup_us
            << " last_lead_us=" << lead_us
            << " extra_lead_us=" << extra_lead_us;
        last_log_us = now_us;
        min_remaining_us = 0;
        max_remaining_us = 0;
        min_estimate_us = 0;
        max_estimate_us = 0;
        min_late_budget_us = 0;
        max_late_budget_us = 0;
        sum_remaining_us = 0;
        sum_estimate_us = 0;
        deferred_samples = 0;
        immediate_samples = 0;
        near_deadline_samples = 0;
        samples = 0;
    }
}

// Estimated submit+swap cost (microseconds) used to compensate throttle sleeps.
static std::atomic<long long> g_submit_swap_est_us{1500LL};
static const long long g_submit_swap_spin_margin_us = 1500LL;

static void logDeferredTimerWakeup(qint64 timer_fire_us,
                                   qint64 target_wakeup_us,
                                   qint64 target_us,
                                   qint64 extra_lead_us,
                                   bool render_active_now,
                                   bool render_scheduled_now)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_late_us = 0;
    static qint64 max_late_us = 0;
    static qint64 sum_late_us = 0;
    static qint64 max_target_slack_us = 0;
    static int blocked_samples = 0;
    static int samples = 0;

    const qint64 late_us = target_wakeup_us > 0 ? qMax<qint64>(0, timer_fire_us - target_wakeup_us) : -1;
    const qint64 target_slack_us = target_us > 0 ? (target_us - timer_fire_us) : -1;

    if (late_us >= 0) {
        if (min_late_us == 0 || late_us < min_late_us)
            min_late_us = late_us;
        if (late_us > max_late_us)
            max_late_us = late_us;
        sum_late_us += late_us;
    }
    if (target_slack_us > max_target_slack_us)
        max_target_slack_us = target_slack_us;
    if (render_active_now || render_scheduled_now)
        blocked_samples++;
    samples++;

    if (last_log_us == 0)
        last_log_us = timer_fire_us;
    else if (timer_fire_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[defer] timer_wakeup"
            << " late_us_min=" << min_late_us
            << " late_us_max=" << max_late_us
            << " late_us_avg=" << (samples > 0 ? (sum_late_us / samples) : 0)
            << " blocked_samples=" << blocked_samples
            << " extra_lead_us=" << extra_lead_us
            << " target_slack_us_last=" << target_slack_us
            << " target_slack_us_max=" << max_target_slack_us
            << " target_wakeup_us_last=" << target_wakeup_us
            << " target_us_last=" << target_us;
        last_log_us = timer_fire_us;
        min_late_us = 0;
        max_late_us = 0;
        sum_late_us = 0;
        max_target_slack_us = 0;
        blocked_samples = 0;
        samples = 0;
    }
}

static void logBufferedPacingStats(qint64 now_us, qint64 interval_us, qint64 delta_us, bool timer_owned_playback, bool pace_timer_active)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_delta_us = 0;
    static qint64 max_delta_us = 0;
    static qint64 sum_delta_us = 0;
    static qint64 max_late_us = 0;
    static qint64 max_missed_intervals = 0;
    static int samples = 0;
    static int late_samples = 0;

    if (!timer_owned_playback)
        return;

    if (min_delta_us == 0 || delta_us < min_delta_us)
        min_delta_us = delta_us;
    if (delta_us > max_delta_us)
        max_delta_us = delta_us;
    if (delta_us < 0) {
        const qint64 late_us = -delta_us;
        if (late_us > max_late_us)
            max_late_us = late_us;
        const qint64 missed_intervals = interval_us > 0 ? (late_us / interval_us) + 1 : 1;
        if (missed_intervals > max_missed_intervals)
            max_missed_intervals = missed_intervals;
        late_samples++;
    }
    sum_delta_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[pace] timer_owned interval_us=" << interval_us
            << " delta_us_min=" << min_delta_us
            << " delta_us_max=" << max_delta_us
            << " delta_us_avg=" << (sum_delta_us / samples)
            << " late_samples=" << late_samples
            << " max_late_us=" << max_late_us
            << " max_missed_intervals=" << max_missed_intervals
            << " pace_timer_active=" << pace_timer_active;
        last_log_us = now_us;
        min_delta_us = 0;
        max_delta_us = 0;
        sum_delta_us = 0;
        max_late_us = 0;
        max_missed_intervals = 0;
        samples = 0;
        late_samples = 0;
    }
}

static void logPresentSubmitStats(qint64 now_us, bool present, bool timer_owned_playback, qint64 interval_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_sample_us = 0;
    static qint64 last_log_us = 0;
    static qint64 min_interval_us = 0;
    static qint64 max_interval_us = 0;
    static qint64 sum_interval_us = 0;
    static int samples = 0;
    static int present_samples = 0;

    if (!present)
        return;

    if (last_sample_us != 0) {
        const qint64 interval = now_us - last_sample_us;
        if (min_interval_us == 0 || interval < min_interval_us)
            min_interval_us = interval;
        if (interval > max_interval_us)
            max_interval_us = interval;
        sum_interval_us += interval;
        samples++;
    }
    last_sample_us = now_us;
    present_samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[present] submit interval_us_min=" << min_interval_us
            << " interval_us_max=" << max_interval_us
            << " interval_us_avg=" << (sum_interval_us / samples)
            << " submits=" << samples
            << " present_samples=" << present_samples
            << " timer_owned_playback=" << timer_owned_playback
            << " target_interval_us=" << interval_us;
        last_log_us = now_us;
        min_interval_us = 0;
        max_interval_us = 0;
        sum_interval_us = 0;
        samples = 0;
        present_samples = 0;
    }
}

static bool shouldUseRefreshPacedBuffering(bool stream_active, int schedule_frame_mixer_active)
{
    return stream_active && schedule_frame_mixer_active != 0;
}

static void logLatencyStats(const char *label, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    if (!label)
        return;

    static QMutex stats_mutex;
    QMutexLocker locker(&stats_mutex);

    static qint64 last_log_us_schedule_to_render = 0;
    static qint64 min_us_schedule_to_render = 0;
    static qint64 max_us_schedule_to_render = 0;
    static qint64 sum_us_schedule_to_render = 0;
    static int samples_schedule_to_render = 0;

    static qint64 last_log_us_render_to_swap = 0;
    static qint64 min_us_render_to_swap = 0;
    static qint64 max_us_render_to_swap = 0;
    static qint64 sum_us_render_to_swap = 0;
    static int samples_render_to_swap = 0;

    qint64 *last_log_us = nullptr;
    qint64 *min_us = nullptr;
    qint64 *max_us = nullptr;
    qint64 *sum_us = nullptr;
    int *samples = nullptr;

    if (qstrcmp(label, "schedule_to_render") == 0) {
        last_log_us = &last_log_us_schedule_to_render;
        min_us = &min_us_schedule_to_render;
        max_us = &max_us_schedule_to_render;
        sum_us = &sum_us_schedule_to_render;
        samples = &samples_schedule_to_render;
    } else {
        last_log_us = &last_log_us_render_to_swap;
        min_us = &min_us_render_to_swap;
        max_us = &max_us_render_to_swap;
        sum_us = &sum_us_render_to_swap;
        samples = &samples_render_to_swap;
    }

    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (*min_us == 0 || delta_us < *min_us)
        *min_us = delta_us;
    if (delta_us > *max_us)
        *max_us = delta_us;
    *sum_us += delta_us;
    (*samples)++;

    if (*last_log_us == 0)
        *last_log_us = now_us;
    else if (now_us - *last_log_us >= 1000000 && *samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] " << label
            << " min_us=" << *min_us
            << " max_us=" << *max_us
            << " avg_us=" << (*sum_us / *samples)
            << " samples=" << *samples;
        *last_log_us = now_us;
        *min_us = 0;
        *max_us = 0;
        *sum_us = 0;
        *samples = 0;
    }
}

static void logScheduleToUpdateStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] schedule_to_update min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logUpdateToRenderStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] update_to_render min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logRenderPendingStats(qint64 now_us, const char *reason, bool render_active_now, int pending_during_cycle, qint64 swap_age_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static int pending_count = 0;

    pending_count++;
    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] render_pending count=" << pending_count
            << " reason=" << reason
            << " render_active=" << render_active_now
            << " pending_during_cycle=" << pending_during_cycle
            << " swap_age_us=" << swap_age_us;
        last_log_us = now_us;
        pending_count = 0;
    }
}

static void logPendingFrameLifecycle(const char *stage, double pts, float duration, qint64 age_us, qint64 idle_gap_us, int queue_depth, int depth_limit, bool render_active_now, bool has_capacity)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static int total = 0;
    static int stored = 0;
    static int promoted = 0;
    static int deferred = 0;
    static int consumed = 0;
    static int blocked = 0;

    total++;
    if (strcmp(stage, "stored") == 0)
        stored++;
    else if (strcmp(stage, "promoted") == 0)
        promoted++;
    else if (strcmp(stage, "deferred") == 0)
        deferred++;
    else if (strcmp(stage, "consumed") == 0)
        consumed++;
    else if (strcmp(stage, "blocked") == 0)
        blocked++;

    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000)
        return;

    CHIAKI_NOISY_DEBUG().nospace()
        << "[latency] pending_lifecycle stage=" << stage
        << " total=" << total
            << " stored=" << stored
            << " promoted=" << promoted
            << " deferred=" << deferred
            << " consumed=" << consumed
            << " blocked=" << blocked
            << " pts=" << pts
            << " duration=" << duration
            << " age_us=" << age_us
            << " idle_gap_us=" << idle_gap_us
            << " queue_depth=" << queue_depth
            << " depth_limit=" << depth_limit
        << " render_active=" << render_active_now
        << " has_capacity=" << has_capacity;

    total = 0;
    stored = 0;
    promoted = 0;
    deferred = 0;
    consumed = 0;
    blocked = 0;
    last_log_us = now_us;
}

static void logPendingFrameTransition(const char *event,
                                      bool pending_now,
                                      double pts,
                                      qint64 age_us,
                                      int queue_depth,
                                      int depth_limit,
                                      bool render_active_now,
                                      bool render_scheduled_now,
                                      bool render_pending_now,
                                      bool replaced_existing)
{
    if (!chiakiGui().isDebugEnabled())
        return;

    CHIAKI_NOISY_DEBUG().nospace()
        << "[latency] pending_transition event=" << event
        << " pending_now=" << pending_now
        << " pts=" << pts
        << " age_us=" << age_us
        << " queue_depth=" << queue_depth
        << " depth_limit=" << depth_limit
        << " render_active=" << render_active_now
        << " render_scheduled=" << render_scheduled_now
        << " render_pending=" << render_pending_now
        << " replaced_existing=" << replaced_existing;
}

static void logPendingFrameGate(const char *stage,
                                const char *reason,
                                double pts,
                                qint64 age_us,
                                int queue_depth,
                                int depth_limit,
                                bool render_active_now,
                                bool render_scheduled_now,
                                bool render_pending_now,
                                bool pending_exists,
                                bool has_capacity)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static int total = 0;
    static int render_busy = 0;
    static int queue_full = 0;
    static int other = 0;

    total++;
    if (strcmp(reason, "render_busy") == 0)
        render_busy++;
    else if (strcmp(reason, "queue_full") == 0)
        queue_full++;
    else
        other++;

    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000)
        return;

    CHIAKI_NOISY_DEBUG().nospace()
        << "[latency] pending_gate stage=" << stage
        << " reason=" << reason
        << " total=" << total
        << " render_busy=" << render_busy
        << " queue_full=" << queue_full
        << " other=" << other
        << " pts=" << pts
        << " age_us=" << age_us
        << " queue_depth=" << queue_depth
        << " depth_limit=" << depth_limit
        << " render_active=" << render_active_now
        << " render_scheduled=" << render_scheduled_now
        << " render_pending=" << render_pending_now
        << " pending_exists=" << pending_exists
        << " has_capacity=" << has_capacity;

    total = 0;
    render_busy = 0;
    queue_full = 0;
    other = 0;
    last_log_us = now_us;
}

static void logPendingQueueDepthHold(const char *stage,
                                     qint64 now_us,
                                     int queue_depth,
                                     int depth_limit,
                                     bool pending_exists,
                                     bool render_active_now,
                                     bool render_scheduled_now,
                                     bool render_pending_now)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 hold_start_us = 0;
    static qint64 last_log_us = 0;
    static int hold_samples = 0;
    static int max_queue_depth = 0;

    const bool holding = pending_exists && queue_depth >= depth_limit;
    if (holding) {
        if (hold_start_us == 0) {
            hold_start_us = now_us;
            hold_samples = 0;
            max_queue_depth = queue_depth;
        }
        hold_samples++;
        if (queue_depth > max_queue_depth)
            max_queue_depth = queue_depth;
        if (last_log_us != 0 && now_us - last_log_us < 1000000)
            return;
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] pending_queue_hold stage=" << stage
            << " duration_us=" << (now_us - hold_start_us)
            << " samples=" << hold_samples
            << " queue_depth=" << queue_depth
            << " depth_limit=" << depth_limit
            << " max_queue_depth=" << max_queue_depth
            << " render_active=" << render_active_now
            << " render_scheduled=" << render_scheduled_now
            << " render_pending=" << render_pending_now;
        last_log_us = now_us;
        return;
    }

    if (hold_start_us == 0)
        return;

    CHIAKI_NOISY_DEBUG().nospace()
        << "[latency] pending_queue_release stage=" << stage
        << " duration_us=" << (now_us - hold_start_us)
        << " samples=" << hold_samples
        << " queue_depth=" << queue_depth
        << " depth_limit=" << depth_limit
        << " max_queue_depth=" << max_queue_depth
        << " render_active=" << render_active_now
        << " render_scheduled=" << render_scheduled_now
        << " render_pending=" << render_pending_now;

    hold_start_us = 0;
    last_log_us = now_us;
    hold_samples = 0;
    max_queue_depth = 0;
}

static void logRenderPhaseStall(qint64 now_us,
                                const char *phase,
                                qint64 delta_us,
                                int queue_depth,
                                int depth_limit,
                                bool pending_frame_waiting,
                                bool render_active_now,
                                bool render_scheduled_now,
                                bool render_pending_now)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;
    static int max_queue_depth = 0;
    static int max_depth_limit = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;
    if (queue_depth > max_queue_depth)
        max_queue_depth = queue_depth;
    if (depth_limit > max_depth_limit)
        max_depth_limit = depth_limit;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] render_phase_stall phase=" << phase
            << " min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples
            << " max_queue_depth=" << max_queue_depth
            << " depth_limit=" << max_depth_limit
            << " pending_frame_waiting=" << pending_frame_waiting
            << " render_active=" << render_active_now
            << " render_scheduled=" << render_scheduled_now
            << " render_pending=" << render_pending_now;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
        max_queue_depth = 0;
        max_depth_limit = 0;
    }
}

static void logRenderHandoffStats(qint64 now_us,
                                  qint64 idle_age_us,
                                  qint64 request_age_us,
                                  int queue_depth,
                                  bool pending_frame_waiting,
                                  bool render_active_now,
                                  bool render_scheduled_now,
                                  bool render_pending_now)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_idle_age_us = 0;
    static qint64 max_idle_age_us = 0;
    static qint64 sum_idle_age_us = 0;
    static qint64 min_request_age_us = 0;
    static qint64 max_request_age_us = 0;
    static qint64 sum_request_age_us = 0;
    static int samples = 0;
    static int max_queue_depth = 0;

    if (min_idle_age_us == 0 || idle_age_us < min_idle_age_us)
        min_idle_age_us = idle_age_us;
    if (idle_age_us > max_idle_age_us)
        max_idle_age_us = idle_age_us;
    sum_idle_age_us += idle_age_us;

    if (min_request_age_us == 0 || request_age_us < min_request_age_us)
        min_request_age_us = request_age_us;
    if (request_age_us > max_request_age_us)
        max_request_age_us = request_age_us;
    sum_request_age_us += request_age_us;

    if (queue_depth > max_queue_depth)
        max_queue_depth = queue_depth;
    samples++;

    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000 || samples == 0)
        return;

    CHIAKI_NOISY_DEBUG().nospace()
        << "[latency] render_handoff idle_min_us=" << min_idle_age_us
        << " idle_max_us=" << max_idle_age_us
        << " idle_avg_us=" << (sum_idle_age_us / samples)
        << " request_min_us=" << min_request_age_us
        << " request_max_us=" << max_request_age_us
        << " request_avg_us=" << (sum_request_age_us / samples)
        << " samples=" << samples
        << " max_queue_depth=" << max_queue_depth
        << " pending_frame_waiting=" << pending_frame_waiting
        << " render_active=" << render_active_now
        << " render_scheduled=" << render_scheduled_now
        << " render_pending=" << render_pending_now;

    last_log_us = now_us;
    min_idle_age_us = 0;
    max_idle_age_us = 0;
    sum_idle_age_us = 0;
    min_request_age_us = 0;
    max_request_age_us = 0;
    sum_request_age_us = 0;
    samples = 0;
    max_queue_depth = 0;
}

static void logRenderQueueUpdateStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] render_queue_update min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickRenderStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] quick_render_work min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickBeginFrameStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] quick_begin_frame min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickBeginHoldStats(qint64 now_us, qint64 delta_us)
{
    if (!chiakiGui().isDebugEnabled())
        return;
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] quick_begin_hold min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickBeginWaitStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] quick_begin_wait min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickBeginQtStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] quick_begin_qt min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickEndFrameStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] quick_end_frame min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickEndQtStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] quick_end_qt min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logQuickReleaseStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] quick_release min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logSwapToStartGapStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] swap_to_start_gap min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logRenderSetupStats(qint64 now_us, qint64 delta_us)
{
    static qint64 last_log_us = 0;
    static qint64 min_us = 0;
    static qint64 max_us = 0;
    static qint64 sum_us = 0;
    static int samples = 0;

    if (min_us == 0 || delta_us < min_us)
        min_us = delta_us;
    if (delta_us > max_us)
        max_us = delta_us;
    sum_us += delta_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] render_setup min_us=" << min_us
            << " max_us=" << max_us
            << " avg_us=" << (sum_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_us = 0;
        max_us = 0;
        sum_us = 0;
        samples = 0;
    }
}

static void logDroppedFrameReason(const char *reason, qint64 now_us, double pts, const char *detail = nullptr)
{
    Q_UNUSED(now_us);
    qCInfo(chiakiGui).nospace()
        << "[drop] reason=" << reason
        << " pts=" << pts
        << (detail ? " detail=" : "")
        << (detail ? detail : "");
}

static const char *updateRequestReasonName(int reason)
{
    switch (static_cast<QmlMainWindow::UpdateRequestReason>(reason)) {
    case QmlMainWindow::UpdateRequestReason::SceneChanged:
        return "scene_changed";
    case QmlMainWindow::UpdateRequestReason::RenderRequested:
        return "render_requested";
    case QmlMainWindow::UpdateRequestReason::Timer:
        return "timer";
    case QmlMainWindow::UpdateRequestReason::QueueStoredFrame:
        return "queue_stored_frame";
    case QmlMainWindow::UpdateRequestReason::QueueReset:
        return "queue_reset";
    case QmlMainWindow::UpdateRequestReason::PendingFrame:
        return "pending_frame";
    case QmlMainWindow::UpdateRequestReason::Replay:
        return "replay";
    case QmlMainWindow::UpdateRequestReason::PlaceboReset:
        return "placebo_reset";
    case QmlMainWindow::UpdateRequestReason::FinalizePending:
        return "finalize_pending";
    case QmlMainWindow::UpdateRequestReason::NoSwapchain:
        return "no_swapchain";
    case QmlMainWindow::UpdateRequestReason::VSync:
        return "vsync";
    case QmlMainWindow::UpdateRequestReason::Unknown:
    default:
        return "unknown";
    }
}

[[maybe_unused]] static void logUpdateRequestReasonStats(int reason)
{
    static qint64 last_log_us = 0;
    static std::array<int, 11> blocked_counts{};
    static int total_blocked = 0;

    const int idx = static_cast<int>(reason);
    if (idx >= 0 && idx < static_cast<int>(blocked_counts.size())) {
        blocked_counts[idx]++;
        total_blocked++;
    }

    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000)
        return;

    CHIAKI_NOISY_DEBUG().nospace()
        << "[latency] update_blocked total=" << total_blocked
        << " scene_changed=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::SceneChanged)]
        << " render_requested=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::RenderRequested)]
        << " timer=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::Timer)]
        << " queue_stored_frame=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::QueueStoredFrame)]
        << " queue_reset=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::QueueReset)]
        << " pending_frame=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::PendingFrame)]
        << " replay=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::Replay)]
        << " placebo_reset=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::PlaceboReset)]
        << " finalize_pending=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::FinalizePending)]
        << " no_swapchain=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::NoSwapchain)]
        << " vsync=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::VSync)]
        << " unknown=" << blocked_counts[static_cast<int>(QmlMainWindow::UpdateRequestReason::Unknown)];

    blocked_counts.fill(0);
    total_blocked = 0;
    last_log_us = now_us;
}

static void logRenderRequestedSignalStats(bool render_scheduled_now, bool render_active_now, bool quick_need_render_now)
{
    static qint64 last_log_us = 0;
    static int total = 0;
    static int while_scheduled = 0;
    static int while_active = 0;
    static int while_both = 0;
    static int while_idle = 0;

    total++;
    if (render_scheduled_now && render_active_now)
        while_both++;
    else if (render_scheduled_now)
        while_scheduled++;
    else if (render_active_now)
        while_active++;
    else
        while_idle++;

    Q_UNUSED(quick_need_render_now);

    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000)
        return;

    CHIAKI_NOISY_DEBUG().nospace()
        << "[latency] render_requested_signal total=" << total
        << " while_scheduled=" << while_scheduled
        << " while_active=" << while_active
        << " while_both=" << while_both
        << " while_idle=" << while_idle;

    total = 0;
    while_scheduled = 0;
    while_active = 0;
    while_both = 0;
    while_idle = 0;
    last_log_us = now_us;
}

static void detectOpenGLGpuVendor(QmlMainWindow *window)
{
    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return;

    const GLubyte *vendor_raw = ctx->extraFunctions()->glGetString(GL_VENDOR);
    if (!vendor_raw)
        return;

    QByteArray vendor = QByteArray(reinterpret_cast<const char *>(vendor_raw)).toLower();
    if (vendor.contains("amd") || vendor.contains("ati"))
    {
        window->setProperty("_chiaki_detected_amd", true);
        qCInfo(chiakiGui) << "Using amd graphics card";
    }
    else if (vendor.contains("nvidia"))
    {
        window->setProperty("_chiaki_detected_nvidia", true);
        qCInfo(chiakiGui) << "Using nvidia graphics card";
    }
}

static void logRenderIdleStats(qint64 now_us, qint64 render_duration_us, bool render_pending_now, bool pending_frame_waiting_now, int pending_during_cycle)
{
    static qint64 last_log_us = 0;
    static qint64 min_duration_us = 0;
    static qint64 max_duration_us = 0;
    static qint64 sum_duration_us = 0;
    static int samples = 0;
    static int pending_samples = 0;
    static int pending_frame_samples = 0;
    static int pending_cycle_samples = 0;

    if (min_duration_us == 0 || render_duration_us < min_duration_us)
        min_duration_us = render_duration_us;
    if (render_duration_us > max_duration_us)
        max_duration_us = render_duration_us;
    sum_duration_us += render_duration_us;
    samples++;
    if (render_pending_now)
        pending_samples++;
    if (pending_frame_waiting_now)
        pending_frame_samples++;
    if (pending_during_cycle > 0)
        pending_cycle_samples++;

    if (last_log_us == 0) {
        last_log_us = now_us;
        return;
    }
    if (now_us - last_log_us < 1000000 || samples == 0)
        return;

    CHIAKI_NOISY_DEBUG().nospace()
        << "[latency] render_idle duration_us_min=" << min_duration_us
        << " duration_us_max=" << max_duration_us
        << " duration_us_avg=" << (sum_duration_us / samples)
        << " samples=" << samples
        << " pending_samples=" << pending_samples
        << " pending_frame_samples=" << pending_frame_samples
        << " pending_cycle_samples=" << pending_cycle_samples;

    last_log_us = now_us;
    min_duration_us = 0;
    max_duration_us = 0;
    sum_duration_us = 0;
    samples = 0;
    pending_samples = 0;
    pending_frame_samples = 0;
    pending_cycle_samples = 0;
}

static void placebo_log_cb(void *user, pl_log_level level, const char *msg)
{
    (void)user;
    if (detailedVerboseTelemetryEnabled() &&
        (level == PL_LOG_TRACE || level == PL_LOG_DEBUG)) {
        const uint64_t now_us = chiaki_time_now_monotonic_us();
        if (now_us < placebo_verbose_log_quiet_until_us.load(std::memory_order_relaxed))
            return;
    }

    ChiakiLogLevel chiaki_level;
    switch (level) {
    case PL_LOG_NONE:
    case PL_LOG_TRACE:
    case PL_LOG_DEBUG:
        qCDebug(chiakiGui).noquote() << "[libplacebo]" << msg;
        break;
    case PL_LOG_INFO:
        qCInfo(chiakiGui).noquote() << "[libplacebo]" << msg;
        break;
    case PL_LOG_WARN:
        qCWarning(chiakiGui).noquote() << "[libplacebo]" << msg;
        break;
    case PL_LOG_ERR:
    case PL_LOG_FATAL:
        qCCritical(chiakiGui).noquote() << "[libplacebo]" << msg;
        break;
    }
}

static bool map_frame(pl_gpu gpu, pl_tex *tex,
                      const struct pl_source_frame *src,
                      struct pl_frame *out_frame)
{
    AVFrame *frame = reinterpret_cast<AVFrame *>(src->frame_data);
    QmlMainWindow *q = reinterpret_cast<QmlMainWindow *>(frame->opaque);
    pl_avframe_params av_params{
        .frame      = frame,
        .tex        = tex,};
    const qint64 map_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    bool ok = pl_map_avframe_ex(gpu, out_frame, &av_params
    );
    const qint64 map_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (map_end_us >= map_begin_us && map_end_us - map_begin_us >= 500) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] pl_map_avframe_ex_us=" << (map_end_us - map_begin_us)
            << " zero_copy=" << ((q && q->getBackend() && q->getBackend()->zeroCopy()) ? 1 : 0)
            << " fmt=" << frame->format;
    }

    if (!ok) {
        fprintf(stderr, "Failed mapping AVFrame!\n");
        qCWarning(chiakiGui) << "Failed to map AVFrame to Placebo frame!";
        if(q->getBackend() && q->getBackend()->zeroCopy())
        {
            qCInfo(chiakiGui) << "Mapping frame failed, trying without zero copy!";
            q->getBackend()->disableZeroCopy();
        }
        av_frame_free(&frame);
        return false;
    }
    if (out_frame->num_planes <= 0) {
        qCInfo(chiakiGui) << "Mapped frame contains no planes, dropping";
        pl_unmap_avframe(gpu, out_frame);
        av_frame_free(&frame);
        return false;
    }
    return true;
}

static void unmap_frame(pl_gpu gpu, struct pl_frame *frame,
                        const struct pl_source_frame *src)
{
    pl_unmap_avframe(gpu, frame);
    AVFrame *av_frame = reinterpret_cast<AVFrame *>(src->frame_data);
    av_frame_free(&av_frame);
}

static AVBufferRef *startupWarmupFrameMarker();

static void discard_frame(const struct pl_source_frame *src)
{
    AVFrame *frame = reinterpret_cast<AVFrame *>(src->frame_data);
    AVBufferRef *marker = startupWarmupFrameMarker();
    if (marker && frame && frame->opaque_ref && frame->opaque_ref->data == marker->data) {
        av_frame_free(&frame);
        return;
    }
    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    QmlMainWindow *q = frame ? reinterpret_cast<QmlMainWindow *>(frame->opaque) : nullptr;
    const QString detail = q ? q->describePlaceboDiscardReason(now_us)
                             : QStringLiteral("cause=owner_missing");
    const QByteArray detail_utf8 = detail.toUtf8();
    logDroppedFrameReason("placebo_discard", now_us, src->pts, detail_utf8.constData());
    if (q)
        q->increaseDroppedFrames();
    av_frame_free(&frame);
    qCInfo(chiakiGui) << "Dropped frame with PTS" << src->pts;
}

static void discard_snapshot_frame(const struct pl_source_frame *src)
{
    AVFrame *frame = reinterpret_cast<AVFrame *>(src->frame_data);
    av_frame_free(&frame);
}

static AVBufferRef *startupWarmupFrameMarker()
{
    static AVBufferRef *marker = av_buffer_alloc(1);
    return marker;
}

static AVFrame *clone_frame_for_replay(const AVFrame *frame)
{
    if (!frame)
        return nullptr;

    return av_frame_clone(frame);
}

static AVFrame *make_fallback_snapshot_frame(const AVFrame *frame)
{
    if (!frame)
        return nullptr;

    if (!frame->hw_frames_ctx || frame->format == AV_PIX_FMT_VULKAN)
        return nullptr;

    AVFrame *copy = av_frame_alloc();
    if (!copy)
        return nullptr;

    if (av_hwframe_transfer_data(copy, frame, 0) < 0) {
        av_frame_free(&copy);
        return av_frame_clone(frame);
    }

    int sw_format = copy->format;
    int sw_width = copy->width;
    int sw_height = copy->height;
    AVRational sw_sar = copy->sample_aspect_ratio;
    AVColorSpace sw_colorspace = copy->colorspace;
    AVColorRange sw_color_range = copy->color_range;
    AVColorPrimaries sw_color_primaries = copy->color_primaries;
    AVColorTransferCharacteristic sw_color_trc = copy->color_trc;
    av_frame_copy_props(copy, frame);
    copy->format = sw_format >= 0 ? sw_format : frame->format;
    copy->width = sw_width;
    copy->height = sw_height;
    copy->sample_aspect_ratio = sw_sar;
    copy->colorspace = sw_colorspace;
    copy->color_range = sw_color_range;
    copy->color_primaries = sw_color_primaries;
    copy->color_trc = sw_color_trc;
    if (copy->hw_frames_ctx) {
        av_buffer_unref(&copy->hw_frames_ctx);
        copy->hw_frames_ctx = nullptr;
    }
    return copy;
}

static AVFrame *make_startup_warmup_frame(unsigned width, unsigned height, bool hdr)
{
    if (width == 0 || height == 0)
        return nullptr;

    const enum AVPixelFormat format = hdr ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return nullptr;

    AVBufferRef *marker = startupWarmupFrameMarker();
    if (marker)
        frame->opaque_ref = av_buffer_ref(marker);
    frame->format = format;
    frame->width = static_cast<int>(width);
    frame->height = static_cast<int>(height);
    frame->sample_aspect_ratio = AVRational{1, 1};
    frame->colorspace = hdr ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
    frame->color_range = AVCOL_RANGE_MPEG;
    frame->color_primaries = hdr ? AVCOL_PRI_BT2020 : AVCOL_PRI_BT709;
    frame->color_trc = hdr ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_BT709;

    if (av_frame_get_buffer(frame, 32) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }
    if (av_frame_make_writable(frame) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    const int chroma_width = (frame->width + 1) / 2;
    const int chroma_height = (frame->height + 1) / 2;
    if (format == AV_PIX_FMT_YUV420P) {
        for (int y = 0; y < frame->height; ++y)
            memset(frame->data[0] + y * frame->linesize[0], 16, frame->width);
        for (int y = 0; y < chroma_height; ++y) {
            memset(frame->data[1] + y * frame->linesize[1], 128, chroma_width);
            memset(frame->data[2] + y * frame->linesize[2], 128, chroma_width);
        }
    } else {
        for (int y = 0; y < frame->height; ++y) {
            auto *row = reinterpret_cast<uint16_t *>(frame->data[0] + y * frame->linesize[0]);
            for (int x = 0; x < frame->width; ++x)
                row[x] = 64;
        }
        for (int y = 0; y < chroma_height; ++y) {
            auto *row_u = reinterpret_cast<uint16_t *>(frame->data[1] + y * frame->linesize[1]);
            auto *row_v = reinterpret_cast<uint16_t *>(frame->data[2] + y * frame->linesize[2]);
            for (int x = 0; x < chroma_width; ++x) {
                row_u[x] = 512;
                row_v[x] = 512;
            }
        }
    }

    return frame;
}

static QString shader_cache_path()
{
    static QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pl_shader.cache";
    return path;
}


static const char *render_params_path()
{
    static QString path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/Chiaki/pl_render_params.conf";
    return qPrintable(path);
}

static const struct pl_hook *load_mpv_hook(pl_gpu gpu, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(chiakiGui) << "Failed to open shader resource" << path;
        return nullptr;
    }

    QByteArray shader = file.readAll();
    const struct pl_hook *hook = pl_mpv_user_shader_parse(gpu, shader.constData(), static_cast<size_t>(shader.size()));
    if (!hook)
        qCWarning(chiakiGui) << "Failed to parse shader resource" << path;
    return hook;
}

static bool uses_fsrcnnx(PlaceboUpscaler upscaler)
{
    return upscaler == PlaceboUpscaler::FSRCNNX8 || upscaler == PlaceboUpscaler::FSRCNNX16;
}

static constexpr float spatial_upscale_threshold = 1.2f;

static bool uses_custom_upscale_hook(PlaceboUpscaler upscaler)
{
    return upscaler == PlaceboUpscaler::FSR || uses_fsrcnnx(upscaler);
}

static const struct pl_hook *select_spatial_hook(QmlMainWindow::VideoPreset preset,
                                                 PlaceboUpscaler upscaler,
                                                 float upscale_factor,
                                                 const struct pl_hook *fsr_hook,
                                                 const struct pl_hook *hook8,
                                                 const struct pl_hook *hook16)
{
    if (upscale_factor <= 1.0f)
        return nullptr;

    switch (preset) {
    case QmlMainWindow::VideoPreset::HighQualitySpatial:
        return upscale_factor >= spatial_upscale_threshold ? hook8 : fsr_hook;
    case QmlMainWindow::VideoPreset::HighQualityAdvancedSpatial:
        return upscale_factor >= spatial_upscale_threshold ? hook16 : fsr_hook;
    case QmlMainWindow::VideoPreset::Custom:
        switch (upscaler) {
        case PlaceboUpscaler::FSR:
            return fsr_hook;
        case PlaceboUpscaler::FSRCNNX8:
            return upscale_factor >= spatial_upscale_threshold ? hook8 : fsr_hook;
        case PlaceboUpscaler::FSRCNNX16:
            return upscale_factor >= spatial_upscale_threshold ? hook16 : fsr_hook;
        default:
            return nullptr;
        }
    default:
        return nullptr;
    }
}

static void clearQuickOpenGLTarget(QOpenGLFramebufferObject *fbo)
{
    if (!fbo)
        return;

    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return;

    QOpenGLExtraFunctions *gl = ctx->extraFunctions();
    if (!gl)
        return;

    gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo->handle()));
    gl->glViewport(0, 0, fbo->width(), fbo->height());
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

pl_gpu QmlMainWindow::placeboGpu() const
{
    if (placebo_vulkan)
        return placebo_vulkan->gpu;
    if (placebo_opengl)
        return placebo_opengl->gpu;
    return nullptr;
}

class RenderControl : public QQuickRenderControl
{
public:
    explicit RenderControl(QWindow *window)
        : window(window)
    {
    }

    QWindow *renderWindow(QPoint *offset) override
    {
        Q_UNUSED(offset);
        return window;
    }

private:
    QWindow *window = {};
};

QmlMainWindow::QmlMainWindow(Settings *settings, bool exit_app_on_stream_exit)
    : QWindow()
    , settings(settings)
{
    init(settings, exit_app_on_stream_exit);
}

QmlMainWindow::QmlMainWindow(const StreamSessionConnectInfo &connect_info)
    : QWindow()
    , settings(connect_info.settings)
{
    direct_stream = true;
    emit directStreamChanged();
    init(connect_info.settings);
    backend->createSession(connect_info);

    if (connect_info.fullscreen || connect_info.zoom || connect_info.stretch)
        fullscreenTime();
    if (connect_info.zoom)
        setVideoMode(VideoMode::Zoom);
    else if (connect_info.stretch)
        setVideoMode(VideoMode::Stretch);

    connect(session, &StreamSession::SessionQuit, qGuiApp, &QGuiApplication::quit);
}

QmlMainWindow::~QmlMainWindow()
{
    Q_ASSERT(!placebo_swapchain);

    if (stats_overlay_widget) {
        stats_overlay_widget->hide();
        delete stats_overlay_widget;
        stats_overlay_widget = nullptr;
    }

    drainRenderThread();

    if (buffered_pace_thread) {
        buffered_pace_thread->stopThread();
        buffered_pace_thread->wait();
        delete buffered_pace_thread;
        buffered_pace_thread = nullptr;
    }
    if (present_pace_thread) {
        present_pace_thread->stopThread();
        present_pace_thread->wait();
        delete present_pace_thread;
        present_pace_thread = nullptr;
    }
    if (deferred_swap_thread) {
        deferred_swap_thread->stopThread();
        deferred_swap_thread->wait();
        delete deferred_swap_thread;
        deferred_swap_thread = nullptr;
    }

    if (render_backend == RenderBackend::Vulkan)
        av_buffer_unref(&vulkan_hw_dev_ctx);

    const bool isOpenGlBackend = render_backend == RenderBackend::OpenGL;
    bool hasOpenGlContextCurrent = isOpenGlBackend && makeOpenGLContextCurrent();

    if (isOpenGlBackend && quick_window)
        quick_window->setRenderTarget(QQuickRenderTarget());

#ifndef Q_OS_MACOS
    if (owns_render_thread) {
        QMetaObject::invokeMethod(quick_render, &QQuickRenderControl::invalidate);
        render_thread->quit();
        render_thread->wait();
    } else {
        quick_render->invalidate();
    }
#else
    // On macOS, we still need to invalidate to prevent leaks
    if (owns_render_thread) {
        QMetaObject::invokeMethod(quick_render, &QQuickRenderControl::invalidate);
        render_thread->quit();
        render_thread->wait();
    } else {
        quick_render->invalidate();
    }
#endif

    if (pl_gpu gpu = placeboGpu()) {
        pl_unmap_avframe(gpu, &direct_frame);
        if (quick_tex)
            pl_tex_destroy(gpu, &quick_tex);
        for (auto &tex : placebo_tex)
            pl_tex_destroy(gpu, &tex);
        if (render_backend == RenderBackend::Vulkan && quick_sem)
            pl_vulkan_sem_destroy(gpu, &quick_sem);
    }

    {
        QMutexLocker locker(&pending_frame_mutex);
        clearPendingFrameStateLocked();
    }
    av_frame_free(&direct_pending_frame);
    {
        QMutexLocker locker(&reset_seed_mutex);
        if (reset_seed_frame)
            av_frame_free(&reset_seed_frame);
        reset_seed_pts = 0.0;
        reset_seed_duration = 0.0f;
        reset_seed_generation = 0;
    }
    clearSnapshotFrame();

    // Only destroy the FBO while its OpenGL context is current.
    if (hasOpenGlContextCurrent) {
        delete quick_fbo;
    } else if (quick_fbo) {
        // Try one more time to make context current for proper cleanup
        if (isOpenGlBackend && makeOpenGLContextCurrent()) {
            delete quick_fbo;
            hasOpenGlContextCurrent = true;
        }
    }
    quick_fbo = nullptr;
    quick_tex = {};
    quick_sem = VK_NULL_HANDLE;
    quick_sem_value = 0;
    quick_vk_image = VK_NULL_HANDLE;
    quick_vk_format = VK_FORMAT_UNDEFINED;

    if (hasOpenGlContextCurrent)
        doneOpenGLContextCurrent();

    delete quick_item;
    delete quick_window;
    // calls invalidate here if not already called
    delete quick_render;
    if (owns_render_thread)
        delete render_thread->parent();
    delete qml_engine;

    FILE *file = fopen(qPrintable(shader_cache_path()), "wb");
    if (file) {
        pl_cache_save_file(placebo_cache, file);
        fclose(file);
    }
    pl_cache_destroy(&placebo_cache);
    pl_queue_destroy(&placebo_queue);
    pl_renderer_destroy(&placebo_renderer);
    if (render_backend == RenderBackend::Vulkan) {
        pl_vulkan_destroy(&placebo_vulkan);
        pl_vk_inst_destroy(&placebo_vk_inst);
    } else {
        pl_opengl_destroy(&placebo_opengl);
    }
    delete qt_vk_inst;
    delete qt_gl_offscreen_surface;
    delete qt_gl_context;
    pl_options_free(&renderparams_opts);
    pl_mpv_user_shader_destroy(&fsr_hook);
    pl_mpv_user_shader_destroy(&fsrcnnx_hook_8);
    pl_mpv_user_shader_destroy(&fsrcnnx_hook_16);
    pl_log_destroy(&placebo_log);
}

void QmlMainWindow::updateWindowType(WindowType type)
{
    switch (type) {
    case WindowType::SelectedResolution:
        setVideoMode(VideoMode::Normal);
        break;
    case WindowType::CustomResolution:
        setVideoMode(VideoMode::Normal);
        break;
    case WindowType::Fullscreen:
        fullscreenTime();
        setVideoMode(VideoMode::Normal);
        break;
    case WindowType::Zoom:
        fullscreenTime();
        setVideoMode(VideoMode::Zoom);
        break;
    case WindowType::Stretch:
        fullscreenTime();
        setVideoMode(VideoMode::Stretch);
        break;
    default:
        break;
    }
}

bool QmlMainWindow::hasVideo() const
{
    return has_video;
}

int QmlMainWindow::droppedFrames() const
{
    return dropped_frames;
}

void QmlMainWindow::increaseDroppedFrames()
{
    dropped_frames_current.fetchAndAddRelaxed(1);
}

bool QmlMainWindow::keepVideo() const
{
    return keep_video;
}

QmlBackend * QmlMainWindow::getBackend()
{
    return backend;
}

void QmlMainWindow::setKeepVideo(bool keep)
{
    keep_video = keep;
    emit keepVideoChanged();
}

void QmlMainWindow::grabInput()
{
    setCursor(Qt::ArrowCursor);
    grab_input++;
    if (session)
        session->BlockInput(grab_input);
}

void QmlMainWindow::releaseInput()
{
    if (!grab_input)
        return;
    grab_input--;
    if (!grab_input && has_video && settings->GetHideCursor())
        setCursor(Qt::BlankCursor);
    if (session)
        session->BlockInput(grab_input);
}

void QmlMainWindow::requestOverlayUpdate()
{
    scheduleUpdate(true, UpdateRequestReason::Unknown);
}

void QmlMainWindow::presentStartupWarmupFrame(unsigned width, unsigned height, bool hdr)
{
    if (has_video || width == 0 || height == 0)
        return;

    AVFrame *frame = make_startup_warmup_frame(width, height, hdr);
    if (!frame)
        return;
    startup_warmup_preserve_next_session_change = true;
    startup_warmup_frame_active = true;
    ChiakiFfmpegFrame warmup_frame = {
        .frame = frame,
        .pts = 0.0,
        .duration = source_frame_interval_ms > 0.0 ? source_frame_interval_ms / 1000.0 : 1.0 / 60.0,
        .recovered = false,
    };
    presentFrame(warmup_frame, 0);
    startup_warmup_frame_active = false;
}

void QmlMainWindow::armVerbosePlaceboQuietWindow()
{
    placebo_verbose_log_quiet_until_us.store(chiaki_time_now_monotonic_us() + 3000000, std::memory_order_relaxed);
}

void QmlMainWindow::setOverlayInteractionActive(bool active)
{
    const int previous = overlay_interaction_active.fetchAndStoreRelaxed(active ? 1 : 0);
    if (active) {
        armQuickNeedSync("overlay_interaction_active");
        scheduleUpdate(true, UpdateRequestReason::Unknown);
    } else if (previous != 0)
        scheduleUpdate(true, UpdateRequestReason::Unknown);
}

void QmlMainWindow::setStatsOverlayActive(bool active)
{
    const bool previous = stats_overlay_visible;
    stats_overlay_visible = active;
    if (stats_overlay_widget)
        stats_overlay_widget->setOverlayVisible(active);
    if (active)
        updateStatsOverlayGeometry();
    if (previous != stats_overlay_visible)
        emit statsOverlayActiveChanged();
}

void QmlMainWindow::noteLoadingTransitionComplete()
{
    armQuickNeedSync("loading_transition_complete");
}

void QmlMainWindow::updateStatsOverlayGeometry()
{
    if (stats_overlay_widget)
        stats_overlay_widget->syncGeometry();
}

bool QmlMainWindow::directStream() const
{
    return direct_stream;
}

QmlMainWindow::VideoMode QmlMainWindow::videoMode() const
{
    return video_mode;
}

void QmlMainWindow::setVideoMode(VideoMode mode)
{
    video_mode = mode;
    emit videoModeChanged();
}

float QmlMainWindow::zoomFactor() const
{
    return zoom_factor;
}

void QmlMainWindow::setZoomFactor(float factor)
{
    zoom_factor = factor;
    emit zoomFactorChanged();
}

QmlMainWindow::VideoPreset QmlMainWindow::videoPreset() const
{
    return video_preset;
}

void QmlMainWindow::setVideoPreset(VideoPreset preset)
{
    video_preset = preset;
    schedule_frame_mixer_active.storeRelaxed(configuredFrameMixerEnabledForScheduling() ? 1 : 0);
    emit videoPresetChanged();
}

void QmlMainWindow::setSettings(Settings *new_settings)
{
    settings = new_settings;
    QString profile = settings->GetCurrentProfile();
    qCCritical(chiakiGui) << "Current Profile: " << profile;
    if(profile.isEmpty())
        QGuiApplication::setApplicationDisplayName("chiaki-ng");
    else
        QGuiApplication::setApplicationDisplayName(QString("chiaki-ng:%1").arg(profile));
    this->setTitle(QGuiApplication::applicationDisplayName());
}

void QmlMainWindow::show()
{
    QQmlComponent component(qml_engine, QUrl(QStringLiteral("qrc:/Main.qml")));
    if (!component.isReady()) {
        qCCritical(chiakiGui) << "Component not ready\n" << component.errors();
        QMetaObject::invokeMethod(QGuiApplication::instance(), &QGuiApplication::quit, Qt::QueuedConnection);
        return;
    }

    QVariantMap props;
    props[QStringLiteral("parent")] = QVariant::fromValue(quick_window->contentItem());
    quick_item = qobject_cast<QQuickItem*>(component.createWithInitialProperties(props));
    if (!quick_item) {
        qCCritical(chiakiGui) << "Failed to create root item\n" << component.errors();
        QMetaObject::invokeMethod(QGuiApplication::instance(), &QGuiApplication::quit, Qt::QueuedConnection);
        return;
    }
    if (!pending_renderer_fallback_reason.isEmpty()) {
        const QString reason = pending_renderer_fallback_reason;
        pending_renderer_fallback_reason.clear();
        QMetaObject::invokeMethod(quick_item, "showRendererFallbackDialog",
                                  Q_ARG(QVariant, QVariant::fromValue(reason)));
    }
    auto screen_size = QGuiApplication::primaryScreen()->availableSize();
    resize(screen_size);

    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP") == "gamescope")
        fullscreenTime();
    else
    {
        if(!settings->GetGeometry().isEmpty())
        {
            setGeometry(settings->GetGeometry());
            showNormal();
        }
        else
            showMaximized();
        setWindowAdjustable(true);
    }
}

namespace {
constexpr int kDefaultQueueDepthLimit = 2;

constexpr double kPtsStaleMarginS = 0.0005; // 500 µs

static const struct pl_filter_config *frame_mixer_config(Settings *settings)
{
    if (!settings)
        return pl_find_filter_config("oversample", PL_FILTER_FRAME_MIXING);

    switch (settings->GetPlaceboFrameMixer()) {
    case PlaceboFrameMixer::None:
        return nullptr;
    case PlaceboFrameMixer::Oversample:
        return pl_find_filter_config("oversample", PL_FILTER_FRAME_MIXING);
    case PlaceboFrameMixer::Hermite:
        return pl_find_filter_config("hermite", PL_FILTER_FRAME_MIXING);
    case PlaceboFrameMixer::Linear:
        return pl_find_filter_config("linear", PL_FILTER_FRAME_MIXING);
    case PlaceboFrameMixer::Cubic:
        return pl_find_filter_config("cubic", PL_FILTER_FRAME_MIXING);
    }

    return pl_find_filter_config("oversample", PL_FILTER_FRAME_MIXING);
}

}

const struct pl_filter_config *QmlMainWindow::effectiveFrameMixerConfig(const struct pl_render_params *render_params) const
{
    if (bypass_frame_queue)
        return nullptr;
    Q_UNUSED(render_params);

    switch (video_preset) {
    case VideoPreset::Fast:
        return frame_mixer_config(settings);
    case VideoPreset::Custom: {
        return frame_mixer_config(settings);
    }
    case VideoPreset::Default:
    case VideoPreset::HighQuality:
    case VideoPreset::HighQualitySpatial:
    case VideoPreset::HighQualityAdvancedSpatial:
        return frame_mixer_config(settings);
    }

    return frame_mixer_config(settings);
}

bool QmlMainWindow::effectiveFrameMixerEnabled(const struct pl_render_params *render_params) const
{
    return effectiveFrameMixerConfig(render_params) != nullptr;
}

bool QmlMainWindow::configuredFrameMixerEnabledForScheduling() const
{
    if (bypass_frame_queue)
        return false;
    switch (video_preset) {
    case VideoPreset::Fast:
        return frame_mixer_config(settings) != nullptr;
    case VideoPreset::Custom: {
        return frame_mixer_config(settings) != nullptr;
    }
    case VideoPreset::Default:
    case VideoPreset::HighQuality:
    case VideoPreset::HighQualitySpatial:
    case VideoPreset::HighQualityAdvancedSpatial:
        return frame_mixer_config(settings) != nullptr;
    }

    return frame_mixer_config(settings) != nullptr;
}

void QmlMainWindow::presentFrame(ChiakiFfmpegFrame frame, int32_t frames_lost, qint64 decoder_delivery_us)
{
    const qint64 present_entry_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (decoder_delivery_us > 0 && present_entry_us >= decoder_delivery_us) {
        const qint64 gui_handoff_us = present_entry_us - decoder_delivery_us;
        if (gui_handoff_us >= 12000) {
            CHIAKI_NOISY_DEBUG().nospace()
                << "[decode] gui_handoff_outlier_us=" << gui_handoff_us
                << " pts=" << frame.pts;
        }
    }

    dropped_frames_current.fetchAndAddRelaxed(frames_lost);

    if (!frame.frame)
        return;

    if (bypass_frame_queue) {
        {
            QMutexLocker locker(&direct_frame_mutex);
            if (direct_pending_frame) {
                av_frame_free(&direct_pending_frame);
                dropped_frames_current.fetchAndAddRelaxed(1);
            }
            direct_pending_frame = frame.frame;
        }
        if (!startup_warmup_frame_active)
            startup_first_real_frame_queued_pending_visibility.storeRelease(1);
        scheduleUpdate(false, UpdateRequestReason::PendingFrame);
        return;
    }

    const bool synthetic_warmup_frame = startup_warmup_frame_active;
    double frame_queue_origin = synthetic_warmup_frame ? 0.0 : queue_pts_origin;

    if (frame.recovered)
    {
        const bool stored_pending = storePendingFrame(frame, true, synthetic_warmup_frame);
        if (stored_pending)
            snapshotPendingFrame();
        if (stored_pending && !synthetic_warmup_frame && !has_video && startup_first_real_frame_queued_pending_visibility.loadAcquire() == 0)
            startup_first_real_frame_queued_pending_visibility.storeRelease(1);
        if (frame.frame)
            av_frame_free(&frame.frame);
        bool need_reset = false;
        double queue_pts_origin_snapshot = -1.0;
        {
            QMutexLocker locker(&placebo_state_mutex);
            queue_pts_origin_snapshot = queue_pts_origin;
            double current_pts = queue_pts_origin;
            if (queue_pts_origin >= 0.0 && playback_started && ts_start) {
                const uint64_t now_us = chiaki_time_now_monotonic_us();
                current_pts += static_cast<double>(now_us - ts_start) / 1000000.0;
            }
            need_reset = queue_pts_origin < 0.0 || frame.pts + 1e-6 < current_pts;
        }
        if (need_reset && placebo_reset_pending.loadAcquire() == 0) {
            logPtsRollback("presentFrame/recovered", frame.pts, queue_pts_origin_snapshot, true, frame.duration * 1000.0);
            queuePlaceboReset(true);
        }
        if (need_reset)
            storeResetSeedFromPendingFrame(reset_seed_capture_generation.loadAcquire());
        return;
    }

    frame.frame->opaque = this;

    if (!synthetic_warmup_frame && frame.duration > 0.0f)
        source_frame_interval_ms = 1000.0 * frame.duration;
    bool deinterlace_enabled = false;
    auto armStartupVisibility = [this, synthetic_warmup_frame]() {
        if (!synthetic_warmup_frame && !has_video && startup_first_real_frame_queued_pending_visibility.loadAcquire() == 0)
            startup_first_real_frame_queued_pending_visibility.storeRelease(1);
    };
    if (!synthetic_warmup_frame) {
        if (!placebo_state_mutex.tryLock()) {
            if (storePendingFrame(frame, true, synthetic_warmup_frame)) {
                snapshotPendingFrame();
                armStartupVisibility();
            } else if (frame.frame) {
                av_frame_free(&frame.frame);
                return;
            }
            if (frame.frame)
                av_frame_free(&frame.frame);
            if (queue_stored_frame_pending.fetchAndStoreRelaxed(1) == 0)
                scheduleUpdate(false, UpdateRequestReason::PendingFrame);
            return;
        }
        struct PlaceboStateUnlocker
        {
            QMutex *mutex;
            ~PlaceboStateUnlocker()
            {
                if (mutex)
                    mutex->unlock();
            }
        } placebo_state_unlocker{&placebo_state_mutex};
        if (queue_pts_origin < 0.0 || frame.pts + 1e-6 < queue_pts_origin) {
            logPtsRollback("presentFrame/rollback", frame.pts, queue_pts_origin, false, frame.duration * 1000.0);
            pl_queue_reset(placebo_queue);
            resetQueueDepthTracking();
            ts_start = 0;
            queue_pts_origin = frame.pts;
            setQueuePtsOriginCached(queue_pts_origin);
            newest_queued_frame_pts = -1.0;
            playback_started = false;
            frame_queue_origin = queue_pts_origin;
            {
                QMutexLocker pending_locker(&pending_frame_mutex);
                prunePendingFramesBeforeLocked(frame.pts);
            }
        }
        deinterlace_enabled = this->renderparams_opts->params.deinterlace_params;
    } else {
        deinterlace_enabled = this->renderparams_opts->params.deinterlace_params;
    }

    struct pl_source_frame src_frame{
        .pts = frame.pts - frame_queue_origin,
        .duration = static_cast<float>(frame.duration),
        // allow soft-disabling deinterlacing at the source frame level
        .first_field = deinterlace_enabled
            ? pl_field_from_avframe(frame.frame)
                : PL_FIELD_NONE,
        .frame_data = frame.frame,
        .map = map_frame,
        .unmap = unmap_frame,
        .discard = discard_frame,
    };
    const int depth_limit = effectiveQueueDepthLimit();
    // In non-mixer mode the effective depth_limit is already 2, which is the
    // minimum libplacebo needs to advance past a displayed frame.
    const int submission_depth_limit = depth_limit;
    bool queued = false;
    bool depth_exceeded = false;
    bool promote_pending = false;
    const bool render_busy = render_active.loadAcquire() != 0;
    bool render_scheduled_now = false;
    bool render_pending_now = false;
    const bool pending_submission_active = pending_frame_submission_active.loadAcquire() != 0;
    const bool should_rearm_stored_frame = [&]() {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled_now = render_scheduled;
        render_pending_now = render_pending;
        return !render_busy &&
               !render_scheduled &&
               !render_pending &&
               !pending_submission_active &&
               queue_stored_frame_pending.loadAcquire() == 0 &&
               pending_frame_present.loadAcquire() == 0;
    }();
    const bool render_pipeline_busy = render_busy || render_scheduled_now || render_pending_now;
    AVFrame *promoted_frame = nullptr;
    double promoted_pts = 0.0;
    float promoted_duration = 0.0f;
    double promoted_queue_origin = 0.0;
    bool promoted_deinterlace_enabled = false;
    qint64 promoted_age_us = -1;
    int queue_depth_snapshot = -1;
    bool pending_exists_snapshot = false;
    bool promote_eligible_snapshot = false;
    bool has_capacity_snapshot = false;
    qint64 idle_gap_us_snapshot = -1;
    {
        QMutexLocker locker(&placebo_state_mutex);
        const int depth = pl_queue_num_frames(placebo_queue);
        queue_depth_snapshot = depth;
        const bool pending_exists = pending_frame_present.loadAcquire() != 0 || pending_submission_active;
        pending_exists_snapshot = pending_exists;
        const int tolerated_depth = submission_depth_limit + (pending_exists ? 1 : 0);
        depth_exceeded = depth >= tolerated_depth;
        has_capacity_snapshot = depth < submission_depth_limit;
        promote_eligible_snapshot = !render_pipeline_busy && pending_exists && has_capacity_snapshot;
        updateQueueDepthAverage(depth);
        if (promote_eligible_snapshot) {
            QMutexLocker pending_locker(&pending_frame_mutex);
            PendingFrameEntry promoted_entry;
            if (takePendingFrameLocked(promoted_entry)) {
                promote_pending = true;
                promoted_frame = promoted_entry.frame;
                promoted_pts = promoted_entry.pts;
                promoted_duration = promoted_entry.duration;
                promoted_queue_origin = promoted_entry.queue_origin;
                promoted_deinterlace_enabled = deinterlace_enabled;
                promoted_age_us = promoted_entry.stored_us > 0
                    ? static_cast<qint64>(chiaki_time_now_monotonic_us()) - static_cast<qint64>(promoted_entry.stored_us)
                    : -1;
                const bool pending_now = pending_frame != nullptr;

                logPendingFrameTransition("promoted_clear",
                                          pending_now,
                                          promoted_pts,
                                          promoted_age_us,
                                          depth,
                                          submission_depth_limit,
                                          render_busy,
                                          render_scheduled_now,
                                          render_pending_now,
                                          false);
            }
        }
        if (!depth_exceeded && !promote_pending && !pending_exists)
        {
            const qint64 push_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            pl_queue_push(placebo_queue, &src_frame);
            const qint64 push_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            logLatencyStats("pl_queue_push", push_end_us - push_begin_us);
            CHIAKI_NOISY_DEBUG().nospace()
                << "[latency] pl_queue_push_us=" << (push_end_us - push_begin_us)
                << " queue_depth=" << pl_queue_num_frames(placebo_queue);
            if (!synthetic_warmup_frame)
                newest_queued_frame_pts = qMax(newest_queued_frame_pts, frame.pts);
            queued = true;
        }
    }

    {
        const qint64 idle_us = last_render_idle_us.loadAcquire();
        const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        idle_gap_us_snapshot = idle_us > 0 ? qMax<qint64>(0, now_us - idle_us) : -1;
    }

    if (pending_exists_snapshot && !promote_eligible_snapshot) {
        const char *gate_reason = render_pipeline_busy ? "render_busy"
                                              : (!has_capacity_snapshot ? "queue_full" : "promotion_window");
        qint64 blocked_age_us = -1;
        double blocked_pts = 0.0;
        float blocked_duration = 0.0f;
        {
            QMutexLocker pending_locker(&pending_frame_mutex);
            if (pending_frame) {
                blocked_pts = pending_pts;
                blocked_duration = pending_duration;
                blocked_age_us = pending_frame_stored_us > 0
                    ? static_cast<qint64>(chiaki_time_now_monotonic_us()) - static_cast<qint64>(pending_frame_stored_us)
                    : -1;
            }
        }
        logPendingFrameLifecycle("blocked",
                                 blocked_pts,
                                 blocked_duration,
                                 blocked_age_us,
                                 idle_gap_us_snapshot,
                                 queue_depth_snapshot,
                                                                 submission_depth_limit,
                                   render_pipeline_busy,
                                 has_capacity_snapshot);
        logPendingFrameGate("blocked",
                            gate_reason,
                            blocked_pts,
                            blocked_age_us,
                            queue_depth_snapshot,
                                     submission_depth_limit,
                               render_pipeline_busy,
                            render_scheduled_now,
                            render_pending_now,
                            pending_exists_snapshot,
                            has_capacity_snapshot);
    }
    bool promoted_frame_queued = false;
    if (promote_pending) {
        if (promoted_frame) {
            struct pl_source_frame promoted_src_frame = {
                .pts = promoted_pts - promoted_queue_origin,
                .duration = promoted_duration,
                .first_field = promoted_deinterlace_enabled
                    ? pl_field_from_avframe(promoted_frame)
                    : PL_FIELD_NONE,
                .frame_data = promoted_frame,
                .map = map_frame,
                .unmap = unmap_frame,
                .discard = discard_frame,
            };
            {
                QMutexLocker locker(&placebo_state_mutex);
                {
                    const qint64 push_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                    pl_queue_push(placebo_queue, &promoted_src_frame);
                    const qint64 push_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                    queue_depth_cached.storeRelease(pl_queue_num_frames(placebo_queue));
                    logLatencyStats("pl_queue_push", push_end_us - push_begin_us);
                    CHIAKI_NOISY_DEBUG().nospace()
                        << "[latency] pl_queue_push_promoted_us=" << (push_end_us - push_begin_us)
                        << " queue_depth=" << pl_queue_num_frames(placebo_queue);
                    newest_queued_frame_pts = qMax(newest_queued_frame_pts, promoted_pts);
                    // Record that a promoted frame was queued so scheduling
                    // fires even when render pipeline is busy.  Do NOT set
                    // queued/queued_after_promotion here — the current frame
                    // still needs to go through the storage/free path below.
                    promoted_frame_queued = true;
                }
            }
            logPendingFrameLifecycle("promoted",
                                     promoted_pts,
                                     promoted_duration,
                                     promoted_age_us,
                                     idle_gap_us_snapshot,
                                     queue_depth_snapshot,
                                     submission_depth_limit,
                                     render_busy,
                                     true);
        }
        if (storePendingFrame(frame, true, synthetic_warmup_frame)) {
            snapshotPendingFrame();
            armStartupVisibility();
        }
        if (frame.frame)
            av_frame_free(&frame.frame);
        // Schedule even when render pipeline is busy: a promoted frame
        // was just queued so we must re-arm to consume it.
        if (should_rearm_stored_frame || promoted_frame_queued)
            scheduleUpdate(false, UpdateRequestReason::QueueStoredFrame);
        return;
    }
    if (!queued)
        {
            const char *gate_reason = pending_exists_snapshot ? "pending_exists" : (depth_exceeded ? "queue_full" : "not_queued");
            logPendingFrameGate("store_pending",
                                gate_reason,
                            frame.pts,
                            -1,
                            queue_depth_snapshot,
                            submission_depth_limit,
                            render_busy,
                            render_scheduled_now,
                                render_pending_now,
                                pending_exists_snapshot,
                                has_capacity_snapshot);
        const quint64 reset_seed_generation = reset_seed_capture_generation.loadAcquire();
        if (reset_seed_capture_active.loadAcquire() != 0 && reset_seed_generation != 0)
            storeResetSeedFrame(frame.frame, frame.pts, frame.duration, reset_seed_generation);
        if (storePendingFrame(frame, true, synthetic_warmup_frame)) {
            snapshotPendingFrame();
            armStartupVisibility();
        }
        if (frame.frame)
            av_frame_free(&frame.frame);
        if (should_rearm_stored_frame)
            scheduleUpdate(false, UpdateRequestReason::QueueStoredFrame);
        return;
    }

    bool dropped_stale_pending = false;
    {
        QMutexLocker locker(&pending_frame_mutex);
        while (pending_frame && pending_pts <= frame.pts + 1e-6) {
            const double dropped_pts = pending_pts;
            const qint64 dropped_age_us = pending_frame_stored_us > 0
                ? static_cast<qint64>(chiaki_time_now_monotonic_us()) - static_cast<qint64>(pending_frame_stored_us)
                : -1;
            logDroppedFrameReason("stale_pending_frame", static_cast<qint64>(chiaki_time_now_monotonic_us()), dropped_pts,
                                  "replaced by newer direct queue submission");
            bool render_scheduled_now = false;
            bool render_pending_now = false;
            {
                QMutexLocker schedule_locker(&render_schedule_mutex);
                render_scheduled_now = render_scheduled;
                render_pending_now = render_pending;
            }
            qCInfo(chiakiGui)
                << "Dropping stale pending frame after newer direct queue submission"
                << "pending_pts=" << dropped_pts
                << "queued_pts=" << frame.pts;
            increaseDroppedFrames();
            dropPendingFrameLocked();
            const bool pending_now = pending_frame != nullptr;
            logPendingFrameTransition("stale_clear",
                                      pending_now,
                                      dropped_pts,
                                      dropped_age_us,
                                      queue_depth_snapshot,
                                      submission_depth_limit,
                                      render_active.loadAcquire() != 0,
                                      render_scheduled_now,
                                      render_pending_now,
                                      false);
            dropped_stale_pending = true;
        }
    }
    if (dropped_stale_pending)
        refreshPendingFrameAge();

    if (!synthetic_warmup_frame) {
        {
            const quint64 reset_seed_generation = reset_seed_capture_generation.loadAcquire();
            if (reset_seed_capture_active.loadAcquire() != 0 && reset_seed_generation != 0)
                storeResetSeedFrame(frame.frame, frame.pts, frame.duration, reset_seed_generation);
        }

        snapshotLastFrame(frame.frame, frame.pts, frame.duration);

        if (!playback_started) {
            uint64_t now_us = chiaki_time_now_monotonic_us();
            uint64_t frame_pts_us = src_frame.pts > 0.0
                ? static_cast<uint64_t>(src_frame.pts * 1000000.0)
                : 0;
            ts_start = frame_pts_us < now_us ? now_us - frame_pts_us : 0;
            playback_started = true;
        }
    }

    armStartupVisibility();
    if (queued) {
        if (should_rearm_stored_frame || promoted_frame_queued)
            scheduleUpdate(false, UpdateRequestReason::QueueStoredFrame);
    }
}

void QmlMainWindow::setStreamMaxFPS(unsigned int max_fps)
{
    const double interval_ms = max_fps > 0 ? 1000.0 / max_fps : 16.6667;
    stream_configured_frame_interval_ms = interval_ms;
    source_frame_interval_ms = interval_ms;
}

void QmlMainWindow::resetPlaceboQueue()
{
    if (bypass_frame_queue) {
        placebo_reset_preserve_timeline.storeRelease(0);
        return;
    }
    uint64_t now_ms = chiaki_time_now_monotonic_ms();
    const bool preserve_timeline = placebo_reset_preserve_timeline.loadAcquire() != 0;
    if (!preserve_timeline && last_placebo_reset_ts && now_ms - last_placebo_reset_ts < 100)
    {
        const int wait_ms = static_cast<int>(qMax<uint64_t>(100 - (now_ms - last_placebo_reset_ts), 1));
        const quint64 generation = placebo_reset_throttle_generation.fetchAndAddRelaxed(1) + 1;
        qCInfo(chiakiGui)
            << "Queue reset throttled"
            << "wait_ms=" << wait_ms
            << "preserve_timeline=" << preserve_timeline;
        placebo_reset_pending.storeRelease(0);
        QTimer::singleShot(wait_ms, this, [this, generation, preserve_timeline]() {
            if (placebo_reset_throttle_generation.loadAcquire() != generation)
                return;
            queuePlaceboReset(preserve_timeline);
        });
        return;
    }
    last_placebo_reset_ts = now_ms;
    placebo_reset_throttle_generation.fetchAndAddRelaxed(1);
    placebo_reset_preserve_timeline.storeRelease(0);

    QMutexLocker locker(&placebo_state_mutex);
    qCInfo(chiakiGui)
        << "Queue reset executing"
        << "preserve_timeline=" << preserve_timeline
        << "queue_pts_origin=" << queue_pts_origin
        << "playback_started=" << playback_started;
    pl_queue_reset(placebo_queue);
    renderer_cache_flush_pending.storeRelaxed(1);
    newest_queued_frame_pts = -1.0;
    resetQueueDepthTracking();
    const bool keep_timeline = preserve_timeline && queue_pts_origin >= 0.0 && playback_started;
    preserve_playback_timeline = keep_timeline;
    if (!keep_timeline) {
        queue_pts_origin = -1.0;
        setQueuePtsOriginCached(queue_pts_origin);
        playback_started = false;
        ts_start = 0;
        preserve_playback_timeline = false;
    }
    locker.unlock();
    const quint64 generation = pending_reset_snapshot_generation.loadRelaxed();
    last_reset_snapshot_generation.storeRelaxed(generation);
    const bool had_pending = hasPendingFrame();
    const quint64 reset_generation = reset_seed_capture_generation.loadAcquire();
    reset_seed_capture_active.storeRelease(0);
    reset_seed_capture_generation.storeRelease(0);
    bool seeded = applyResetSeedFrame(reset_generation);
    if (!seeded && had_pending)
        applyPendingFrame();
    if (!seeded && !had_pending)
        applyKeptFrameSnapshot();
}

void QmlMainWindow::schedulePlaceboReset()
{
    queuePlaceboReset(false);
}

void QmlMainWindow::queuePlaceboReset(bool preserve_timeline)
{
    qCInfo(chiakiGui)
        << "Queue reset scheduled"
        << "preserve_timeline=" << preserve_timeline;
    if (preserve_timeline) {
        placebo_reset_preserve_timeline.storeRelease(1);
    } else if (placebo_reset_preserve_timeline.loadRelaxed() == 0) {
        placebo_reset_preserve_timeline.storeRelease(0);
    }
    quint64 generation = reset_seed_capture_generation.loadAcquire();
    if (!generation)
        generation = placebo_reset_throttle_generation.loadRelaxed() + 1;
    reset_seed_capture_generation.storeRelease(generation);
    reset_seed_capture_active.storeRelease(1);
    placebo_reset_pending.storeRelease(1);
    QMetaObject::invokeMethod(this, [this]() {
        scheduleUpdate(false, UpdateRequestReason::PlaceboReset);
    }, Qt::QueuedConnection);
}

double QmlMainWindow::queueDepthAverage() const
{
    return queue_depth_average.load(std::memory_order_acquire);
}

void QmlMainWindow::resetQueueDepthTracking()
{
    queue_depth_average.store(0.0, std::memory_order_release);
    queue_depth_cached.storeRelease(0);
    present_backpressure_active.storeRelease(0);
    emit queueDepthAverageChanged();
}

double QmlMainWindow::queuePtsOriginCached() const
{
    const qint64 origin_us = queue_pts_origin_cached_us.load(std::memory_order_acquire);
    if (origin_us < 0)
        return -1.0;
    return static_cast<double>(origin_us) / 1000000.0;
}

void QmlMainWindow::setQueuePtsOriginCached(double queue_pts_origin)
{
    const qint64 origin_us = queue_pts_origin < 0.0
        ? -1
        : static_cast<qint64>(queue_pts_origin * 1000000.0 + 0.5);
    queue_pts_origin_cached_us.store(origin_us, std::memory_order_release);
}

double QmlMainWindow::pendingFrameAge() const
{
    QMutexLocker locker(&pending_frame_age_mutex);
    return pending_frame_age;
}

QString QmlMainWindow::describePlaceboDiscardReason(qint64 now_us) const
{
    const qint64 swap_return_us = last_swap_return_us.loadAcquire();
    const qint64 swap_age_us = swap_return_us > 0 ? qMax<qint64>(0, now_us - swap_return_us) : -1;
    const qint64 frame_age_us = pending_frame_stored_us > 0 ? qMax<qint64>(0, now_us - static_cast<qint64>(pending_frame_stored_us)) : -1;
    const bool pending_exists = hasPendingFrame();
    const int render_active_now = render_active.loadAcquire();
    const int pending_during_cycle = render_pending_during_cycle.loadAcquire();
    const qint64 queue_depth_avg_scaled = static_cast<qint64>(queue_depth_average.load(std::memory_order_acquire) * 1000.0);
    const qint64 pending_age_scaled = static_cast<qint64>(pendingFrameAge() * 1000.0);

    QString cause;
    if (!present_vsync_enabled) {
        cause = QStringLiteral("present_vsync_disabled");
    } else if (pending_exists) {
        cause = QStringLiteral("pending_frame_waiting");
    } else if (pending_during_cycle > 0) {
        // Multiple render requests arrived while render was running — genuine render backlog
        cause = QStringLiteral("render_busy");
    } else if (render_active_now != 0) {
        // discard_frame fires from inside pl_queue_update() which is always within render();
        // render_active is therefore always 1 here. A transient submit spike let queue depth
        // briefly exceed the limit; libplacebo pruned the excess frame. Not a render backlog.
        cause = QStringLiteral("submit_spike_queue_trim");
    } else if (swap_age_us >= 0 && swap_age_us > 20000) {
        cause = QStringLiteral("late_acquire_or_missed_present_window");
    } else {
        cause = QStringLiteral("present_window_miss");
    }

    return QStringLiteral("cause=%1 swap_age_us=%2 frame_age_us=%3 queue_depth_avg_x1000=%4 pending_age_x1000=%5 render_active=%6 pending_during_cycle=%7 vsync=%8")
        .arg(cause)
        .arg(swap_age_us)
        .arg(frame_age_us)
        .arg(queue_depth_avg_scaled)
        .arg(pending_age_scaled)
        .arg(render_active_now)
        .arg(pending_during_cycle)
        .arg(present_vsync_enabled ? 1 : 0);
}

void QmlMainWindow::updateQueueDepthAverage(int depth)
{
    const double alpha = 0.15;
    double current = queue_depth_average.load(std::memory_order_acquire);
    double next = 0.0;
    do {
        next = current * (1.0 - alpha) + static_cast<double>(depth) * alpha;
    } while (!queue_depth_average.compare_exchange_weak(
        current,
        next,
        std::memory_order_release,
        std::memory_order_acquire));
    emit queueDepthAverageChanged();
}

void QmlMainWindow::updatePendingFrameAge(double age)
{
    double clamped = qMax(age, 0.0);
    {
        QMutexLocker locker(&pending_frame_age_mutex);
        pending_frame_age = clamped;
    }
    emit pendingFrameAgeChanged();
}

bool QmlMainWindow::storePendingFrame(ChiakiFfmpegFrame &frame, bool take_ownership, bool synthetic_warmup)
{
    if (!frame.frame)
        return false;

    int queue_depth = -1;
    const int depth_limit = effectiveQueueDepthLimit();
    const int submission_depth_limit = depth_limit;
    bool queued_existing = false;
    bool replaced_existing = false;

    AVFrame *stored_frame = take_ownership ? frame.frame : av_frame_clone(frame.frame);
    if (!stored_frame)
        return false;

    double queue_origin = queuePtsOriginCached();
    queue_depth = queue_depth_cached.loadAcquire();
    const bool have_state_lock = placebo_state_mutex.tryLock();
    if (have_state_lock) {
        queue_origin = queue_pts_origin;
        queue_depth = pl_queue_num_frames(placebo_queue);
    }

    {
        QMutexLocker locker(&pending_frame_mutex);
        queued_existing = pending_frame != nullptr;

        const uint64_t now_us = chiaki_time_now_monotonic_us();
        const bool overflow_enabled = pendingFrameOverflowEnabled();
        if (!overflow_enabled) {
            while (!pending_frame_overflow.empty()) {
                PendingFrameEntry dropped = std::move(pending_frame_overflow.front());
                pending_frame_overflow.pop_front();
                if (dropped.frame) {
                    logDroppedFrameReason("pending_overflow_disabled_drop", static_cast<qint64>(now_us), dropped.pts,
                                          "overflow disabled");
                    increaseDroppedFrames();
                    av_frame_free(&dropped.frame);
                }
            }
        }

        if (!pending_frame) {
            pending_frame = stored_frame;
            pending_frame_synthetic_warmup = synthetic_warmup;
            pending_pts = frame.pts;
            pending_duration = frame.duration;
            pending_frame_queue_origin = queue_origin;
            pending_frame_stored_us = now_us;
        } else {
            if (pending_frame_synthetic_warmup && !synthetic_warmup) {
                av_frame_free(&pending_frame);
                pending_frame = stored_frame;
                pending_frame_synthetic_warmup = false;
                pending_pts = frame.pts;
                pending_duration = frame.duration;
                pending_frame_queue_origin = queue_origin;
                pending_frame_stored_us = now_us;
                replaced_existing = true;
            } else {
                const size_t overflow_limit = static_cast<size_t>(pendingFrameOverflowLimit(submission_depth_limit));
                const double newest_pending_pts = pending_frame_overflow.empty()
                    ? pending_pts
                    : pending_frame_overflow.back().pts;
                if (frame.pts <= newest_pending_pts + 1e-6) {
                    logDroppedFrameReason("stale_pending_reject", static_cast<qint64>(chiaki_time_now_monotonic_us()), frame.pts,
                                          "before pending insert");
                    qCInfo(chiakiGui)
                        << "Dropping stale pending frame before overflow insert"
                        << "frame_pts=" << frame.pts
                        << "newest_pending_pts=" << newest_pending_pts;
                    increaseDroppedFrames();
                    av_frame_free(&stored_frame);
                    if (take_ownership)
                        frame.frame = nullptr;
                    return false;
                }
                if (overflow_limit == 0) {
                    logDroppedFrameReason("pending_overflow_disabled_replace", static_cast<qint64>(chiaki_time_now_monotonic_us()), pending_pts,
                                          "overflow disabled");
                    increaseDroppedFrames();
                    av_frame_free(&pending_frame);
                    pending_frame = stored_frame;
                    pending_frame_synthetic_warmup = synthetic_warmup;
                    pending_pts = frame.pts;
                    pending_duration = frame.duration;
                    pending_frame_queue_origin = queue_origin;
                    pending_frame_stored_us = now_us;
                    replaced_existing = true;
                } else {
                    if (pending_frame_overflow.size() >= overflow_limit) {
                        PendingFrameEntry dropped = std::move(pending_frame_overflow.front());
                        pending_frame_overflow.pop_front();
                        if (dropped.frame) {
                            logDroppedFrameReason("pending_overflow_evict", static_cast<qint64>(chiaki_time_now_monotonic_us()), dropped.pts,
                                                  "overflow queue full");
                            increaseDroppedFrames();
                            av_frame_free(&dropped.frame);
                        }
                    }
                    insertPendingOverflowLocked({
                        stored_frame,
                        frame.pts,
                        static_cast<float>(frame.duration),
                        queue_origin,
                        now_us,
                        synthetic_warmup,
                    });
                    replaced_existing = true;
                }
            }
        }

        pending_frame_present.storeRelease(1);
        queue_stored_frame_pending.storeRelease(1);
    }

        if (!synthetic_warmup && !has_video && startup_first_real_frame_queued_pending_visibility.loadAcquire() == 0)
            startup_first_real_frame_queued_pending_visibility.storeRelease(1);

    if (have_state_lock)
        placebo_state_mutex.unlock();

    if (take_ownership)
        frame.frame = nullptr;

    const qint64 stored_idle_us = last_render_idle_us.loadAcquire();
    const qint64 stored_now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    const qint64 stored_idle_gap_us = stored_idle_us > 0 ? qMax<qint64>(0, stored_now_us - stored_idle_us) : -1;
    bool render_scheduled_now = false;
    bool render_pending_now = false;
    {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled_now = render_scheduled;
        render_pending_now = render_pending;
    }
    logPendingFrameTransition(queued_existing ? "stored_append" : "stored_set",
                              true,
                              frame.pts,
                              0,
                              queue_depth,
                              submission_depth_limit,
                              render_active.loadAcquire() != 0,
                              render_scheduled_now,
                              render_pending_now,
                              replaced_existing);
    logPendingFrameLifecycle("stored",
                             frame.pts,
                             frame.duration,
                             0,
                             stored_idle_gap_us,
                             queue_depth,
                             submission_depth_limit,
                             render_active.loadAcquire() != 0,
                             queue_depth < submission_depth_limit);
    refreshPendingFrameAge();
    if (render_active.loadAcquire() == 0 && !render_scheduled_now && !render_pending_now)
        scheduleUpdate(false, UpdateRequestReason::PendingFrame);
    return true;
}

bool QmlMainWindow::storeResetSeedFrame(const AVFrame *frame, double pts, float duration, quint64 generation)
{
    if (!frame || generation == 0)
        return false;

    AVFrame *clone = av_frame_clone(frame);
    if (!clone)
        return false;

    {
        QMutexLocker locker(&reset_seed_mutex);
        if (reset_seed_frame && reset_seed_generation != generation) {
            av_frame_free(&reset_seed_frame);
            reset_seed_frame = nullptr;
            reset_seed_pts = 0.0;
            reset_seed_duration = 0.0f;
            reset_seed_generation = 0;
        }
        if (reset_seed_frame && pts <= reset_seed_pts + 1e-6) {
            av_frame_free(&clone);
            return false;
        }
        if (reset_seed_frame)
            av_frame_free(&reset_seed_frame);
        reset_seed_frame = clone;
        reset_seed_pts = pts;
        reset_seed_duration = duration;
        reset_seed_generation = generation;
    }

    return true;
}

bool QmlMainWindow::storeResetSeedFromPendingFrame(quint64 generation)
{
    if (generation == 0)
        return false;

    AVFrame *clone = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&pending_frame_mutex);
        if (!cloneNewestPendingFrameLocked(clone, pts, duration))
            return false;
    }

    {
        QMutexLocker locker(&reset_seed_mutex);
        if (reset_seed_frame && reset_seed_generation != generation) {
            av_frame_free(&reset_seed_frame);
            reset_seed_frame = nullptr;
            reset_seed_pts = 0.0;
            reset_seed_duration = 0.0f;
            reset_seed_generation = 0;
        }
        if (reset_seed_frame && pts <= reset_seed_pts + 1e-6) {
            av_frame_free(&clone);
            return false;
        }
        if (reset_seed_frame)
            av_frame_free(&reset_seed_frame);
        reset_seed_frame = clone;
        reset_seed_pts = pts;
        reset_seed_duration = duration;
        reset_seed_generation = generation;
    }

    return true;
}

void QmlMainWindow::snapshotPendingFrame()
{
    AVFrame *snapshot_frame = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&pending_frame_mutex);
        if (!cloneNewestPendingFrameLocked(snapshot_frame, pts, duration))
            return;
    }
    snapshotLastFrame(snapshot_frame, pts, duration, true);
}

void QmlMainWindow::refreshPendingFrameAge()
{
    uint64_t stored_us = 0;
    {
        QMutexLocker locker(&pending_frame_mutex);
        if (!pending_frame) {
            updatePendingFrameAge(0.0);
            return;
        }
        stored_us = pending_frame_stored_us;
    }

    double age = 0.0;
    if (stored_us) {
        const uint64_t now_us = chiaki_time_now_monotonic_us();
        if (now_us > stored_us)
            age = static_cast<double>(now_us - stored_us) / 1000000.0;
    }

    updatePendingFrameAge(age);
}

void QmlMainWindow::clearPendingFrameStateLocked()
{
    if (pending_frame)
        av_frame_free(&pending_frame);
    pending_pts = 0.0;
    pending_duration = 0.0f;
    pending_frame_queue_origin = 0.0;
    pending_frame_stored_us = 0;
    pending_frame_synthetic_warmup = false;
    while (!pending_frame_overflow.empty()) {
        PendingFrameEntry entry = std::move(pending_frame_overflow.front());
        pending_frame_overflow.pop_front();
        if (entry.frame)
            av_frame_free(&entry.frame);
    }
    pending_frame_present.storeRelease(0);
    queue_stored_frame_pending.storeRelease(0);
    pending_frame_submission_active.storeRelease(0);
    pending_frame_release_queued.storeRelease(0);
    pending_frame_release_queued_us.storeRelease(0);
}

void QmlMainWindow::prunePendingFramesBeforeLocked(double cutoff_pts)
{
    if (cutoff_pts < 0.0)
        return;

    constexpr double pts_epsilon = 1e-6;
    const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());

    auto drop_entry = [&](PendingFrameEntry &entry, const char *reason) {
        if (!entry.frame)
            return;
        logDroppedFrameReason(reason, now_us, entry.pts, "rollback cleanup");
        increaseDroppedFrames();
        av_frame_free(&entry.frame);
    };

    if (pending_frame && (
            pending_pts + pts_epsilon < cutoff_pts ||
            (pending_frame_synthetic_warmup && pending_pts <= cutoff_pts + pts_epsilon))) {
        PendingFrameEntry dropped;
        dropped.frame = pending_frame;
        dropped.pts = pending_pts;
        dropped.duration = pending_duration;
        dropped.queue_origin = pending_frame_queue_origin;
        dropped.stored_us = pending_frame_stored_us;
        pending_frame = nullptr;
        pending_frame_synthetic_warmup = false;
        pending_pts = 0.0;
        pending_duration = 0.0f;
        pending_frame_queue_origin = 0.0;
        pending_frame_stored_us = 0;
        drop_entry(dropped, "rollback_pending_prune");
    }

    while (!pending_frame_overflow.empty() && (
                pending_frame_overflow.front().pts + pts_epsilon < cutoff_pts ||
                (pending_frame_overflow.front().synthetic_warmup &&
                 pending_frame_overflow.front().pts <= cutoff_pts + pts_epsilon))) {
        PendingFrameEntry dropped = std::move(pending_frame_overflow.front());
        pending_frame_overflow.pop_front();
        drop_entry(dropped, "rollback_pending_prune");
    }

    if (!pending_frame && !pending_frame_overflow.empty())
        promotePendingFrameFromOverflowLocked();

    const bool pending_now = pending_frame != nullptr || !pending_frame_overflow.empty();
    pending_frame_present.storeRelease(pending_now ? 1 : 0);
    queue_stored_frame_pending.storeRelease(pending_now ? 1 : 0);
    if (!pending_now) {
        pending_frame_release_queued.storeRelease(0);
        pending_frame_release_queued_us.storeRelease(0);
    }
}

bool QmlMainWindow::cloneNewestPendingFrameLocked(AVFrame *&clone, double &pts, float &duration, uint64_t *stored_us)
{
    clone = nullptr;
    const AVFrame *newest_frame = pending_frame;
    double newest_pts = pending_pts;
    float newest_duration = pending_duration;
    uint64_t newest_stored_us = pending_frame_stored_us;

    if (!pending_frame_overflow.empty()) {
        const PendingFrameEntry &overflow_entry = pending_frame_overflow.back();
        const bool overflow_is_newer = !newest_frame
            || overflow_entry.pts > newest_pts + 1e-6
            || (qAbs(overflow_entry.pts - newest_pts) <= 1e-6 && overflow_entry.stored_us > newest_stored_us);
        if (overflow_is_newer) {
            newest_frame = overflow_entry.frame;
            newest_pts = overflow_entry.pts;
            newest_duration = overflow_entry.duration;
            newest_stored_us = overflow_entry.stored_us;
        }
    }

    if (!newest_frame)
        return false;

    clone = av_frame_clone(newest_frame);
    if (!clone)
        return false;

    pts = newest_pts;
    duration = newest_duration;
    if (stored_us)
        *stored_us = newest_stored_us;
    return true;
}

void QmlMainWindow::insertPendingOverflowLocked(PendingFrameEntry entry)
{
    auto pos = pending_frame_overflow.begin();
    while (pos != pending_frame_overflow.end() && pos->pts <= entry.pts + 1e-6)
        ++pos;
    pending_frame_overflow.insert(pos, std::move(entry));
}

bool QmlMainWindow::promotePendingFrameFromOverflowLocked()
{
    if (pending_frame)
        return true;
    if (pending_frame_overflow.empty()) {
        pending_frame_present.storeRelease(0);
        queue_stored_frame_pending.storeRelease(0);
        return false;
    }

    PendingFrameEntry entry = std::move(pending_frame_overflow.front());
    pending_frame_overflow.pop_front();
    pending_frame = entry.frame;
    pending_frame_synthetic_warmup = entry.synthetic_warmup;
    pending_pts = entry.pts;
    pending_duration = entry.duration;
    pending_frame_queue_origin = entry.queue_origin;
    pending_frame_stored_us = entry.stored_us;
    pending_frame_present.storeRelease(pending_frame ? 1 : 0);
    queue_stored_frame_pending.storeRelease(pending_frame ? 1 : 0);
    if (!pending_frame) {
        pending_frame_release_queued.storeRelease(0);
        pending_frame_release_queued_us.storeRelease(0);
    }
    return pending_frame != nullptr;
}

bool QmlMainWindow::takePendingFrameLocked(PendingFrameEntry &entry)
{
    if (!pending_frame && !promotePendingFrameFromOverflowLocked())
        return false;
    if (!pending_frame)
        return false;

    entry.frame = pending_frame;
    entry.pts = pending_pts;
    entry.duration = pending_duration;
    entry.queue_origin = pending_frame_queue_origin;
    entry.stored_us = pending_frame_stored_us;
    entry.synthetic_warmup = pending_frame_synthetic_warmup;

    pending_frame = nullptr;
    pending_pts = 0.0;
    pending_duration = 0.0f;
    pending_frame_queue_origin = 0.0;
    pending_frame_stored_us = 0;
    pending_frame_synthetic_warmup = false;
    promotePendingFrameFromOverflowLocked();
    if (!pending_frame) {
        pending_frame_release_queued.storeRelease(0);
        pending_frame_release_queued_us.storeRelease(0);
    }
    return true;
}

bool QmlMainWindow::dropPendingFrameLocked()
{
    if (pending_frame)
        av_frame_free(&pending_frame);
    pending_pts = 0.0;
    pending_duration = 0.0f;
    pending_frame_queue_origin = 0.0;
    pending_frame_stored_us = 0;
    pending_frame_synthetic_warmup = false;
    const bool promoted = promotePendingFrameFromOverflowLocked();
    if (!pending_frame) {
        pending_frame_release_queued.storeRelease(0);
        pending_frame_release_queued_us.storeRelease(0);
    }
    return promoted;
}

void QmlMainWindow::snapshotLastFrame(AVFrame *frame, double pts, float duration, bool take_ownership)
{
    // qCInfo(chiakiGui) << "snapshotLastFrame invoked pts=" << pts << " duration=" << duration;
    bool stored = false;
    const char *reason = "unknown";
    auto log_result = [&]() {
        // qCInfo(chiakiGui) << "snapshotLastFrame result stored=" << stored << " reason=" << reason;
    };
    auto release_owned_frame = [&]() {
        if (take_ownership && frame)
            av_frame_free(&frame);
    };

    if (!frame) {
        reason = "null frame";
        log_result();
        return;
    }
    const bool is_hw = frame->hw_frames_ctx != nullptr;
    if (!is_hw && !frame->data[0]) {
        reason = "missing data";
        release_owned_frame();
        log_result();
        return;
    }
    if (!is_hw && av_pix_fmt_count_planes(static_cast<AVPixelFormat>(frame->format)) <= 0) {
        reason = "no planes";
        release_owned_frame();
        log_result();
        return;
    }

    AVFrame *clone = take_ownership ? frame : av_frame_clone(frame);
    if (!clone) {
        reason = "clone failure";
        release_owned_frame();
        log_result();
        return;
    }

    if (!take_ownership) {
        clone->format = frame->format;
        clone->width = frame->width;
        clone->height = frame->height;
        clone->sample_aspect_ratio = frame->sample_aspect_ratio;
        clone->colorspace = frame->colorspace;
        clone->color_range = frame->color_range;
        clone->color_primaries = frame->color_primaries;
        clone->color_trc = frame->color_trc;
        av_frame_copy_props(clone, frame);
        if (!is_hw && clone->hw_frames_ctx) {
            av_buffer_unref(&clone->hw_frames_ctx);
            clone->hw_frames_ctx = nullptr;
        }
    }
    clone->opaque = this;
    AVFrame *fallback_clone = make_fallback_snapshot_frame(frame);
    {
        QMutexLocker locker(&kept_frame_mutex);
        if (kept_frame)
            av_frame_free(&kept_frame);
        if (fallback_frame)
            av_frame_free(&fallback_frame);
        kept_frame = clone;
        kept_frame_pts = pts;
        kept_frame_duration = duration;
        stored = true;
        reason = "stored";
        fallback_frame = fallback_clone;
        if (!fallback_clone && frame->hw_frames_ctx && frame->format != AV_PIX_FMT_VULKAN)
            qCWarning(chiakiGui) << "snapshotLastFrame: failed to create fallback frame";
    }
    snapshot_generation.fetchAndAddRelaxed(1);
    // qCInfo(chiakiGui) << "snapshotLastFrame: stored kept_frame pts=" << pts << " duration=" << duration;
    log_result();
}

void QmlMainWindow::clearSnapshotFrame()
{
    QMutexLocker locker(&kept_frame_mutex);
    if (kept_frame)
        av_frame_free(&kept_frame);
    if (fallback_frame)
        av_frame_free(&fallback_frame);
    kept_frame = nullptr;
    fallback_frame = nullptr;
}

bool QmlMainWindow::enqueueKeptFrame(double queue_pts_origin_hint, bool deinterlace_enabled, double &used_origin)
{
    AVFrame *clone = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&kept_frame_mutex);
        AVFrame *source = fallback_frame ? fallback_frame : kept_frame;
        if (!source)
            return false;
        clone = clone_frame_for_replay(source);
        if (!clone)
            return false;
        pts = kept_frame_pts;
        duration = kept_frame_duration;
    }

    clone->opaque = this;
    double origin = queue_pts_origin_hint;
    if (origin < 0.0)
        origin = pts;
    used_origin = origin;

    struct pl_source_frame snapshot_src = {};
    snapshot_src.pts = pts - origin;
    snapshot_src.duration = static_cast<float>(duration);
    snapshot_src.first_field = deinterlace_enabled
        ? pl_field_from_avframe(clone)
        : PL_FIELD_NONE;
    snapshot_src.frame_data = clone;
    snapshot_src.map = map_frame;
    snapshot_src.unmap = unmap_frame;
    snapshot_src.discard = discard_snapshot_frame;

    {
        const qint64 push_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        pl_queue_push(placebo_queue, &snapshot_src);
        const qint64 push_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        logLatencyStats("pl_queue_push", push_end_us - push_begin_us);
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] pl_queue_push_snapshot_us=" << (push_end_us - push_begin_us)
            << " queue_depth=" << pl_queue_num_frames(placebo_queue);
    }
    newest_queued_frame_pts = qMax(newest_queued_frame_pts, pts);
    return true;
}

bool QmlMainWindow::queueStoredFrame(AVFrame *frame, double pts, float duration,
                                     void (*discard_cb)(const struct pl_source_frame *),
                                     bool synthetic_warmup)
{
    if (!frame)
        return false;

    frame->opaque = this;

    if (duration > 0.0f)
        source_frame_interval_ms = 1000.0 * duration;
    bool deinterlace_enabled = false;
    struct pl_source_frame src_frame;
    bool defer_to_pending = false;
    int queue_depth_before_push = -1;
    int depth_limit_snapshot = effectiveQueueDepthLimit();
    const int submission_depth_limit = depth_limit_snapshot;
    const bool render_active_now = render_active.loadAcquire() != 0;
    bool render_scheduled_now = false;
    {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled_now = render_scheduled || render_pending;
    }
    {
        QMutexLocker locker(&placebo_state_mutex);
        if (!synthetic_warmup && newest_queued_frame_pts >= 0.0 && pts <= newest_queued_frame_pts + 1e-6) {
            logDroppedFrameReason("stale_stored_frame", static_cast<qint64>(chiaki_time_now_monotonic_us()), pts,
                                  "before queue submission");
            qCInfo(chiakiGui)
                << "Dropping stale stored frame before queue submission"
                << "frame_pts=" << pts
                << "newest_queued_pts=" << newest_queued_frame_pts;
            increaseDroppedFrames();
            av_frame_free(&frame);
            return false;
        }
        const bool preserve_timeline = preserve_playback_timeline;
        preserve_playback_timeline = false;
        double queue_origin = synthetic_warmup ? pts : queue_pts_origin;
        if (!synthetic_warmup && (queue_pts_origin < 0.0 || pts + 1e-6 < queue_pts_origin)) {
            logPtsRollback("applyPendingFrame/rollback", pts, queue_pts_origin, preserve_timeline, duration * 1000.0);
            pl_queue_reset(placebo_queue);
            resetQueueDepthTracking();
            const double old_origin = queue_pts_origin;
            queue_pts_origin = pts;
            setQueuePtsOriginCached(queue_pts_origin);
            queue_origin = queue_pts_origin;
            newest_queued_frame_pts = -1.0;
            {
                QMutexLocker pending_locker(&pending_frame_mutex);
                prunePendingFramesBeforeLocked(pts);
            }
            if (preserve_timeline && old_origin >= 0.0 && playback_started) {
                const double delta_pts = queue_pts_origin - old_origin;
                const int64_t delta_us = static_cast<int64_t>(delta_pts * 1000000.0);
                int64_t ts_start_signed = static_cast<int64_t>(ts_start) + delta_us;
                if (ts_start_signed < 0)
                    ts_start_signed = 0;
                ts_start = static_cast<uint64_t>(ts_start_signed);
            } else {
                ts_start = 0;
                playback_started = false;
            }
        }
        deinterlace_enabled = this->renderparams_opts->params.deinterlace_params;
        src_frame = {
            .pts = pts - queue_origin,
            .duration = static_cast<float>(duration),
            .first_field = deinterlace_enabled
                ? pl_field_from_avframe(frame)
                : PL_FIELD_NONE,
            .frame_data = frame,
            .map = map_frame,
            .unmap = unmap_frame,
            .discard = discard_cb,
        };
        queue_depth_before_push = pl_queue_num_frames(placebo_queue);
        if (queue_depth_before_push >= submission_depth_limit) {
            defer_to_pending = true;
        } else {
            const qint64 push_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            pl_queue_push(placebo_queue, &src_frame);
            const qint64 push_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            logLatencyStats("pl_queue_push", push_end_us - push_begin_us);
            CHIAKI_NOISY_DEBUG().nospace()
                << "[latency] pl_queue_push_stored_us=" << (push_end_us - push_begin_us)
                << " queue_depth=" << pl_queue_num_frames(placebo_queue);
            if (!synthetic_warmup)
                newest_queued_frame_pts = qMax(newest_queued_frame_pts, pts);
        }
    }

    if (defer_to_pending) {
        ChiakiFfmpegFrame deferred_frame = {
            .frame = frame,
            .pts = pts,
            .duration = duration,
            .recovered = false,
        };
        const bool stored = storePendingFrame(deferred_frame, true, synthetic_warmup);
        if (!stored) {
            av_frame_free(&frame);
            return false;
        }
        if (!synthetic_warmup && !has_video && startup_video_visible_pending.loadAcquire() == 0)
            startup_video_visible_pending.storeRelease(1);
        if (queue_stored_frame_pending.fetchAndStoreRelaxed(1) == 0)
            scheduleUpdate(false, UpdateRequestReason::PendingFrame);
        return true;
    }

    if (synthetic_warmup) {
        bool warmup_render_pending_now = false;
        {
            QMutexLocker locker(&render_schedule_mutex);
            warmup_render_pending_now = render_scheduled || render_pending;
        }
        if (render_active.loadAcquire() != 0 || render_scheduled_now || warmup_render_pending_now) {
            ChiakiFfmpegFrame deferred_warmup_frame = {
                .frame = frame,
                .pts = pts,
                .duration = duration,
                .recovered = false,
            };
            if (!storePendingFrame(deferred_warmup_frame, true, true)) {
                av_frame_free(&frame);
                return false;
            }
            scheduleUpdate(false, UpdateRequestReason::PendingFrame);
            return true;
        }
        if (queue_stored_frame_pending.fetchAndStoreRelaxed(1) == 0) {
            if (stream_session_active.loadAcquire() != 0 && configuredFrameMixerEnabledForScheduling())
                scheduleBufferedUpdate(UpdateRequestReason::QueueStoredFrame);
            else
                scheduleUpdate(false, UpdateRequestReason::QueueStoredFrame);
        }
        return true;
    }

    {
        uint64_t now_us = chiaki_time_now_monotonic_us();
        uint64_t frame_pts_us = src_frame.pts > 0.0
            ? static_cast<uint64_t>(src_frame.pts * 1000000.0)
            : 0;
        if (!playback_started) {
            ts_start = frame_pts_us < now_us ? now_us - frame_pts_us : 0;
            playback_started = true;
        } else if (ts_start) {
            const double current_pts = static_cast<double>(now_us - ts_start) / 1000000.0;
            const double catch_up_threshold = qMax(static_cast<double>(duration) * 2.0, 0.05);
            if (src_frame.pts > current_pts + catch_up_threshold)
                ts_start = frame_pts_us < now_us ? now_us - frame_pts_us : 0;
        }
    }

    if (!has_video && startup_first_real_frame_queued_pending_visibility.loadAcquire() == 0)
        startup_first_real_frame_queued_pending_visibility.storeRelease(1);
    if (queue_stored_frame_pending.fetchAndStoreRelaxed(1) == 0) {
        if (stream_session_active.loadAcquire() != 0 && configuredFrameMixerEnabledForScheduling())
            scheduleBufferedUpdate(UpdateRequestReason::QueueStoredFrame);
        else
            scheduleUpdate(false, UpdateRequestReason::QueueStoredFrame);
    }
    return true;
}

bool QmlMainWindow::applyResetSeedFrame(quint64 generation)
{
    AVFrame *frame = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&reset_seed_mutex);
        if (!reset_seed_frame)
            return false;
        if (reset_seed_generation != generation) {
            av_frame_free(&reset_seed_frame);
            reset_seed_pts = 0.0;
            reset_seed_duration = 0.0f;
            reset_seed_generation = 0;
            return false;
        }
        frame = reset_seed_frame;
        pts = reset_seed_pts;
        duration = reset_seed_duration;
        reset_seed_frame = nullptr;
        reset_seed_pts = 0.0;
        reset_seed_duration = 0.0f;
        reset_seed_generation = 0;
    }

    {
        QMutexLocker locker(&pending_frame_mutex);
        prunePendingFramesBeforeLocked(pts);
    }

    return queueStoredFrame(frame, pts, duration, discard_frame);
}

bool QmlMainWindow::applyKeptFrameSnapshot()
{
    AVFrame *clone = nullptr;
    double pts = 0.0;
    float duration = 0.0f;
    {
        QMutexLocker locker(&kept_frame_mutex);
        AVFrame *source = fallback_frame ? fallback_frame : kept_frame;
        if (!source)
            return false;
        clone = clone_frame_for_replay(source);
        if (!clone)
            return false;
        pts = kept_frame_pts;
        duration = kept_frame_duration;
    }

    return queueStoredFrame(clone, pts, duration, discard_snapshot_frame);
}

void QmlMainWindow::applyPendingFrame()
{
    PendingFrameEntry pending_entry;
    bool pending_still_present = false;
    {
        QMutexLocker locker(&pending_frame_mutex);
        pending_frame_submission_active.storeRelease(1);
        if (!takePendingFrameLocked(pending_entry))
        {
            pending_frame_submission_active.storeRelease(0);
            updatePendingFrameAge(0.0);
            return;
        }
        pending_still_present = pending_frame != nullptr;
    }

    bool render_scheduled_now = false;
    bool render_pending_now = false;
    {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled_now = render_scheduled;
        render_pending_now = render_pending;
    }
    logPendingFrameTransition(pending_still_present ? "consumed_keep" : "consumed_clear",
                              pending_still_present,
                              pending_entry.pts,
                              0,
                              -1,
                              -1,
                              render_active.loadAcquire() != 0,
                              render_scheduled_now,
                              render_pending_now,
                              false);

    if (pending_still_present)
        refreshPendingFrameAge();
    else
        updatePendingFrameAge(0.0);
    const qint64 consumed_idle_us = last_render_idle_us.loadAcquire();
    const qint64 consumed_now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    const qint64 consumed_idle_gap_us = consumed_idle_us > 0 ? qMax<qint64>(0, consumed_now_us - consumed_idle_us) : -1;
    logPendingFrameLifecycle("consumed",
                             pending_entry.pts,
                             pending_entry.duration,
                             0,
                             consumed_idle_gap_us,
                             -1,
                             -1,
                             render_active.loadAcquire() != 0,
                             true);
    int pending_queue_depth = -1;
    {
        QMutexLocker locker(&placebo_state_mutex);
        pending_queue_depth = pl_queue_num_frames(placebo_queue);
    }
    const qint64 queue_stored_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    queueStoredFrame(pending_entry.frame,
                     pending_entry.pts,
                     pending_entry.duration,
                     discard_frame,
                     pending_entry.synthetic_warmup);
    const qint64 queue_stored_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    pending_frame_submission_active.storeRelease(0);
    if (pending_still_present)
        queuePendingFrameRelease();
    logLatencyStats("pending_apply_queueStoredFrame", queue_stored_end_us - queue_stored_begin_us);
    CHIAKI_NOISY_DEBUG().nospace()
        << "[latency] pending_apply_queueStoredFrame_us=" << (queue_stored_end_us - queue_stored_begin_us)
        << " pending_pts=" << pending_entry.pts
        << " render_active=" << (render_active.loadAcquire() != 0)
        << " queue_depth=" << pending_queue_depth;
}

bool QmlMainWindow::applyPendingFrameIfQueueHasCapacity()
{
    if (!hasPendingFrame())
        return false;
    const int depth_limit = effectiveQueueDepthLimit();
    const int submission_depth_limit = depth_limit;
    const bool render_active_now = render_active.loadAcquire() != 0;
    bool render_scheduled_now = false;
    bool render_pending_now = false;
    {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled_now = render_scheduled;
        render_pending_now = render_pending;
    }

    int queue_depth = -1;
    bool has_capacity = false;
    {
        QMutexLocker locker(&placebo_state_mutex);
        queue_depth = pl_queue_num_frames(placebo_queue);
        has_capacity = queue_depth < submission_depth_limit;
    }
    logPendingQueueDepthHold("applyPendingFrameIfQueueHasCapacity",
                             static_cast<qint64>(chiaki_time_now_monotonic_us()),
                             queue_depth,
                             submission_depth_limit,
                             true,
                             render_active_now,
                             render_scheduled_now,
                             render_pending_now);
    if (has_capacity) {
        const qint64 apply_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        applyPendingFrame();
        const qint64 apply_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        logLatencyStats("pending_apply", apply_end_us - apply_begin_us);
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] pending_apply_us=" << (apply_end_us - apply_begin_us)
            << " queue_depth=" << queue_depth
            << " depth_limit=" << submission_depth_limit
            << " render_active=" << render_active_now
            << " render_scheduled=" << render_scheduled_now
            << " render_pending=" << render_pending_now;
        return true;
    }
    qint64 deferred_age_us = -1;
    double deferred_pts = 0.0;
    {
        QMutexLocker locker(&pending_frame_mutex);
        deferred_pts = pending_pts;
        deferred_age_us = pending_frame_stored_us > 0
            ? static_cast<qint64>(chiaki_time_now_monotonic_us()) - static_cast<qint64>(pending_frame_stored_us)
            : -1;
    }
    logPendingFrameGate("deferred",
                        "queue_full",
                        deferred_pts,
                        deferred_age_us,
                        queue_depth,
                        submission_depth_limit,
                        render_active_now,
                        render_scheduled_now,
                        render_pending_now,
                        true,
                        false);
    {
        QMutexLocker locker(&pending_frame_mutex);
        const qint64 deferred_idle_us = last_render_idle_us.loadAcquire();
        const qint64 deferred_now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        logPendingFrameLifecycle("deferred",
                                 pending_pts,
                                 pending_duration,
                                 pending_frame_stored_us > 0
                                     ? static_cast<qint64>(chiaki_time_now_monotonic_us()) - static_cast<qint64>(pending_frame_stored_us)
                                     : -1,
                                 deferred_idle_us > 0 ? qMax<qint64>(0, deferred_now_us - deferred_idle_us) : -1,
                                 queue_depth,
                                 submission_depth_limit,
                                 render_active.loadAcquire() != 0,
                                 false);
    }
    return false;
}

void QmlMainWindow::queuePendingFrameRelease()
{
    if (!hasPendingFrame())
        return;
    if (pending_frame_release_queued.fetchAndStoreRelaxed(1) != 0)
        return;
    pending_frame_release_queued_us.storeRelease(static_cast<qint64>(chiaki_time_now_monotonic_us()));
    scheduleUpdate(false, QmlMainWindow::UpdateRequestReason::PendingFrame);
}

bool QmlMainWindow::hasPendingFrame() const
{
    return pending_frame_present.loadAcquire() != 0;
}

bool QmlMainWindow::hasPendingFrameOverflow()
{
    QMutexLocker locker(&pending_frame_mutex);
    return !pending_frame_overflow.empty();
}

bool QmlMainWindow::pendingFrameOverflowEnabled() const
{
    static const bool enabled = []() {
        const QByteArray value = qgetenv("CHIAKI_ENABLE_PENDING_OVERFLOW");
        if (value.isEmpty())
            return true;

        const QByteArray normalized = value.trimmed().toLower();
        return normalized != "0" && normalized != "false" && normalized != "off";
    }();
    return enabled;
}

bool QmlMainWindow::hasBufferedWork()
{
    if (bypass_frame_queue) {
        QMutexLocker locker(&direct_frame_mutex);
        return direct_pending_frame != nullptr;
    }
    if (queue_depth_cached.loadAcquire() > 0)
        return true;
    if (placebo_state_mutex.tryLock()) {
        const int depth = placebo_queue ? pl_queue_num_frames(placebo_queue) : 0;
        placebo_state_mutex.unlock();
        queue_depth_cached.storeRelease(depth);
        if (depth > 0)
            return true;
    }
    if (hasPendingFrame())
        return true;
    if (pending_frame_submission_active.loadAcquire() != 0)
        return true;
    if (placebo_reset_pending.loadAcquire() != 0)
        return true;
    return false;
}

int QmlMainWindow::effectiveQueueDepthLimit() const
{
    int depth_limit = kDefaultQueueDepthLimit;

    // Test hook: allow forcing queue depth via env var for quick A/B experiments.
    const QByteArray force_depth = qgetenv("CHIAKI_TEST_QUEUE_DEPTH");
    if (!force_depth.isEmpty()) {
        bool ok = false;
        const int v = force_depth.toInt(&ok);
        if (ok && v > 0)
            depth_limit = v;
    }

    // Always use low-latency queue depth regardless of frame mixer mode.
    return qMax(depth_limit, 2);
}

int QmlMainWindow::pendingFrameOverflowLimit(int submission_depth_limit) const
{
    if (!pendingFrameOverflowEnabled())
        return 0;

    // Enabled by default. Set CHIAKI_ENABLE_PENDING_OVERFLOW=0 to disable the
    // small overflow queue behind the primary pending-frame slot. Keep this
    // intentionally tight so it absorbs short bursts without building a long
    // presentation backlog.
    Q_UNUSED(submission_depth_limit);
    return 2;
}

AVBufferRef *QmlMainWindow::vulkanHwDeviceCtx()
{
    if (render_backend != RenderBackend::Vulkan)
        return nullptr;

    if (vulkan_hw_dev_ctx || vk_decode_queue_index < 0)
        return vulkan_hw_dev_ctx;

    // Check if we're on a broken Mesa version in a Van Gogh GPU, if so, fail early.
    // 0x163F is AMD Custom GPU 0405 (AMD Aerith (Steam Deck LCD) or Aerith Plus (Xbox ROG Ally) )
    // 0x1435 is AMD Custom GPU 0932 (AMD Sephiroth (Steam Deck OLED))
    // Both are Van Gogh GPUs and have broken Vulkan HW decode on radv between 24.1.0-24.1.2
    // Sadly, SteamOS marks all of their Mesa builds as patch version .99, which makes it impossible to tell if we're on a
    // fixed 24.1 driver, so let's require a 24.2 driver or newer on Van Gogh devices just to be safe.
    bool is_van_gogh = amd_card && (vk_device_props.deviceID == 0x163F || vk_device_props.deviceID == 0x1435);
    if (is_van_gogh && vk_device_driver_props.driverID == VK_DRIVER_ID_MESA_RADV && vk_device_props.driverVersion < VK_MAKE_VERSION(24, 2, 0)){
        qCWarning(chiakiGui) << "Refusing to create Vulkan decode context due to us running on a Van Gogh GPU with a broken radv version ";
        return nullptr;
    }

    vulkan_hw_dev_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    AVHWDeviceContext *hwctx = reinterpret_cast<AVHWDeviceContext*>(vulkan_hw_dev_ctx->data);
    hwctx->user_opaque = const_cast<void*>(reinterpret_cast<const void*>(placebo_vulkan));
    AVVulkanDeviceContext *vkctx = reinterpret_cast<AVVulkanDeviceContext*>(hwctx->hwctx);
    vkctx->get_proc_addr = placebo_vulkan->get_proc_addr;
    vkctx->inst = placebo_vulkan->instance;
    vkctx->phys_dev = placebo_vulkan->phys_device;
    vkctx->act_dev = placebo_vulkan->device;
    vkctx->device_features = *placebo_vulkan->features;
    vkctx->enabled_inst_extensions = placebo_vk_inst->extensions;
    vkctx->nb_enabled_inst_extensions = placebo_vk_inst->num_extensions;
    vkctx->enabled_dev_extensions = placebo_vulkan->extensions;
    vkctx->nb_enabled_dev_extensions = placebo_vulkan->num_extensions;

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(59, 34, 100)
    vkctx->nb_qf = 0;
    if (placebo_vulkan->queue_graphics.index >= 0 && placebo_vulkan->queue_graphics.count > 0) {
        AVVulkanDeviceQueueFamily &qf = vkctx->qf[vkctx->nb_qf++];
        qf.idx = placebo_vulkan->queue_graphics.index;
        qf.num = placebo_vulkan->queue_graphics.count;
        qf.flags = VK_QUEUE_GRAPHICS_BIT;
        qf.video_caps = static_cast<VkVideoCodecOperationFlagBitsKHR>(0);
    }
    if (placebo_vulkan->queue_transfer.index >= 0 && placebo_vulkan->queue_transfer.count > 0) {
        AVVulkanDeviceQueueFamily &qf = vkctx->qf[vkctx->nb_qf++];
        qf.idx = placebo_vulkan->queue_transfer.index;
        qf.num = placebo_vulkan->queue_transfer.count;
        qf.flags = VK_QUEUE_TRANSFER_BIT;
        qf.video_caps = static_cast<VkVideoCodecOperationFlagBitsKHR>(0);
    }
    if (placebo_vulkan->queue_compute.index >= 0 && placebo_vulkan->queue_compute.count > 0) {
        AVVulkanDeviceQueueFamily &qf = vkctx->qf[vkctx->nb_qf++];
        qf.idx = placebo_vulkan->queue_compute.index;
        qf.num = placebo_vulkan->queue_compute.count;
        qf.flags = VK_QUEUE_COMPUTE_BIT;
        qf.video_caps = static_cast<VkVideoCodecOperationFlagBitsKHR>(0);
    }
    if (vk_decode_queue_index >= 0) {
        AVVulkanDeviceQueueFamily &qf = vkctx->qf[vkctx->nb_qf++];
        qf.idx = vk_decode_queue_index;
        qf.num = 1;
        qf.flags = VK_QUEUE_VIDEO_DECODE_BIT_KHR;
        qf.video_caps = static_cast<VkVideoCodecOperationFlagBitsKHR>(
            VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR | VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR);
    }
#else
    vkctx->queue_family_index = placebo_vulkan->queue_graphics.index;
    vkctx->nb_graphics_queues = placebo_vulkan->queue_graphics.count;
    vkctx->queue_family_tx_index = placebo_vulkan->queue_transfer.index;
    vkctx->nb_tx_queues = placebo_vulkan->queue_transfer.count;
    vkctx->queue_family_comp_index = placebo_vulkan->queue_compute.index;
    vkctx->nb_comp_queues = placebo_vulkan->queue_compute.count;
    vkctx->queue_family_decode_index = vk_decode_queue_index;
    vkctx->nb_decode_queues = 1;
#endif

    vkctx->lock_queue = [](struct AVHWDeviceContext *dev_ctx, uint32_t queue_family, uint32_t index) {
        auto vk = reinterpret_cast<pl_vulkan>(dev_ctx->user_opaque);
        vk->lock_queue(vk, queue_family, index);
    };
    vkctx->unlock_queue = [](struct AVHWDeviceContext *dev_ctx, uint32_t queue_family, uint32_t index) {
        auto vk = reinterpret_cast<pl_vulkan>(dev_ctx->user_opaque);
        vk->unlock_queue(vk, queue_family, index);
    };
    if (av_hwdevice_ctx_init(vulkan_hw_dev_ctx) < 0) {
        qCWarning(chiakiGui) << "Failed to create Vulkan decode context";
        av_buffer_unref(&vulkan_hw_dev_ctx);
    }

    return vulkan_hw_dev_ctx;
}

bool QmlMainWindow::makeOpenGLContextCurrent()
{
    if (!qt_gl_context)
        return false;

    QSurface *target_surface = nullptr;
    if (qt_gl_offscreen_surface && !isExposed())
        target_surface = qt_gl_offscreen_surface;
    else if (handle())
        target_surface = this;
    else if (qt_gl_offscreen_surface)
        target_surface = qt_gl_offscreen_surface;

    if (!target_surface)
        return false;

    if (QThread::currentThread() == qt_gl_context->thread())
        return qt_gl_context->makeCurrent(target_surface);

    bool ok = false;
    QObject *context_obj = qt_gl_context;
    QMetaObject::invokeMethod(context_obj, [this, target_surface, &ok]() {
        ok = qt_gl_context && qt_gl_context->makeCurrent(target_surface);
    }, Qt::BlockingQueuedConnection);
    return ok;
}

void QmlMainWindow::doneOpenGLContextCurrent()
{
    if (!qt_gl_context)
        return;

    if (QThread::currentThread() == qt_gl_context->thread()) {
        qt_gl_context->doneCurrent();
        return;
    }

    QObject *context_obj = qt_gl_context;
    QMetaObject::invokeMethod(context_obj, [this]() {
        if (qt_gl_context)
            qt_gl_context->doneCurrent();
    }, Qt::BlockingQueuedConnection);
}

void QmlMainWindow::init(Settings *settings, bool exit_app_on_stream_exit)
{
    bypass_frame_queue = settings->GetDirectFrameMapping();
    render_backend = settings->GetRenderBackend();
    setSurfaceType(render_backend == RenderBackend::Vulkan ? QWindow::VulkanSurface : QWindow::OpenGLSurface);
    qparams = {};
    qparams.drift_compensation = 1e-3;
    qparams.interpolation_threshold = 0.01;
    qparams.timeout = 0;

    auto initOpenGLBackend = [&]() -> bool {
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setAlphaBufferSize(8);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        format.setSwapInterval(settings->GetVSyncEnabled() ? 1 : 0);
        present_vsync_enabled = settings->GetVSyncEnabled();
#if defined(Q_OS_MACOS)
        // macOS only exposes modern OpenGL as a core profile.
        if (format.majorVersion() < 4) {
            format.setVersion(4, 1);
            format.setProfile(QSurfaceFormat::CoreProfile);
        }
#endif

        qt_gl_context = new QOpenGLContext();
        qt_gl_context->setFormat(format);
        if (QOpenGLContext *share = QOpenGLContext::globalShareContext())
            qt_gl_context->setShareContext(share);
        if (!qt_gl_context->create()) {
            qCWarning(chiakiGui) << "Failed to create QOpenGLContext";
            return false;
        }

        setFormat(qt_gl_context->format());
        qt_gl_offscreen_surface = new QOffscreenSurface();
        qt_gl_offscreen_surface->setFormat(qt_gl_context->format());
        qt_gl_offscreen_surface->create();
        if (!qt_gl_offscreen_surface->isValid()) {
            qCWarning(chiakiGui) << "Failed to create OpenGL offscreen surface";
            return false;
        }

        if (!this->handle())
            this->create();

        if (!makeOpenGLContextCurrent()) {
            qCWarning(chiakiGui) << "Failed to make QOpenGLContext current";
            return false;
        }

        detectOpenGLGpuVendor(this);
        amd_card = property("_chiaki_detected_amd").toBool();
        nvidia_card = property("_chiaki_detected_nvidia").toBool();

        struct pl_opengl_params gl_params = {
            .get_proc_addr_ex = [](void *proc_ctx, const char *procname) -> pl_voidfunc_t {
                auto ctx = static_cast<QOpenGLContext *>(proc_ctx);
                // Use volatile to prevent compiler optimizations from caching the address on macOS.
                volatile pl_voidfunc_t addr = reinterpret_cast<pl_voidfunc_t>(ctx->getProcAddress(procname));
                return addr;
            },
            .proc_ctx = qt_gl_context,
            .debug = false,
            .allow_software = true,
            .make_current = [](void *priv) -> bool {
                auto q = static_cast<QmlMainWindow *>(priv);
                return q->makeOpenGLContextCurrent();
            },
            .release_current = [](void *priv) {
                auto q = static_cast<QmlMainWindow *>(priv);
                q->doneOpenGLContextCurrent();
            },
            .priv = this,
        };
        placebo_opengl = pl_opengl_create(placebo_log, &gl_params);
        if (!placebo_opengl) {
            qCWarning(chiakiGui) << "Failed to create libplacebo OpenGL backend";
            return false;
        }
        doneOpenGLContextCurrent();
        return true;
    };

    auto fallbackToOpenGL = [&](const QString &reason) {
        qCWarning(chiakiGui) << "Vulkan renderer initialization failed, falling back to OpenGL:" << reason;
        pending_renderer_fallback_reason = reason;

        if (placebo_vulkan)
            pl_vulkan_destroy(&placebo_vulkan);
        if (placebo_vk_inst)
            pl_vk_inst_destroy(&placebo_vk_inst);

        render_backend = RenderBackend::OpenGL;
        setSurfaceType(QWindow::OpenGLSurface);
        if (!initOpenGLBackend())
            qFatal("Failed initializing OpenGL backend");
    };

    struct pl_log_params log_params = {
        .log_cb = placebo_log_cb,
        .log_level = detailedVerboseTelemetryEnabled() ? PL_LOG_DEBUG : PL_LOG_INFO,
    };
    placebo_log = pl_log_create(PL_API_VER, &log_params);

    if (render_backend == RenderBackend::Vulkan) {
        if (qEnvironmentVariableIsSet("CHIAKI_FORCE_VULKAN_FALLBACK")) {
            QString reason = qEnvironmentVariable("CHIAKI_FORCE_VULKAN_FALLBACK_REASON");
            if (reason.isEmpty())
                reason = QStringLiteral("Forced Vulkan fallback for testing");
            fallbackToOpenGL(reason);
            goto renderer_backend_ready;
        }

        const char *vk_exts[] = {
            nullptr,
            VK_KHR_SURFACE_EXTENSION_NAME,
        };

#if defined(Q_OS_LINUX)
        if (QGuiApplication::platformName().startsWith("wayland"))
            vk_exts[0] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
        else if (QGuiApplication::platformName().startsWith("xcb"))
            vk_exts[0] = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
        else
            Q_UNREACHABLE();
#elif defined(Q_OS_MACOS)
        vk_exts[0] = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
#elif defined(Q_OS_WIN)
        vk_exts[0] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#endif

        const char *opt_extensions[] = {
            VK_EXT_HDR_METADATA_EXTENSION_NAME,
        };

        struct pl_vk_inst_params vk_inst_params = {
            .extensions = vk_exts,
            .num_extensions = 2,
            .opt_extensions = opt_extensions,
            .num_opt_extensions = 1,
        };
        placebo_vk_inst = pl_vk_inst_create(placebo_log, &vk_inst_params);
        if (!placebo_vk_inst) {
            fallbackToOpenGL(QStringLiteral("Failed to create Vulkan instance"));
        } else {

#define GET_PROC(name_) { \
    vk_funcs.name_ = reinterpret_cast<decltype(vk_funcs.name_)>( \
        placebo_vk_inst->get_proc_addr(placebo_vk_inst->instance, #name_)); \
    if (!vk_funcs.name_) { \
        fallbackToOpenGL(QStringLiteral("Failed to resolve Vulkan symbol %1").arg(#name_)); \
        goto vulkan_setup_done; \
    } }
        GET_PROC(vkGetDeviceProcAddr)
#if defined(Q_OS_LINUX)
        if (QGuiApplication::platformName().startsWith("wayland"))
            GET_PROC(vkCreateWaylandSurfaceKHR)
        else if (QGuiApplication::platformName().startsWith("xcb"))
            GET_PROC(vkCreateXcbSurfaceKHR)
#elif defined(Q_OS_MACOS)
        GET_PROC(vkCreateMetalSurfaceEXT)
#elif defined(Q_OS_WIN)
        GET_PROC(vkCreateWin32SurfaceKHR)
#endif
        GET_PROC(vkDestroySurfaceKHR)
        GET_PROC(vkGetPhysicalDeviceQueueFamilyProperties)
        GET_PROC(vkGetPhysicalDeviceProperties2)
#undef GET_PROC

        const char *opt_dev_extensions[] = {
            VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
            VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
            VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME,
            VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME,
        };

        struct pl_vulkan_params vulkan_params = {
            .instance = placebo_vk_inst->instance,
            .get_proc_addr = placebo_vk_inst->get_proc_addr,
            .allow_software = true,
            PL_VULKAN_DEFAULTS
            .extra_queues = VK_QUEUE_VIDEO_DECODE_BIT_KHR,
            .opt_extensions = opt_dev_extensions,
            .num_opt_extensions = 4,
        };
        placebo_vulkan = pl_vulkan_create(placebo_log, &vulkan_params);
        if (!placebo_vulkan) {
            fallbackToOpenGL(QStringLiteral("Failed to create Vulkan device"));
        } else {

#define GET_PROC(name_) { \
    vk_funcs.name_ = reinterpret_cast<decltype(vk_funcs.name_)>( \
        vk_funcs.vkGetDeviceProcAddr(placebo_vulkan->device, #name_)); \
    if (!vk_funcs.name_) { \
        fallbackToOpenGL(QStringLiteral("Failed to resolve Vulkan symbol %1").arg(#name_)); \
        goto vulkan_setup_done; \
    } }
        GET_PROC(vkWaitSemaphores)
#undef GET_PROC

        uint32_t queueFamilyCount = 0;
        vk_funcs.vkGetPhysicalDeviceQueueFamilyProperties(placebo_vulkan->phys_device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
        vk_funcs.vkGetPhysicalDeviceQueueFamilyProperties(placebo_vulkan->phys_device, &queueFamilyCount, queueFamilyProperties.data());
        auto queue_it = std::find_if(queueFamilyProperties.begin(), queueFamilyProperties.end(), [](VkQueueFamilyProperties prop) {
            return prop.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR;
        });
        if (queue_it != queueFamilyProperties.end())
            vk_decode_queue_index = std::distance(queueFamilyProperties.begin(), queue_it);
        // device driver properties is core since vulkan 1.2 and libplacebo requires 1.2+ so if device is created, it's safe to assume this exists
        VkPhysicalDeviceDriverProperties driver_props = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        };
        VkPhysicalDeviceProperties2 device_props = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &driver_props,
        };
        vk_funcs.vkGetPhysicalDeviceProperties2(placebo_vulkan->phys_device, &device_props);
        vk_device_props = device_props.properties;
        vk_device_driver_props = driver_props;
        if(vk_device_props.vendorID == 0x1002)
        {
            amd_card = true;
            qCInfo(chiakiGui) << "Using amd graphics card";
        }
        else if(vk_device_props.vendorID == 0x10de)
        {
            nvidia_card = true;
            qCInfo(chiakiGui) << "Using nvidia graphics card";
        }
        }
        }
vulkan_setup_done:
        ;
    } else {
        if (!initOpenGLBackend())
            qFatal("Failed initializing OpenGL backend");
    }

    if (render_backend == RenderBackend::Vulkan) {
        qt_vk_inst = new QVulkanInstance;
        qt_vk_inst->setVkInstance(placebo_vk_inst->instance);
        if (!qt_vk_inst->create()) {
            delete qt_vk_inst;
            qt_vk_inst = nullptr;
            fallbackToOpenGL(QStringLiteral("Failed to create QVulkanInstance"));
        }
    }

renderer_backend_ready:
    struct pl_cache_params cache_params = {
        .log = placebo_log,
        .max_total_size = 10 << 20, // 10 MB
    };
    placebo_cache = pl_cache_create(&cache_params);
    pl_gpu_set_cache(placeboGpu(), placebo_cache);
    FILE *file = fopen(qPrintable(shader_cache_path()), "rb");
    if (file) {
        pl_cache_load_file(placebo_cache, file);
        fclose(file);
    }

    placebo_renderer = pl_renderer_create(
        placebo_log,
        placeboGpu()
    );

    placebo_queue = pl_queue_create(placeboGpu());

    if (render_backend == RenderBackend::Vulkan) {
        struct pl_vulkan_sem_params sem_params = {
            .type = VK_SEMAPHORE_TYPE_TIMELINE,
        };
        quick_sem = pl_vulkan_sem_create(placebo_vulkan->gpu, &sem_params);
        if (!quick_sem)
            qCCritical(chiakiGui) << "Failed to create quick Vulkan semaphore";
    }

    quick_render = new RenderControl(this);

    QQuickWindow::setDefaultAlphaBuffer(true);
    QQuickWindow::setGraphicsApi(render_backend == RenderBackend::Vulkan ? QSGRendererInterface::Vulkan : QSGRendererInterface::OpenGL);
    quick_window = new QQuickWindow(quick_render);
    if (render_backend == RenderBackend::Vulkan) {
        quick_window->setVulkanInstance(qt_vk_inst);
        quick_window->setGraphicsDevice(QQuickGraphicsDevice::fromDeviceObjects(placebo_vulkan->phys_device, placebo_vulkan->device, placebo_vulkan->queue_graphics.index));
    } else {
        quick_window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(qt_gl_context));
    }
    quick_window->setColor(QColor(0, 0, 0, 0));
    connect(quick_window, &QQuickWindow::focusObjectChanged, this, &QmlMainWindow::focusObjectChanged);

    buffered_pace_thread = new BufferedPlaybackPacerThread(this);
    buffered_pace_thread->start();

    present_pace_thread = new DeferredPresentPacerThread(this);
    present_pace_thread->start();

    vulkan_deferred_swap_enabled = settings->GetVulkanDeferredSwap();
    deferred_swap_thread = new DeferredSwapThread(this);
    deferred_swap_thread->start(QThread::HighPriority);

    qml_engine = new QQmlEngine(this);
    qml_engine->addImageProvider(QStringLiteral("svg"), new QmlSvgProvider);
    if (!qml_engine->incubationController())
        qml_engine->setIncubationController(quick_window->incubationController());
    connect(qml_engine, &QQmlEngine::quit, this, &QWindow::close);

    backend = new QmlBackend(settings, this);
    stats_overlay_widget = new StatsOverlayWidget(this, backend);
    connect(backend, &QmlBackend::sessionChanged, this, [this, exit_app_on_stream_exit](StreamSession *s) {
        const bool preserve_startup_warmup = s && startup_warmup_preserve_next_session_change;
        startup_warmup_preserve_next_session_change = false;
        session = s;
        stream_session_active.storeRelease(s ? 1 : 0);
        startup_video_visible_generation.fetchAndAddRelaxed(1);
        grab_input = 0;
        if (session)
            session->BlockInput(0);
        // Only drain at session end (teardown): prevents frame_mix use-after-free.
        // At session start there is no prior in-flight frame_mix, and the queue
        // reset below is mutex-protected, so blocking the main thread here is
        // unnecessary and causes a visible freeze in the connecting UI.
        if (!s)
            drainRenderThread();
        if (!preserve_startup_warmup) {
            {
                QMutexLocker locker(&direct_frame_mutex);
                av_frame_free(&direct_pending_frame);
                direct_frame_reset.storeRelease(1);
            }
            {
                QMutexLocker locker(&placebo_state_mutex);
                pl_queue_reset(placebo_queue);
                newest_queued_frame_pts = -1.0;
            }
            resetQueueDepthTracking();
            {
                QMutexLocker locker(&reset_seed_mutex);
                if (reset_seed_frame)
                    av_frame_free(&reset_seed_frame);
                reset_seed_pts = 0.0;
                reset_seed_duration = 0.0f;
                reset_seed_generation = 0;
            }
            clearSnapshotFrame();
            snapshot_generation.storeRelaxed(0);
            pending_reset_snapshot_generation.storeRelaxed(0);
            last_reset_snapshot_generation.storeRelaxed(0);
            next_buffered_update_us = 0;
            last_buffered_interval_us = 0;
            buffered_timer_fired.storeRelease(0);
            queue_pts_origin = -1.0;
            setQueuePtsOriginCached(queue_pts_origin);
            newest_queued_frame_pts = -1.0;
            reset_seed_capture_active.storeRelease(0);
            reset_seed_capture_generation.storeRelease(0);
            playback_started = false;
            ts_start = 0;
            quick_begin_wait_last_us = 0;
            quick_render_skip_next = 0;
            queue_stored_frame_pending.storeRelease(0);
            pending_frame_submission_active.storeRelease(0);
            loading_transition_complete.storeRelease(0);
            emit loadingTransitionCompleteChanged();
            startup_video_visible_refresh_pending.storeRelease(0);
            startup_first_real_frame_queued_pending_visibility.storeRelease(0);
            {
                QMutexLocker locker(&pending_frame_mutex);
                clearPendingFrameStateLocked();
            }
            updatePendingFrameAge(0.0);
            if (buffered_pace_thread)
                buffered_pace_thread->disarm();
            present_pace_timer_rearm.storeRelease(0);
            if (present_pace_thread)
                present_pace_thread->disarm();
            last_swap_return_us.storeRelease(0);
            deferred_present_in_flight.storeRelease(0);
            schedule_frame_mixer_active.storeRelaxed(configuredFrameMixerEnabledForScheduling() ? 1 : 0);
            if (has_video) {
                has_video = false;
                setCursor(Qt::ArrowCursor);
                emit hasVideoChanged();
            }
            startup_video_visible_pending.storeRelease(0);
        }
        if(session && exit_app_on_stream_exit)
        {
            connect(session, &StreamSession::SessionQuit, qGuiApp, &QGuiApplication::quit);
        }
        if(!session)
        {
            {
                QMutexLocker locker(&render_schedule_mutex);
                render_scheduled = false;
                render_pending = false;
                render_pending_during_cycle.storeRelaxed(0);
            }
            update_pending.storeRelaxed(0);
            armQuickNeedSync("session_disconnected");
            quick_need_render.storeRelaxed(1);
            setStreamMaxFPS(60);
            setStreamWindowAdjustable(false);
            if(qEnvironmentVariable("XDG_CURRENT_DESKTOP") != "gamescope")
            {
                if(!this->settings->GetGeometry().isEmpty())
                {
                    setGeometry(this->settings->GetGeometry());
                    normalTime();
                }
                else
                    showMaximized();
            }
            scheduleUpdate(true, UpdateRequestReason::SceneChanged);
        }
    });
    connect(backend, &QmlBackend::windowTypeUpdated, this, &QmlMainWindow::updateWindowType);

    if (render_backend == RenderBackend::OpenGL) {
        render_thread = QGuiApplication::instance()->thread();
        owns_render_thread = false;
    } else {
        render_thread = new QThread;
        render_thread->setObjectName("render");
        render_thread->start();
        owns_render_thread = true;

        quick_render->prepareThread(render_thread);
        quick_render->moveToThread(render_thread);
    }

    connect(quick_render, &QQuickRenderControl::sceneChanged, this, [this]() {
        armQuickNeedSync("sceneChanged");
        scheduleUpdate(render_backend != RenderBackend::OpenGL, UpdateRequestReason::SceneChanged);
    });
    connect(quick_render, &QQuickRenderControl::renderRequested, this, [this]() {
        quick_need_render.storeRelaxed(1);
        scheduleUpdate(render_backend != RenderBackend::OpenGL, UpdateRequestReason::RenderRequested);
    });


    if (render_backend == RenderBackend::OpenGL) {
        if (!makeOpenGLContextCurrent())
            qFatal("Failed to make QOpenGLContext current for render control initialization");
        quick_render->initialize();
        doneOpenGLContextCurrent();
    } else {
        QMetaObject::invokeMethod(quick_render, &QQuickRenderControl::initialize);
    }

    QTimer *dropped_frames_timer = new QTimer(this);
    dropped_frames_timer->setInterval(1000);
    dropped_frames_timer->start();
    connect(dropped_frames_timer, &QTimer::timeout, this, [this]() {
        int dropped_frames_next = dropped_frames_current.fetchAndStoreRelaxed(0);
        if (dropped_frames != dropped_frames_next) {
            dropped_frames = dropped_frames_next;
            emit droppedFramesChanged();
        }
    });

    this->renderparams_opts = pl_options_alloc(this->placebo_log);
    pl_options_reset(this->renderparams_opts, &pl_render_high_quality_params);
    this->renderparams_changed = true;
    this->fsr_hook = load_mpv_hook(placeboGpu(), QStringLiteral(":/shaders/FSR.glsl"));
    this->fsrcnnx_hook_8 = load_mpv_hook(placeboGpu(), QStringLiteral(":/shaders/FSRCNNX_x2_8-0-4-1.glsl"));
    this->fsrcnnx_hook_16 = load_mpv_hook(placeboGpu(), QStringLiteral(":/shaders/FSRCNNX_x2_16-0-4-1.glsl"));


    switch (settings->GetPlaceboPreset()) {
    case PlaceboPreset::Fast:
        setVideoPreset(VideoPreset::Fast);
        break;
    case PlaceboPreset::Default:
        setVideoPreset(VideoPreset::Default);
        break;
    case PlaceboPreset::HighQualitySpatial:
        setVideoPreset(VideoPreset::HighQualitySpatial);
        break;
    case PlaceboPreset::HighQualityAdvancedSpatial:
        setVideoPreset(VideoPreset::HighQualityAdvancedSpatial);
        break;
    case PlaceboPreset::Custom:
        setVideoPreset(VideoPreset::Custom);
        break;
    case PlaceboPreset::HighQuality:
    default:
        setVideoPreset(VideoPreset::HighQuality);
        break;
    }
    setZoomFactor(settings->GetZoomFactor());
}

void QmlMainWindow::drainRenderThread()
{
    drainDeferredSwaps();

    if (!quick_render)
        return;

    if (quick_render->thread() == QThread::currentThread())
        return;

    // Wait for queued render work to finish before tearing down libplacebo stuff which can cause crash on closing stream
    QMetaObject::invokeMethod(quick_render, []() {}, Qt::BlockingQueuedConnection);
}

void QmlMainWindow::normalTime()
{
    if(windowState() == Qt::WindowFullScreen)
    {
        if(was_maximized)
        {
            showMaximized();
            was_maximized = false;
        }
        else
            showNormal();
        setMinimumSize(QSize(0, 0));
    }
    QTimer::singleShot(1000, this, [this]{
        if(session)
            setStreamWindowAdjustable(true);
        else
        {
            setWindowAdjustable(true);
        }
    });
}

void QmlMainWindow::fullscreenTime()
{
    if(windowState() == Qt::WindowFullScreen)
        return;
    if(session)
        setStreamWindowAdjustable(false);
    else
        setWindowAdjustable(false);
    setMinimumSize(size());
    if(windowState() == Qt::WindowMaximized)
        was_maximized = true;
    else
        was_maximized = false;
    showFullScreen();
}
void QmlMainWindow::update()
{
    Q_ASSERT(QThread::currentThread() == QGuiApplication::instance()->thread());

    {
        QMutexLocker locker(&render_schedule_mutex);
        if (render_scheduled) {
            render_pending = true;
            render_pending_during_cycle.fetchAndAddRelaxed(1);
            return;
        }
        render_scheduled = true;
    }

    if (quick_need_sync.fetchAndStoreRelaxed(0) != 0) {
        quick_render->polishItems();
        if (quick_render->thread() == QThread::currentThread())
            sync();
        else
            QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::sync, this), Qt::QueuedConnection);
    }

    last_render_dispatch_us.storeRelease(static_cast<qint64>(chiaki_time_now_monotonic_us()));
    QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::render, this));
}

void QmlMainWindow::scheduleUpdate(bool force)
{
    scheduleUpdate(force, UpdateRequestReason::Unknown);
}

void QmlMainWindow::scheduleUpdate(bool force, UpdateRequestReason reason)
{
    if (QThread::currentThread() != QGuiApplication::instance()->thread()) {
        QMetaObject::invokeMethod(this, [this, force, reason]() { scheduleUpdate(force, reason); }, Qt::QueuedConnection);
        return;
    }

    const bool ui_priority = render_backend != RenderBackend::OpenGL &&
        (reason == UpdateRequestReason::SceneChanged ||
         reason == UpdateRequestReason::RenderRequested);
    if (ui_priority) {
        if (render_active.loadAcquire() != 0 || deferred_present_in_flight.loadAcquire() != 0)
            ui_priority_update_pending.storeRelease(1);
        force = true;
    }

    if (force && reason == UpdateRequestReason::PendingFrame) {
        last_update_request_reason.storeRelaxed(static_cast<int>(reason));
        const qint64 schedule_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        last_schedule_update_us.storeRelease(schedule_us);
        last_update_us = schedule_us;
        update();
        return;
    }

    if (!force) {
        if (!update_pending.testAndSetRelaxed(0, 1))
            return;
    } else {
        update_pending.storeRelaxed(1);
    }
    last_update_request_reason.storeRelaxed(static_cast<int>(reason));
    last_schedule_update_us.storeRelease(static_cast<qint64>(chiaki_time_now_monotonic_us()));
    QMetaObject::invokeMethod(this, [this]() {
        update_pending.storeRelaxed(0);
        last_update_us = chiaki_time_now_monotonic_us();
        const qint64 schedule_us = last_schedule_update_us.loadAcquire();
        if (schedule_us > 0 && last_update_us >= schedule_us)
            logScheduleToUpdateStats(last_update_us, last_update_us - schedule_us);
        update();
    }, Qt::QueuedConnection);
}

void QmlMainWindow::scheduleBufferedUpdate()
{
    scheduleBufferedUpdate(UpdateRequestReason::Unknown);
}

void QmlMainWindow::scheduleBufferedUpdate(UpdateRequestReason reason)
{
    if (QThread::currentThread() != QGuiApplication::instance()->thread()) {
        QMetaObject::invokeMethod(this, [this, reason]() { scheduleBufferedUpdate(reason); }, Qt::QueuedConnection);
        return;
    }
    if (!buffered_pace_thread)
        return;
    const bool stream_owned_playback = stream_session_active.loadAcquire() != 0;
    const bool timer_owned_playback = shouldUseRefreshPacedBuffering(
        stream_owned_playback,
        schedule_frame_mixer_active.loadAcquire());
    const bool render_busy_now = render_active.loadAcquire() != 0;
    auto hasBufferedWork = [this, stream_owned_playback]() {
        if (hasPendingFrame())
            return true;
        {
            QMutexLocker locker(&placebo_state_mutex);
            if (placebo_queue && pl_queue_num_frames(placebo_queue) > 0)
                return true;
        }
        if (stream_owned_playback &&
            (quick_need_sync.loadRelaxed() != 0 || quick_need_render.loadRelaxed() != 0))
            return true;
        if (placebo_reset_pending.loadAcquire() != 0)
            return true;
        return false;
    };
    double interval_ms = stream_configured_frame_interval_ms;
    if (timer_owned_playback) {
        double refresh_rate = screen() ? screen()->refreshRate() : 60.0;
        if (refresh_rate <= 1.0)
            refresh_rate = 60.0;
        interval_ms = 1000.0 / refresh_rate;
    }
    const qint64 interval_us = static_cast<qint64>(qMax(1.0, interval_ms) * 1000.0);
    const qint64 now_us = chiaki_time_now_monotonic_us();
    const qint64 last_swap_return_us = this->last_swap_return_us.loadAcquire();
    const qint64 start_frame_block_estimate_us = this->start_frame_block_estimate_us.loadAcquire();
    const bool buffered_interval_changed = last_buffered_interval_us != interval_us;
    if (buffered_interval_changed) {
        next_buffered_update_us = 0;
        last_buffered_interval_us = interval_us;
    }
    if (next_buffered_update_us == 0) {
        const qint64 anchor_us = last_swap_return_us > 0 ? last_swap_return_us : now_us;
        next_buffered_update_us = anchor_us + interval_us;
    }
    const bool pace_thread_active = timer_owned_playback && buffered_pace_thread->isArmed();
    if (timer_owned_playback && pace_thread_active && !buffered_interval_changed)
        return;
    if (timer_owned_playback && render_busy_now) {
        const qint64 next_wait_us = qMax<qint64>(1, next_buffered_update_us - now_us);
        logBufferedPacingStats(now_us, interval_us, next_wait_us, timer_owned_playback, pace_thread_active);
        buffered_pace_thread->arm(next_buffered_update_us);
        return;
    }
    if (now_us >= next_buffered_update_us) {
        const qint64 elapsed_us = now_us - next_buffered_update_us;
        const qint64 missed_intervals = elapsed_us >= 0 ? (elapsed_us / interval_us) + 1 : 1;
        next_buffered_update_us += missed_intervals * interval_us;
        logBufferedPacingStats(now_us, interval_us, -elapsed_us, timer_owned_playback, pace_thread_active);
        if (timer_owned_playback) {
            if (reason == UpdateRequestReason::Timer && !hasBufferedWork()) {
                buffered_pace_thread->arm(next_buffered_update_us);
                return;
            }
            buffered_pace_thread->arm(next_buffered_update_us);
            if (buffered_timer_fired.fetchAndStoreRelaxed(0) == 0)
                return;
        }
        scheduleUpdate(false, reason);
        return;
    }
    qint64 wait_us = next_buffered_update_us - now_us;
    logBufferedPacingStats(now_us, interval_us, wait_us, timer_owned_playback, pace_thread_active);
    buffered_pace_thread->arm(next_buffered_update_us);
}

void QmlMainWindow::scheduleRenderIfBacklog(UpdateRequestReason reason)
{
    if (render_backend == RenderBackend::Vulkan && vulkan_deferred_swap_enabled && deferred_present_in_flight.loadAcquire() != 0)
        return;

    if (hasBufferedWork())
        scheduleUpdate(false, reason);
}

void QmlMainWindow::handleBufferedPlaybackWake(qint64 timer_fire_us)
{
    logTimerFireInterval(timer_fire_us);
    buffered_timer_fired.storeRelease(1);
    scheduleBufferedUpdate(UpdateRequestReason::Timer);
}

bool QmlMainWindow::throttleFramePresentation(double interval_s)
{
    auto reset_present_pacing = [this]() {
        next_frame_target_us = 0;
        last_throttle_interval_s = 0.0;
    };

    if (present_pacing_reset_pending.fetchAndStoreRelaxed(0) != 0)
        reset_present_pacing();

    if (interval_s <= 0.0) {
        reset_present_pacing();
        present_pace_timer_rearm.storeRelease(0);
        if (present_pace_thread)
            present_pace_thread->disarm();
        return true;
    }
    const uint64_t interval_us = static_cast<uint64_t>(interval_s * 1000000.0 + 0.5);
    if (!interval_us)
        return true;
    if (qAbs(last_throttle_interval_s - interval_s) > 1e-6)
        next_frame_target_us = 0;
    last_throttle_interval_s = interval_s;

    const uint64_t now_us = chiaki_time_now_monotonic_us();
    if (!next_frame_target_us)
        next_frame_target_us = now_us + interval_us;
    while (next_frame_target_us <= now_us)
        next_frame_target_us += interval_us;
    const qint64 swap_to_start_gap_estimate_us = this->swap_to_start_gap_estimate_us.loadAcquire();
    const qint64 start_frame_block_estimate_us = this->start_frame_block_estimate_us.loadAcquire();
    const qint64 render_submit_estimate_us = this->render_submit_estimate_us.loadAcquire();
    const qint64 submit_swap_estimate_us = qMax<qint64>(0, static_cast<qint64>(g_submit_swap_est_us.load(std::memory_order_acquire)));
    const qint64 render_path_estimate_us = qMax<qint64>(0, swap_to_start_gap_estimate_us) +
        qMax<qint64>(0, start_frame_block_estimate_us) +
        qMax<qint64>(0, render_submit_estimate_us);
    const uint64_t base_lead_us = static_cast<uint64_t>(qMax<qint64>(0, render_path_estimate_us));
    const uint64_t extra_lead_us = static_cast<uint64_t>(qMax<qint64>(0, submit_swap_estimate_us));
    const uint64_t total_lead_us = base_lead_us + extra_lead_us;
    const uint64_t remaining_to_target_us = next_frame_target_us > now_us ? (next_frame_target_us - now_us) : 0;
    const uint64_t target_wakeup_us = remaining_to_target_us > total_lead_us
        ? (next_frame_target_us - total_lead_us)
        : now_us;
    logDeferredPresentDecision(static_cast<qint64>(now_us),
                               static_cast<qint64>(next_frame_target_us),
                               static_cast<qint64>(target_wakeup_us),
                               static_cast<qint64>(remaining_to_target_us),
                               start_frame_block_estimate_us,
                               static_cast<qint64>(total_lead_us),
                               static_cast<qint64>(extra_lead_us),
                               target_wakeup_us > now_us);

    uint64_t wait_us = target_wakeup_us > now_us ? (target_wakeup_us - now_us) : 0;
    while (wait_us > 0 && ui_priority_update_pending.loadAcquire() == 0) {
        const uint64_t sleep_us = qMin<uint64_t>(wait_us, 1000);
        QThread::usleep(static_cast<unsigned long>(sleep_us));
        const uint64_t after_sleep_us = chiaki_time_now_monotonic_us();
        wait_us = target_wakeup_us > after_sleep_us ? (target_wakeup_us - after_sleep_us) : 0;
    }
    if (ui_priority_update_pending.fetchAndStoreRelaxed(0) != 0 ||
        present_pacing_reset_pending.fetchAndStoreRelaxed(0) != 0) {
        reset_present_pacing();
    } else {
        next_frame_target_us += interval_us;
    }
    present_pace_timer_rearm.storeRelease(0);
    last_present_target_us.storeRelease(0);
    last_present_wakeup_target_us.storeRelease(0);
    if (present_pace_thread)
        present_pace_thread->disarm();
    return true;
}

void QmlMainWindow::handleDeferredPresentWake(qint64 timer_fire_us)
{
    logTimerFireInterval(timer_fire_us);
    if (present_pace_timer_rearm.fetchAndStoreRelaxed(0) == 0)
        return;

    const qint64 target_wakeup_us = last_present_wakeup_target_us.loadAcquire();
    const qint64 target_us = last_present_target_us.loadAcquire();
    bool render_scheduled_now = false;
    {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled_now = render_scheduled;
    }
    const bool render_active_now = render_active.loadAcquire() != 0;
    logDeferredTimerWakeup(timer_fire_us,
                           target_wakeup_us,
                           target_us,
                           0,
                           render_active_now,
                           render_scheduled_now);
    // Keep pacing event-driven: retry until the render thread is idle again.
    if (render_active_now || render_scheduled_now) {
        bool retry_armed = false;
        present_pace_timer_rearm.storeRelease(1);
        if (present_pace_thread) {
            const qint64 retry_wakeup_us = timer_fire_us + 1000;
            last_present_wakeup_target_us.storeRelease(retry_wakeup_us);
            present_pace_thread->arm(retry_wakeup_us);
            retry_armed = true;
        }
        logDeferredPresentRetry(timer_fire_us, render_active_now, render_scheduled_now, retry_armed);
        return;
    }
    scheduleUpdate(false, UpdateRequestReason::VSync);
}

bool QmlMainWindow::enqueueDeferredSwap(qint64 submit_begin_us,
                                        int queue_depth_at_submit,
                                        int depth_limit,
                                        bool pending_frame_waiting,
                                        bool pending_overflow_waiting,
                                        qint64 present_interval_us,
                                        qint64 present_submit_interval_us)
{
    if (!vulkan_deferred_swap_enabled || !deferred_swap_thread || render_backend != RenderBackend::Vulkan || !placebo_swapchain)
        return false;
    DeferredSwapTask task;
    task.submit_request_us = submit_begin_us;
    task.queue_depth_at_submit = queue_depth_at_submit;
    task.depth_limit = depth_limit;
    task.pending_frame_waiting = pending_frame_waiting;
    task.pending_overflow_waiting = pending_overflow_waiting;
    task.present_interval_us = present_interval_us;
    task.present_submit_interval_us = present_submit_interval_us;
    deferred_present_in_flight.storeRelease(1);
    const bool enqueued = deferred_swap_thread->enqueue(task);
    if (!enqueued)
        deferred_present_in_flight.storeRelease(0);
    return enqueued;
}

void QmlMainWindow::processDeferredSwapTask(qint64 submit_begin_us,
                                            int queue_depth_at_submit,
                                            int depth_limit,
                                            bool pending_frame_waiting,
                                            bool pending_overflow_waiting,
                                            qint64 present_interval_us,
                                            qint64 present_submit_interval_us)
{
    auto snapshot_render_schedule_state = [this]() {
        QMutexLocker locker(&render_schedule_mutex);
        return qMakePair(render_scheduled, render_pending);
    };

    if (!placebo_swapchain)
    {
        return;
    }

    const qint64 submit_begin_local_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (submit_begin_local_us >= submit_begin_us)
        logLatencyStats("submit_queue_delay", submit_begin_local_us - submit_begin_us);

    if (present_submit_interval_us > 0) {
        const bool backpressure_active = queue_depth_at_submit >= qMax(2, depth_limit - 1) || pending_frame_waiting;
        const double pace_multiplier = pending_overflow_waiting
            ? 0.80
            : (backpressure_active ? 0.90 : 1.0);
        const double paced_interval_s = static_cast<double>(present_submit_interval_us) / 1000000.0 * pace_multiplier;
        const qint64 pace_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        throttleFramePresentation(paced_interval_s);
        const qint64 pace_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (pace_end_us >= pace_begin_us)
            logLatencyStats("submit_pacing_wait", pace_end_us - pace_begin_us);
    }

    const qint64 submit_call_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    {
        QMutexLocker locker(&placebo_swapchain_mutex);
        if (!pl_swapchain_submit_frame(placebo_swapchain))
            qCWarning(chiakiGui) << "Failed to submit Placebo frame on deferred thread!";
        else {
            const qint64 submit_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            logLatencyStats("pl_swapchain_submit_frame", submit_us - submit_call_begin_us);
            CHIAKI_NOISY_DEBUG().nospace()
                << "[latency] pl_swapchain_submit_frame_us=" << (submit_us - submit_call_begin_us)
                << " queue_depth=" << pl_queue_num_frames(placebo_queue)
                << " deferred=1";
            if (submit_us >= submit_call_begin_us) {
                const qint64 submit_call_us = submit_us - submit_call_begin_us;
                logLatencyStats("submit_call", submit_call_us);
                logLatencyStats("submit_frame_block", submit_call_us);
                const auto submit_render_state = snapshot_render_schedule_state();
                logPresentOutlier("submit_call",
                                  submit_us,
                                  submit_call_us,
                                  false,
                                  present_vsync_enabled,
                                  present_interval_us,
                                  queue_depth_at_submit,
                                  depth_limit,
                                  pending_frame_waiting,
                                  render_active.loadAcquire() != 0,
                                  submit_render_state.first,
                                  submit_render_state.second);
                logRenderPhaseStall(submit_us,
                                    "submit",
                                    submit_call_us,
                                    queue_depth_at_submit,
                                    depth_limit,
                                    pending_frame_waiting,
                                    render_active.loadAcquire() != 0,
                                    submit_render_state.first,
                                    submit_render_state.second);
                logPresentSubmitStats(submit_us, true, stream_session_active.loadAcquire() != 0 && effectiveFrameMixerEnabled(), present_interval_us);
            }

            const qint64 present_swap_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (present_swap_begin_us >= submit_us)
                logLatencyStats("submit_to_swap", present_swap_begin_us - submit_us);
            pl_swapchain_swap_buffers(placebo_swapchain);

            const qint64 swap_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            logSwapInterval(swap_us);
            this->last_swap_return_us.storeRelease(swap_us);
            this->last_present_complete_us.storeRelease(swap_us);
            if (startup_video_visible_pending.loadAcquire() != 0) {
                const quint64 startup_visibility_generation = startup_video_visible_generation.loadAcquire();
                QMetaObject::invokeMethod(this, [this, startup_visibility_generation]() {
                    completeStartupVideoVisibility(startup_visibility_generation);
                }, Qt::QueuedConnection);
            }
            logPresentCompletionStats(swap_us,
                                      submit_begin_us,
                                      swap_us,
                                      queue_depth_at_submit,
                                      RenderBackend::Vulkan,
                                      true);
            if (swap_us >= present_swap_begin_us)
                logLatencyStats("swap_call", swap_us - present_swap_begin_us);
            const qint64 observed = swap_us - submit_begin_local_us;
            if (observed > 0) {
                const long long prev = g_submit_swap_est_us.load(std::memory_order_acquire);
                const long long next = static_cast<long long>(((prev * 7ll) + observed) / 8ll);
                g_submit_swap_est_us.store(next, std::memory_order_release);
            }
        }
    }
}

void QmlMainWindow::finalizeDeferredPresentIfIdle()
{
    if (deferred_swap_thread && !deferred_swap_thread->isIdle())
        return;
    if (deferred_present_in_flight.loadAcquire() == 0) {
        if (hasBufferedWork())
            scheduleUpdate(false, UpdateRequestReason::PendingFrame);
        else
            scheduleUpdate(false, UpdateRequestReason::FinalizePending);
    }
}

void QmlMainWindow::completeStartupVideoVisibility(quint64 generation)
{
    if (startup_video_visible_generation.loadAcquire() != generation)
        return;
    if (startup_video_visible_pending.fetchAndStoreRelaxed(0) == 0)
        return;
    if (startup_first_real_frame_queued_pending_visibility.loadAcquire() == 0)
        return;
    if (has_video)
        return;

    has_video = true;
    startup_first_real_frame_queued_pending_visibility.storeRelease(0);
    if (!grab_input && settings->GetHideCursor())
        setCursor(Qt::BlankCursor);
    emit hasVideoChanged();
    startup_video_visible_refresh_pending.storeRelease(1);
    armQuickNeedSync("completeStartupVideoVisibility");
    quick_need_render.storeRelaxed(1);
    scheduleUpdate(true, UpdateRequestReason::PendingFrame);
}

void QmlMainWindow::drainDeferredSwaps()
{
    if (deferred_swap_thread)
        deferred_swap_thread->drain();
    finalizeDeferredPresentIfIdle();
}

void QmlMainWindow::updatePlacebo()
{
    renderparams_changed = true;
    schedule_frame_mixer_active.storeRelaxed(configuredFrameMixerEnabledForScheduling() ? 1 : 0);
}

void QmlMainWindow::updateVSync()
{
    if (!pendingFrameOverflowEnabled()) {
        QMutexLocker locker(&pending_frame_mutex);
        while (!pending_frame_overflow.empty()) {
            PendingFrameEntry entry = std::move(pending_frame_overflow.front());
            pending_frame_overflow.pop_front();
            if (entry.frame)
                av_frame_free(&entry.frame);
        }
        if (!pending_frame) {
            pending_frame_present.storeRelease(0);
            queue_stored_frame_pending.storeRelease(0);
        }
    }

    if (render_backend != RenderBackend::Vulkan)
        return;
    armQuickNeedSync("updateVSync");
    quick_need_render.storeRelaxed(1);
    swapchain_recreate_pending.storeRelaxed(1);
    scheduleUpdate(true, UpdateRequestReason::VSync);
}

void QmlMainWindow::updateVulkanDeferredSwap()
{
    if (!settings)
        return;

    const bool enabled = settings->GetVulkanDeferredSwap();
    if (!enabled && render_backend == RenderBackend::Vulkan)
        drainDeferredSwaps();
    vulkan_deferred_swap_enabled = enabled;
    if (render_backend == RenderBackend::Vulkan)
        scheduleUpdate(true, UpdateRequestReason::VSync);
}

void QmlMainWindow::createSwapchain()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (placebo_swapchain)
        return;

    if (render_backend == RenderBackend::OpenGL) {
        if (!makeOpenGLContextCurrent()) {
            qCCritical(chiakiGui) << "Failed to make OpenGL context current";
            return;
        }
        struct pl_opengl_swapchain_params sw_params = {
            .swap_buffers = [](void *priv) {
                auto q = static_cast<QmlMainWindow *>(priv);
                if (q->qt_gl_context)
                    q->qt_gl_context->swapBuffers(q);
            },
            .priv = this,
        };
        placebo_swapchain = pl_opengl_create_swapchain(placebo_opengl, &sw_params);
        doneOpenGLContextCurrent();
        if (!placebo_swapchain)
            qCCritical(chiakiGui) << "Failed creating OpenGL swapchain";
        return;
    }

    VkResult err = VK_ERROR_UNKNOWN;
#if defined(Q_OS_LINUX)
    if (QGuiApplication::platformName().startsWith("wayland")) {
        VkWaylandSurfaceCreateInfoKHR surfaceInfo = {};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.display = reinterpret_cast<struct wl_display*>(QGuiApplication::platformNativeInterface()->nativeResourceForWindow("display", this));
        surfaceInfo.surface = reinterpret_cast<struct wl_surface*>(QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", this));
        err = vk_funcs.vkCreateWaylandSurfaceKHR(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
    } else if (QGuiApplication::platformName().startsWith("xcb")) {
        VkXcbSurfaceCreateInfoKHR surfaceInfo = {};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.connection = reinterpret_cast<xcb_connection_t*>(QGuiApplication::platformNativeInterface()->nativeResourceForWindow("connection", this));
        surfaceInfo.window = static_cast<xcb_window_t>(winId());
        err = vk_funcs.vkCreateXcbSurfaceKHR(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
    } else {
        Q_UNREACHABLE();
    }
#elif defined(Q_OS_MACOS)
    VkMetalSurfaceCreateInfoEXT surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    surfaceInfo.pLayer = static_cast<const CAMetalLayer*>(reinterpret_cast<void*(*)(id, SEL)>(objc_msgSend)(reinterpret_cast<id>(winId()), sel_registerName("layer")));
    err = vk_funcs.vkCreateMetalSurfaceEXT(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
#elif defined(Q_OS_WIN)
    VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = GetModuleHandle(nullptr);
    surfaceInfo.hwnd = reinterpret_cast<HWND>(winId());
    err = vk_funcs.vkCreateWin32SurfaceKHR(placebo_vk_inst->instance, &surfaceInfo, nullptr, &surface);
#endif

    if (err != VK_SUCCESS) {
        if (err == VK_ERROR_DEVICE_LOST) {
            qCWarning(chiakiGui)
                << "Failed to create VkSurfaceKHR"
                << "result=" << err;
            handleVulkanDeviceLost(QStringLiteral("vkCreateSurfaceKHR returned VK_ERROR_DEVICE_LOST"));
        } else {
            const bool surface_renderable = isExposed() && width() > 0 && height() > 0;
            const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            int failures = 0;
            qint64 failure_duration_us = 0;
            if (surface_renderable) {
                failures = surface_create_failures.fetchAndAddRelaxed(1) + 1;
                if (surface_create_failure_first_us <= 0)
                    surface_create_failure_first_us = now_us;
                failure_duration_us = now_us - surface_create_failure_first_us;
            } else {
                surface_create_failures.storeRelease(0);
                surface_create_failure_first_us = 0;
            }
            qCWarning(chiakiGui)
                << "Failed to create VkSurfaceKHR"
                << "result=" << err
                << "consecutive_failures=" << failures
                << "failure_duration_us=" << failure_duration_us
                << "surface_renderable=" << surface_renderable;
            if (surface_renderable && failures >= 60 && failure_duration_us >= 3000000) {
                handleVulkanRendererFallback(
                    tr("Vulkan renderer stalled"),
                    tr("The Vulkan surface could not recover. Chiaki-ng will relaunch with OpenGL for this run only."),
                    tr("Vulkan surface creation failed repeatedly while the window was visible. OpenGL is being used for this launch only."));
            } else {
                swapchain_recreate_pending.storeRelease(1);
                QMetaObject::invokeMethod(this, [this]() {
                    scheduleUpdate(true, UpdateRequestReason::VSync);
                }, Qt::QueuedConnection);
            }
        }
        return;
    }
    surface_create_failures.storeRelease(0);
    surface_create_failure_first_us = 0;

    const int swapchain_depth = 1;
    struct pl_vulkan_swapchain_params swapchain_params = {
        .surface = surface,
        // Prefer MAILBOX for vsync (lower) latency when supported).
        .present_mode = settings->GetVSyncEnabled() ? VK_PRESENT_MODE_FIFO_KHR
                                                    : VK_PRESENT_MODE_IMMEDIATE_KHR,
        .swapchain_depth = swapchain_depth,
    };
    placebo_swapchain = pl_vulkan_create_swapchain(placebo_vulkan, &swapchain_params);
    present_vsync_enabled = swapchain_params.present_mode != VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (!placebo_swapchain && !settings->GetVSyncEnabled()) {
        qCWarning(chiakiGui) << "Immediate present mode unsupported, falling back to FIFO VSync";
        swapchain_params.present_mode = VK_PRESENT_MODE_FIFO_KHR;
        swapchain_params.swapchain_depth = 1;
        placebo_swapchain = pl_vulkan_create_swapchain(placebo_vulkan, &swapchain_params);
        present_vsync_enabled = true;
    }
    qCInfo(chiakiGui).nospace()
        << "[present] swapchain_create vsync_setting=" << (settings->GetVSyncEnabled() ? 1 : 0)
        << " requested_mode=" << (settings->GetVSyncEnabled() ? "FIFO" : "IMMEDIATE")
        << " selected_mode="
        << (swapchain_params.present_mode == VK_PRESENT_MODE_FIFO_KHR ? "FIFO"
            : swapchain_params.present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE"
            : swapchain_params.present_mode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX"
            : "OTHER")
        << " swapchain_depth=" << swapchain_depth
        << " present_vsync_enabled=" << (present_vsync_enabled ? 1 : 0)
        << " created=" << (placebo_swapchain ? 1 : 0);
    if (!placebo_swapchain) {
        vk_funcs.vkDestroySurfaceKHR(placebo_vk_inst->instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
}

void QmlMainWindow::destroySwapchain()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (!placebo_swapchain)
        return;

    if (deferred_swap_thread && deferred_swap_thread->hasPendingWork())
        drainDeferredSwaps();
    {
        QMutexLocker locker(&placebo_swapchain_mutex);
        pl_swapchain_destroy(&placebo_swapchain);
    }
    if (render_backend == RenderBackend::Vulkan)
        vk_funcs.vkDestroySurfaceKHR(placebo_vk_inst->instance, surface, nullptr);
    if (render_backend == RenderBackend::OpenGL && quick_fbo) {
        if (!makeOpenGLContextCurrent()) {
            qCWarning(chiakiGui) << "Failed to make OpenGL context current while destroying swapchain";
            swapchain_size = QSize();
            return;
        }
        delete quick_fbo;
        quick_fbo = nullptr;
        doneOpenGLContextCurrent();
    } else {
        delete quick_fbo;
        quick_fbo = nullptr;
    }
    swapchain_size = QSize();
}

void QmlMainWindow::resizeSwapchain()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (!placebo_swapchain)
        createSwapchain();
    if (!placebo_swapchain)
        return;

    const QSize window_size(width() * devicePixelRatio(), height() * devicePixelRatio());
    const bool swapchain_ready = quick_tex &&
                                 (render_backend != RenderBackend::OpenGL || quick_fbo) &&
                                 (render_backend != RenderBackend::Vulkan || quick_vk_image);
    if (window_size == swapchain_size && swapchain_ready)
        return;

    // Present work may still be touching the live swapchain, so wait before
    // resizing or recreating it.
    if (deferred_swap_thread && deferred_swap_thread->hasPendingWork())
        drainDeferredSwaps();

    QSize new_swapchain_size = window_size;
    {
        QMutexLocker locker(&placebo_swapchain_mutex);
        pl_swapchain_resize(placebo_swapchain, &new_swapchain_size.rwidth(), &new_swapchain_size.rheight());
    }

    if (quick_frame)
        endFrame();

    if (render_backend == RenderBackend::OpenGL) {
        if (!makeOpenGLContextCurrent()) {
            qCCritical(chiakiGui) << "Failed to make OpenGL context current";
            return;
        }

        QOpenGLFramebufferObject *new_quick_fbo =
            new QOpenGLFramebufferObject(new_swapchain_size, QOpenGLFramebufferObject::CombinedDepthStencil);
        if (!new_quick_fbo || !new_quick_fbo->isValid()) {
            qCCritical(chiakiGui) << "Failed to create Qt Quick OpenGL framebuffer object";
            delete new_quick_fbo;
            doneOpenGLContextCurrent();
            return;
        }
        // A new FBO holds whatever was in that GPU memory; the overlay can be composited
        // before Qt Quick renders into it again, which shows up as garbage on the loading screen.
        clearQuickOpenGLTarget(new_quick_fbo);
        QOpenGLFramebufferObject::bindDefault();

        int quick_texture_internal_format = static_cast<int>(new_quick_fbo->format().internalTextureFormat());
        if (!quick_texture_internal_format)
            quick_texture_internal_format = 0x8058; // GL_RGBA8
        struct pl_opengl_wrap_params wrap_params = {
            .texture = new_quick_fbo->texture(),
            .framebuffer = static_cast<unsigned int>(new_quick_fbo->handle()),
            .width = new_swapchain_size.width(),
            .height = new_swapchain_size.height(),
            .target = 0x0DE1,
            .iformat = quick_texture_internal_format,
        };
        pl_tex new_quick_tex = pl_opengl_wrap(placeboGpu(), &wrap_params);
        if (!new_quick_tex) {
            qCCritical(chiakiGui) << "Failed to wrap Qt Quick OpenGL framebuffer texture";
            delete new_quick_fbo;
            doneOpenGLContextCurrent();
            return;
        }

        quick_window->setRenderTarget(QQuickRenderTarget());
        if (quick_tex)
            pl_tex_destroy(placeboGpu(), &quick_tex);
        delete quick_fbo;
        quick_fbo = new_quick_fbo;
        quick_tex = new_quick_tex;
        swapchain_size = new_swapchain_size;
        quick_window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(quick_fbo->texture(), quick_fbo->size()));
        (void)0;
        doneOpenGLContextCurrent();
        return;
    }

    struct pl_tex_params tex_params = {
        .w = new_swapchain_size.width(),
        .h = new_swapchain_size.height(),
        .format = pl_find_fmt(placebo_vulkan->gpu, PL_FMT_UNORM, 4, 8, 8, PL_FMT_CAP_RENDERABLE),
        .sampleable = true,
        .renderable = true,
    };
    VkFormat vk_format = VK_FORMAT_UNDEFINED;
    pl_tex new_quick_tex = nullptr;
    if (!pl_tex_recreate(placebo_vulkan->gpu, &new_quick_tex, &tex_params)) {
        qCCritical(chiakiGui) << "Failed to create placebo texture";
        if (new_quick_tex)
            pl_tex_destroy(placebo_vulkan->gpu, &new_quick_tex);
        return;
    }
    VkImage new_quick_vk_image = pl_vulkan_unwrap(placebo_vulkan->gpu, new_quick_tex, &vk_format, nullptr);
    if (!new_quick_vk_image) {
        qCCritical(chiakiGui) << "Failed to unwrap placebo texture";
        pl_tex_destroy(placebo_vulkan->gpu, &new_quick_tex);
        return;
    }
    if (quick_tex)
        pl_tex_destroy(placebo_vulkan->gpu, &quick_tex);
    quick_tex = new_quick_tex;
    quick_vk_image = new_quick_vk_image;
    quick_vk_format = vk_format;
    quick_sem_value = 0;
    swapchain_size = new_swapchain_size;
    quick_window->setRenderTarget(QQuickRenderTarget());
}

void QmlMainWindow::updateSwapchain()
{
    Q_ASSERT(QThread::currentThread() == QGuiApplication::instance()->thread());
    const qint64 update_swapchain_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());

    quick_item->setSize(size());
    quick_window->resize(size());

    if (quick_render->thread() == QThread::currentThread())
        resizeSwapchain();
    else
        QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::resizeSwapchain, this), Qt::QueuedConnection);
    quick_render->polishItems();
    if (quick_render->thread() == QThread::currentThread())
        sync();
    else
        QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::sync, this), Qt::QueuedConnection);
    QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::render, this), Qt::QueuedConnection);
    const qint64 update_swapchain_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (update_swapchain_end_us >= update_swapchain_begin_us && update_swapchain_end_us - update_swapchain_begin_us >= 12000) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] update_swapchain_total_us=" << (update_swapchain_end_us - update_swapchain_begin_us);
    }
}

void QmlMainWindow::sync()
{
    Q_ASSERT(QThread::currentThread() == render_thread);

    if (!quick_tex)
        return;

    const qint64 sync_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    const qint64 sync_begin_frame_begin_us = sync_begin_us;
    beginFrame();
    const qint64 sync_begin_frame_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (sync_begin_frame_end_us >= sync_begin_frame_begin_us) {
        const qint64 sync_begin_frame_us = sync_begin_frame_end_us - sync_begin_frame_begin_us;
        if (sync_begin_frame_us >= 1000) {
            CHIAKI_NOISY_DEBUG().nospace()
                << "[latency] sync_beginFrame_us=" << sync_begin_frame_us;
        }
    }
    if (!quick_frame)
        return;
    const qint64 sync_quick_render_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    quick_need_render.storeRelaxed(quick_render->sync() ? 1 : 0);
    const qint64 sync_quick_render_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (sync_quick_render_end_us >= sync_quick_render_begin_us) {
        const qint64 sync_quick_render_us = sync_quick_render_end_us - sync_quick_render_begin_us;
        if (sync_quick_render_us >= 1000) {
            CHIAKI_NOISY_DEBUG().nospace()
                << "[latency] sync_quick_render_sync_us=" << sync_quick_render_us;
        }
    }
    const qint64 sync_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (sync_end_us >= sync_begin_us && sync_end_us - sync_begin_us >= 3000) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[latency] render_sync_us=" << (sync_end_us - sync_begin_us);
    }
}

void QmlMainWindow::beginFrame()
{
    if (quick_frame)
        return;

    if (render_backend == RenderBackend::OpenGL) {
        if (!makeOpenGLContextCurrent())
            return;
        quick_frame = true;
        quick_render->beginFrame();
        return;
    }

    if (!quick_tex || !quick_sem || !quick_vk_image)
        return;
    quick_sem_value += 1;

    const qint64 quick_hold_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    struct pl_vulkan_hold_params hold_params = {
        .tex = quick_tex,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .qf = VK_QUEUE_FAMILY_IGNORED,
        .semaphore = {
            .sem = quick_sem,
            .value = quick_sem_value,
        }
    };
    pl_vulkan_hold_ex(placebo_vulkan->gpu, &hold_params);
    const qint64 quick_hold_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (quick_hold_after_us >= quick_hold_begin_us) {
        const qint64 quick_hold_us = quick_hold_after_us - quick_hold_begin_us;
        if (quick_hold_us >= 1000) {
            CHIAKI_NOISY_DEBUG().nospace()
                << "[latency] beginFrame_hold_us=" << quick_hold_us;
        }
        logQuickBeginHoldStats(quick_hold_after_us, quick_hold_after_us - quick_hold_begin_us);
    }

    const qint64 quick_wait_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    VkSemaphoreWaitInfo wait_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &quick_sem,
        .pValues = &quick_sem_value,
    };
    const VkResult wait_result = vk_funcs.vkWaitSemaphores(placebo_vulkan->device, &wait_info, UINT64_MAX);
    if (wait_result != VK_SUCCESS) {
        qCWarning(chiakiGui)
            << "Failed waiting for Qt Quick Vulkan semaphore"
            << "result=" << wait_result;
        struct pl_vulkan_release_params release_params = {
            .tex = quick_tex,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .qf = VK_QUEUE_FAMILY_IGNORED,
        };
        pl_vulkan_release_ex(placebo_vulkan->gpu, &release_params);
        if (wait_result == VK_ERROR_DEVICE_LOST) {
            handleVulkanDeviceLost(QStringLiteral("vkWaitSemaphores returned VK_ERROR_DEVICE_LOST"));
        } else {
            swapchain_recreate_pending.storeRelease(1);
            QMetaObject::invokeMethod(this, [this]() {
                scheduleUpdate(true, UpdateRequestReason::VSync);
            }, Qt::QueuedConnection);
        }
        return;
    }
    const qint64 quick_wait_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (quick_wait_after_us >= quick_wait_begin_us) {
        quick_begin_wait_last_us = quick_wait_after_us - quick_wait_begin_us;
        if (quick_begin_wait_last_us >= 1000) {
            CHIAKI_NOISY_DEBUG().nospace()
                << "[latency] beginFrame_wait_us=" << quick_begin_wait_last_us;
        }
        logQuickBeginWaitStats(quick_wait_after_us, quick_begin_wait_last_us);
    }

    quick_window->setRenderTarget(QQuickRenderTarget::fromVulkanImage(
        quick_vk_image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        quick_vk_format,
        swapchain_size));
    if (startup_video_visible_pending.loadAcquire() != 0)
        scheduleUpdate(false, UpdateRequestReason::PendingFrame);
    quick_frame = true;
    const qint64 quick_qt_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    quick_render->beginFrame();
    const qint64 quick_qt_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (quick_qt_after_us >= quick_qt_begin_us) {
        const qint64 quick_qt_us = quick_qt_after_us - quick_qt_begin_us;
        if (quick_qt_us >= 1000) {
            CHIAKI_NOISY_DEBUG().nospace()
                << "[latency] beginFrame_qt_us=" << quick_qt_us;
        }
        logQuickBeginQtStats(quick_qt_after_us, quick_qt_after_us - quick_qt_begin_us);
    }
}

void QmlMainWindow::endFrame()
{
    if (!quick_frame)
        return;

    if (render_backend == RenderBackend::OpenGL) {
        quick_frame = false;
        quick_render->endFrame();
        doneOpenGLContextCurrent();
        return;
    }

    quick_frame = false;
    const qint64 quick_end_qt_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    quick_render->endFrame();
    const qint64 quick_end_qt_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (quick_end_qt_after_us >= quick_end_qt_begin_us)
        logQuickEndQtStats(quick_end_qt_after_us, quick_end_qt_after_us - quick_end_qt_begin_us);

    const qint64 quick_release_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    struct pl_vulkan_release_params release_params = {
        .tex = quick_tex,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .qf = VK_QUEUE_FAMILY_IGNORED,
    };
    pl_vulkan_release_ex(placebo_vulkan->gpu, &release_params);
    const qint64 quick_release_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (quick_release_after_us >= quick_release_begin_us)
        logQuickReleaseStats(quick_release_after_us, quick_release_after_us - quick_release_begin_us);
}

void QmlMainWindow::handleVulkanDeviceLost(const QString &reason)
{
    handleVulkanRendererFallback(
        tr("Vulkan device lost"),
        tr("The Vulkan renderer stopped responding. Chiaki-ng will relaunch with OpenGL for this run only.\n\nReason: %1").arg(reason),
        tr("Vulkan device was lost. OpenGL is being used for this launch only. Reason: %1").arg(reason));
}

void QmlMainWindow::handleVulkanRendererFallback(const QString &title, const QString &message, const QString &fallback_reason)
{
    if (render_backend != RenderBackend::Vulkan)
        return;
    if (vulkan_device_lost.fetchAndStoreRelaxed(1) != 0)
        return;

    qCCritical(chiakiGui) << "Vulkan renderer fallback:" << title << fallback_reason;
    vulkan_deferred_swap_enabled = false;
    swapchain_recreate_pending.storeRelease(0);
    deferred_present_in_flight.storeRelease(0);
    quick_need_sync.storeRelease(0);
    quick_need_render.storeRelease(0);
    if (present_pace_thread)
        present_pace_thread->disarm();
    if (buffered_pace_thread)
        buffered_pace_thread->disarm();

    QMetaObject::invokeMethod(this, [this, title, message, fallback_reason]() {
        if (backend) {
            QMetaObject::invokeMethod(backend,
                                      "sessionError",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, title),
                                      Q_ARG(QString, message));
            backend->stopSession(false);
        }

        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments(QCoreApplication::arguments().mid(1));
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("CHIAKI_FORCE_VULKAN_FALLBACK"), QStringLiteral("1"));
        env.insert(QStringLiteral("CHIAKI_FORCE_VULKAN_FALLBACK_REASON"), fallback_reason);
        process.setProcessEnvironment(env);
        if (!process.startDetached())
            qCCritical(chiakiGui) << "Failed to relaunch with OpenGL after Vulkan renderer failure";

        QTimer::singleShot(0, QGuiApplication::instance(), &QGuiApplication::quit);
    }, Qt::QueuedConnection);
}

void QmlMainWindow::render()
{
    Q_ASSERT(QThread::currentThread() == render_thread);
    if (vulkan_device_lost.loadAcquire() != 0) {
        QMutexLocker locker(&render_schedule_mutex);
        render_scheduled = false;
        render_pending = false;
        render_pending_during_cycle.storeRelaxed(0);
        return;
    }
    const qint64 render_entry_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    last_render_entry_us.storeRelease(render_entry_us);
    render_pending_during_cycle.storeRelaxed(0);
    const qint64 schedule_us = last_schedule_update_us.loadAcquire();
    if (schedule_us > 0 && render_entry_us >= schedule_us)
        logLatencyStats("schedule_to_render", render_entry_us - schedule_us);
    auto snapshotRenderScheduleState = [this]() {
        QMutexLocker locker(&render_schedule_mutex);
        return qMakePair(render_scheduled, render_pending);
    };
    auto snapshotQueueDepth = [this]() {
        const int cached_depth = queue_depth_cached.loadAcquire();
        if (cached_depth > 0)
            return cached_depth;
        if (!placebo_state_mutex.tryLock())
            return 0;
        const int depth = pl_queue_num_frames(placebo_queue);
        placebo_state_mutex.unlock();
        queue_depth_cached.storeRelease(depth);
        return depth;
    };
    if (detailedVerboseTelemetryEnabled()) {
        const qint64 update_us = last_update_us;
        if (update_us > 0 && render_entry_us >= update_us)
            logUpdateToRenderStats(render_entry_us, render_entry_us - update_us);
        const qint64 dispatch_us = last_render_dispatch_us.loadAcquire();
        if (dispatch_us > 0 && render_entry_us >= dispatch_us)
            logLatencyStats("render_dispatch_to_entry", render_entry_us - dispatch_us);
        const qint64 last_render_idle_us_local = last_render_idle_us.loadAcquire();
        if (last_render_idle_us_local > 0 && render_entry_us >= last_render_idle_us_local)
            logLatencyStats("render_idle_to_entry", render_entry_us - last_render_idle_us_local);
        const qint64 request_age_us = schedule_us > 0 ? qMax<qint64>(0, render_entry_us - schedule_us) : -1;
        const qint64 idle_age_us = last_render_idle_us_local > 0 ? qMax<qint64>(0, render_entry_us - last_render_idle_us_local) : -1;
        const auto render_schedule_state = snapshotRenderScheduleState();
        const int queue_depth_snapshot = snapshotQueueDepth();
        logRenderHandoffStats(render_entry_us,
                              idle_age_us,
                              request_age_us,
                              queue_depth_snapshot,
                              hasPendingFrame(),
                              render_active.loadAcquire() != 0,
                              render_schedule_state.first,
                              render_schedule_state.second);
    }

    struct RenderActiveGuard {
        QmlMainWindow *self;
        QAtomicInteger<int> &flag;
        qint64 render_entry_us;
        RenderActiveGuard(QmlMainWindow *self, QAtomicInteger<int> &flag, qint64 render_entry_us)
            : self(self), flag(flag), render_entry_us(render_entry_us)
        {
            flag.storeRelease(1);
        }
        ~RenderActiveGuard()
        {
            flag.storeRelease(0);
            const qint64 render_idle_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            self->last_render_idle_us.storeRelease(render_idle_us);
            bool render_pending_now = false;
            bool pending_frame_waiting_now = false;
            int pending_during_cycle_now = 0;
            {
                QMutexLocker locker(&self->render_schedule_mutex);
                render_pending_now = self->render_pending;
                pending_during_cycle_now = self->render_pending_during_cycle.loadAcquire();
            }
            pending_frame_waiting_now = self->hasPendingFrame();
            if (render_idle_us >= render_entry_us)
                logRenderIdleStats(render_idle_us,
                                   render_idle_us - render_entry_us,
                                   render_pending_now,
                                   pending_frame_waiting_now,
                                   pending_during_cycle_now);
            if (pending_frame_waiting_now)
                self->queuePendingFrameRelease();
            else
                self->scheduleRenderIfBacklog(QmlMainWindow::UpdateRequestReason::PendingFrame);
            if (self->present_pace_timer_rearm.fetchAndStoreRelaxed(0) != 0) {
                bool render_scheduled_now = false;
                {
                    QMutexLocker locker(&self->render_schedule_mutex);
                    render_scheduled_now = self->render_scheduled || self->render_pending;
                }
                if (render_scheduled_now) {
                    // A new render is already queued; keep the rearm flag so the
                    // pacer retries after that render finishes too.
                    self->present_pace_timer_rearm.storeRelease(1);
                } else {
                    // Render is now idle and the deferred present pacer was
                    // blocked waiting for us.  Stop the 1ms polling retry and
                    // directly schedule the VSync update — event-driven rather
                    // than busy-polling at ~200 Hz.
                    if (self->present_pace_thread)
                        self->present_pace_thread->disarm();
                    self->scheduleUpdate(false, QmlMainWindow::UpdateRequestReason::VSync);
                }
            }
        }
    } render_guard(this, render_active, render_entry_us);

    if (pending_frame_release_queued.loadAcquire() != 0) {
        const qint64 pending_release_queued_us = pending_frame_release_queued_us.loadAcquire();
        if (pending_release_queued_us > 0 && render_entry_us >= pending_release_queued_us) {
            logLatencyStats("pending_release_wait_to_render", render_entry_us - pending_release_queued_us);
        }
        if (applyPendingFrameIfQueueHasCapacity())
            pending_frame_release_queued.storeRelease(0);
    }

    auto queueHasWork = [this]() {
        const int cached_depth = queue_depth_cached.loadAcquire();
        if (cached_depth > 0)
            return true;
        if (!placebo_state_mutex.tryLock())
            return false;
        const int depth = pl_queue_num_frames(placebo_queue);
        placebo_state_mutex.unlock();
        queue_depth_cached.storeRelease(depth);
        return depth > 0;
    };

    auto hasBufferedWork = [this]() {
        const int cached_depth = queue_depth_cached.loadAcquire();
        if (cached_depth > 0)
            return true;
        if (placebo_state_mutex.tryLock()) {
            const int depth = pl_queue_num_frames(placebo_queue);
            placebo_state_mutex.unlock();
            queue_depth_cached.storeRelease(depth);
            if (depth > 0)
                return true;
        }
        if (hasPendingFrame())
            return true;
        if (placebo_reset_pending.loadAcquire() != 0)
            return true;
        return false;
    };

    auto finalize_render = [this]() {
        bool pending = false;
        {
            QMutexLocker locker(&render_schedule_mutex);
            render_scheduled = false;
            pending = render_pending;
            render_pending = false;
        }
        if (pending) {
            const int pending_during_cycle = render_pending_during_cycle.loadAcquire();
            if (pending_during_cycle > 0) {
                CHIAKI_NOISY_DEBUG().nospace()
                    << "[latency] render_cycle_pending count=" << pending_during_cycle;
            }
            const bool pending_release_armed = pending_frame_release_queued.loadAcquire() != 0;
            QMetaObject::invokeMethod(this, [this, pending_release_armed]() {
                queue_stored_frame_pending.fetchAndStoreRelaxed(0);
                if (render_backend == RenderBackend::Vulkan && vulkan_deferred_swap_enabled && deferred_present_in_flight.loadAcquire() != 0)
                    return;
                if (pending_release_armed && hasPendingFrame())
                    scheduleUpdate(false, UpdateRequestReason::PendingFrame);
                else
                    scheduleUpdate(false, UpdateRequestReason::FinalizePending);
            }, Qt::QueuedConnection);
        } else {
            ui_priority_update_pending.storeRelease(0);
            scheduleRenderIfBacklog(UpdateRequestReason::PendingFrame);
        }
        return pending;
    };
    const bool deferred_present_active = render_backend == RenderBackend::Vulkan &&
        vulkan_deferred_swap_enabled &&
        deferred_present_in_flight.loadAcquire() != 0;
    if (deferred_present_active) {
        finalize_render();
        return;
    }
    const bool swapchain_recreated = swapchain_recreate_pending.fetchAndStoreRelaxed(0) != 0;
    if (swapchain_recreated) {
        destroySwapchain();
        createSwapchain();
        resizeSwapchain();
    }

    auto schedulePostSwapchainRecreateRefresh = [this, swapchain_recreated]() {
    if (!swapchain_recreated)
            return;
        QMetaObject::invokeMethod(this, [this]() {
            armQuickNeedSync("swapchain_recreate_refresh");
            quick_need_render.storeRelaxed(1);
            scheduleUpdate(true, UpdateRequestReason::VSync);
        }, Qt::QueuedConnection);
    };

    if (placebo_reset_pending.fetchAndStoreRelaxed(0) != 0)
        resetPlaceboQueue();

    if (!placebo_swapchain)
    {
        bool scheduled_pending = finalize_render();
        if (!scheduled_pending && (queueHasWork() || placebo_reset_pending.loadAcquire() != 0))
            scheduleUpdate(false, UpdateRequestReason::NoSwapchain);
        schedulePostSwapchainRecreateRefresh();
        return;
    }

    if (renderer_cache_flush_pending.fetchAndStoreRelaxed(0) && placebo_renderer)
        pl_renderer_flush_cache(placebo_renderer);

    const struct pl_render_params *render_params = &pl_render_default_params;
    const PlaceboUpscaler configured_upscaler = settings->GetPlaceboUpscaler();
    switch (video_preset) {
    case VideoPreset::Fast:
        render_params = &pl_render_fast_params;
        break;
    case VideoPreset::Default:
        render_params = &pl_render_default_params;
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::HighQuality:
        render_params = &pl_render_high_quality_params;
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::HighQualitySpatial:
        render_params = &pl_render_high_quality_params;
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::HighQualityAdvancedSpatial:
        render_params = &pl_render_high_quality_params;
        if (render_params->deinterlace_params)
            this->renderparams_opts->params.deinterlace_params = render_params->deinterlace_params;
        break;
    case VideoPreset::Custom:
        if (renderparams_changed) {
            renderparams_changed = false;
            QMap<QString, QString> paramsData = settings->GetPlaceboValues();
            QMapIterator<QString, QString> i(paramsData);
            bool invalid_render_params = false;
            {
                QMutexLocker locker(&placebo_state_mutex);
                this->renderparams_opts->params = pl_render_default_params;
                if (pl_render_default_params.deinterlace_params)
                    this->renderparams_opts->params.deinterlace_params = pl_render_default_params.deinterlace_params;
                while (i.hasNext()) {
                    i.next();
                    if ((i.key() == QLatin1String("upscaler") || i.key() == QLatin1String("plane_upscaler"))
                        && uses_custom_upscale_hook(configured_upscaler)) {
                        continue;
                    }
                    if(!pl_options_set_str(this->renderparams_opts, i.key().toUtf8().constData(), i.value().toUtf8().constData()))
                    {
                        invalid_render_params = true;
                        qCCritical(chiakiGui) << "Failed to load custom render param: " << i.key() << " with value: " << i.value();
                    }
                }
            }
            if (invalid_render_params)
                qCInfo(chiakiGui) << "Updated custom render parameters with one or more invalid parameters.";
            else
                qCInfo(chiakiGui) << "Updated custom render parameters successfully.";
        }
        render_params = &(this->renderparams_opts->params);
        break;
    }

    struct pl_render_params params = *render_params;
    params.frame_mixer = effectiveFrameMixerConfig(&params);
    const bool content_timed = params.frame_mixer == nullptr;
    const bool mixer_active = params.frame_mixer != nullptr;
    const bool has_settings = settings != nullptr;
    const bool apply_display_target = has_settings && stream_session_active.loadAcquire() != 0;
    const int target_trc = apply_display_target ? settings->GetDisplayTargetTrc() : 0;
    const int target_prim = apply_display_target ? settings->GetDisplayTargetPrim() : 0;
    const int target_peak = apply_display_target ? settings->GetDisplayTargetPeak() : 0;
    const int target_contrast = apply_display_target ? settings->GetDisplayTargetContrast() : 0;

    uint64_t ts_pre_update = chiaki_time_now_monotonic_us();
    double refresh_rate = screen() ? screen()->refreshRate() : 60.0;
    if (refresh_rate <= 1.0)
        refresh_rate = 60.0;
    const double refresh_interval_s = 1.0 / refresh_rate;
    const double stream_interval_s = stream_configured_frame_interval_ms > 0.0
        ? stream_configured_frame_interval_ms / 1000.0
        : 0.0;
    const double vsync_duration = refresh_interval_s;
    const double present_interval_s = refresh_interval_s;
    const bool stream_active = stream_session_active.loadAcquire() != 0;
    const bool timer_owned_playback = stream_active && mixer_active;
    const double present_submit_interval_s = timer_owned_playback
        ? present_interval_s
        : stream_interval_s;
    const qint64 present_interval_us = static_cast<qint64>(qMax(1.0, vsync_duration * 1000000.0) + 0.5);
    const bool pending_overflow_waiting = hasPendingFrameOverflow();
    const bool pending_frame_waiting = snapshotQueueDepth() >= effectiveQueueDepthLimit() || hasPendingFrame();
    auto schedulePostRenderWork = [this]() {
        scheduleRenderIfBacklog(UpdateRequestReason::FinalizePending);
    };
    qparams.timeout = 0;
    schedule_frame_mixer_active.storeRelaxed(mixer_active ? 1 : 0);
    qparams.interpolation_threshold = mixer_active ? 0.01 : 0.0;
    qparams.radius = pl_frame_mix_radius(&params);
    qparams.vsync_duration = vsync_duration;
    qparams.pts = playback_started
        ? (double)(ts_pre_update - ts_start) / 1000000.0 + vsync_duration - kPtsStaleMarginS
        : 0.0;
    const int depth_limit = effectiveQueueDepthLimit();
    enum pl_queue_status queue_status;
    bool kept_frame_enqueued = false;
    const qint64 queue_update_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    int queue_depth_before_update = -1;
    struct pl_frame direct_render_frame = {};
    const struct pl_frame *direct_frames[] = { &direct_render_frame };
    const float direct_timestamps[] = { 0.0f };
    if (bypass_frame_queue) {
        AVFrame *incoming = nullptr;
        bool reset_direct_frame = false;
        {
            QMutexLocker locker(&direct_frame_mutex);
            std::swap(incoming, direct_pending_frame);
            reset_direct_frame = direct_frame_reset.fetchAndStoreRelaxed(0) != 0;
        }
        if (reset_direct_frame) {
            pl_unmap_avframe(placeboGpu(), &direct_frame);
            direct_frame = {};
        }
        if (incoming) {
            pl_unmap_avframe(placeboGpu(), &direct_frame);
            direct_frame = {};
            pl_avframe_params avparams = {};
            avparams.frame = incoming;
            avparams.tex = placebo_tex.data();
            if (!pl_map_avframe_ex(placeboGpu(), &direct_frame, &avparams)) {
                qCWarning(chiakiGui) << "Failed to map direct video frame";
                pl_unmap_avframe(placeboGpu(), &direct_frame);
                direct_frame = {};
                if (backend && backend->zeroCopy())
                    backend->disableZeroCopy();
            }
            av_frame_free(&incoming);
        }
        direct_render_frame = direct_frame;
        frame_mix = {};
        frame_mix.num_frames = direct_frame.num_planes > 0 ? 1 : 0;
        frame_mix.frames = direct_frames;
        frame_mix.timestamps = direct_timestamps;
        queue_status = PL_QUEUE_OK;
        queue_depth_before_update = 0;
    } else {
        TimedMutexLocker locker(placebo_state_mutex, "render_queue_update");
        queue_depth_before_update = pl_queue_num_frames(placebo_queue);
        const qint64 queue_update_work_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        queue_status = pl_queue_update(placebo_queue, &frame_mix, &qparams);
        queue_depth_cached.storeRelease(pl_queue_num_frames(placebo_queue));
        const qint64 queue_update_work_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (queue_update_work_end_us >= queue_update_work_begin_us) {
            const qint64 queue_update_work_us = queue_update_work_end_us - queue_update_work_begin_us;
            if (queue_update_work_us >= 500) {
                CHIAKI_NOISY_DEBUG().nospace()
                    << "[latency] render_pl_queue_update_us=" << queue_update_work_us
                    << " queue_depth=" << queue_depth_before_update;
            }
        }
        if (frame_mix.num_frames == 0 &&
            !pending_frame_waiting &&
            queue_depth_before_update < depth_limit &&
            loading_transition_complete.loadAcquire() != 0) {
            double used_origin = -1.0;
            const qint64 kept_enqueue_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (enqueueKeptFrame(queue_pts_origin, this->renderparams_opts->params.deinterlace_params, used_origin)) {
                kept_frame_enqueued = true;
                queue_pts_origin = used_origin;
                setQueuePtsOriginCached(queue_pts_origin);
                if (!playback_started) {
                    uint64_t now_us = chiaki_time_now_monotonic_us();
                    ts_start = now_us;
                    playback_started = true;
                }
                queue_status = pl_queue_update(placebo_queue, &frame_mix, &qparams);
                queue_depth_cached.storeRelease(pl_queue_num_frames(placebo_queue));
            }
            const qint64 kept_enqueue_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (kept_enqueue_end_us >= kept_enqueue_begin_us) {
                const qint64 kept_enqueue_us = kept_enqueue_end_us - kept_enqueue_begin_us;
                if (kept_enqueue_us >= 500) {
                    CHIAKI_NOISY_DEBUG().nospace()
                        << "[latency] render_enqueue_kept_frame_us=" << kept_enqueue_us
                        << " queue_depth=" << queue_depth_before_update;
                }
            }
        }
    }
    switch (queue_status) {
    case PL_QUEUE_ERR:
        finalize_render();
        return;
    case PL_QUEUE_EOF:
    case PL_QUEUE_OK:
    case PL_QUEUE_MORE:
        break;
    }
    if (pending_frame_waiting) {
        if (applyPendingFrameIfQueueHasCapacity())
            pending_frame_release_queued.storeRelease(0);
        else
            queuePendingFrameRelease();
    }
    {
        const qint64 queue_update_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (queue_update_after_us >= queue_update_begin_us) {
            logRenderQueueUpdateStats(queue_update_after_us, queue_update_after_us - queue_update_begin_us);
            logRenderPhaseStall(queue_update_after_us,
                                "queue_update",
                                queue_update_after_us - queue_update_begin_us,
                                queue_depth_before_update,
                                depth_limit,
                                pending_frame_waiting,
                                render_active.loadAcquire() != 0,
                                snapshotRenderScheduleState().first,
                                snapshotRenderScheduleState().second);
        }
    }
    updateQueueDepthAverage(snapshotQueueDepth());
    refreshPendingFrameAge();

    if (render_backend == RenderBackend::OpenGL && quick_need_render.loadRelaxed()) {
        const bool quick_skip_next = stream_session_active.loadAcquire() != 0 && quick_render_skip_next != 0;
        if (quick_skip_next && !quick_frame) {
            quick_render_skip_next = 0;
            quick_need_render.storeRelaxed(1);
        } else {
            quick_need_render.storeRelaxed(0);
            const qint64 quick_begin_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (!quick_frame)
                beginFrame();
            const qint64 quick_begin_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (quick_begin_after_us >= quick_begin_begin_us)
                logQuickBeginFrameStats(quick_begin_after_us, quick_begin_after_us - quick_begin_begin_us);
            if (quick_frame) {
                const qint64 quick_render_call_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                clearQuickOpenGLTarget(quick_fbo);
                quick_render->render();
                const qint64 quick_render_call_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                if (quick_render_call_after_us >= quick_render_call_begin_us)
                    logQuickRenderStats(quick_render_call_after_us, quick_render_call_after_us - quick_render_call_begin_us);
            }
            const qint64 quick_end_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            endFrame();
            const qint64 quick_end_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (quick_end_after_us >= quick_end_begin_us)
                logQuickEndFrameStats(quick_end_after_us, quick_end_after_us - quick_end_begin_us);
            quick_render_skip_next = stream_session_active.loadAcquire() != 0 &&
                render_pending_during_cycle.loadAcquire() > 1 &&
                quick_begin_wait_last_us > 1500 ? 1 : 0;
        }
    }

    const qint64 start_frame_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    const int queue_depth_before_start = snapshotQueueDepth();
    const bool start_frame_backlogged = pending_frame_waiting;
    if (start_frame_backlogged && queue_depth_before_start >= depth_limit) {
        logRenderPhaseStall(start_frame_begin_us,
                            "start_frame_deferred",
                            0,
                            queue_depth_before_start,
                            depth_limit,
                            pending_frame_waiting,
                            render_active.loadAcquire() != 0,
                            snapshotRenderScheduleState().first,
                            snapshotRenderScheduleState().second);
        finalize_render();
    }
    const qint64 last_present_complete_us = this->last_present_complete_us.loadAcquire();
    if (last_present_complete_us > 0 && start_frame_begin_us >= last_present_complete_us) {
        const qint64 swap_to_start_gap_us = start_frame_begin_us - last_present_complete_us;
        logSwapToStartGapStats(start_frame_begin_us, start_frame_begin_us - last_present_complete_us);
        const bool update_swap_to_start_gap_estimate = !pending_frame_waiting &&
            queue_depth_before_start < depth_limit &&
            present_interval_us > 0 &&
            swap_to_start_gap_us <= (present_interval_us * 2);
        if (update_swap_to_start_gap_estimate) {
            const qint64 prior_swap_to_start_gap_estimate_us = this->swap_to_start_gap_estimate_us.loadAcquire();
            const qint64 next_swap_to_start_gap_estimate_us = prior_swap_to_start_gap_estimate_us > 0
                ? ((prior_swap_to_start_gap_estimate_us * 7) + swap_to_start_gap_us) / 8
                : swap_to_start_gap_us;
            this->swap_to_start_gap_estimate_us.storeRelease(next_swap_to_start_gap_estimate_us);
        }
    }
    struct pl_swapchain_frame sw_frame = {};
    {
        QMutexLocker locker(&placebo_swapchain_mutex);
        if (!pl_swapchain_start_frame(placebo_swapchain, &sw_frame)) {
            const bool surface_renderable = isExposed() && width() > 0 && height() > 0;
            const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            int failures = 0;
            qint64 failure_duration_us = 0;
            if (surface_renderable) {
                failures = swapchain_start_failures.fetchAndAddRelaxed(1) + 1;
                if (swapchain_start_failure_first_us <= 0)
                    swapchain_start_failure_first_us = now_us;
                failure_duration_us = now_us - swapchain_start_failure_first_us;
            } else {
                swapchain_start_failures.storeRelease(0);
                swapchain_start_failure_first_us = 0;
            }
            qCWarning(chiakiGui)
                << "Failed to start Placebo frame"
                << "consecutive_failures=" << failures
                << "failure_duration_us=" << failure_duration_us
                << "surface_renderable=" << surface_renderable;
            if (surface_renderable && failures >= 60 && failure_duration_us >= 3000000) {
                handleVulkanRendererFallback(
                    tr("Vulkan renderer stalled"),
                    tr("The Vulkan swapchain could not recover. Chiaki-ng will relaunch with OpenGL for this run only."),
                    tr("Vulkan swapchain failed repeatedly while the window was visible. OpenGL is being used for this launch only."));
            } else {
                swapchain_recreate_pending.storeRelease(1);
                QMetaObject::invokeMethod(this, [this]() {
                    scheduleUpdate(true, UpdateRequestReason::VSync);
                }, Qt::QueuedConnection);
            }
            finalize_render();
            return;
        }
    }
    swapchain_start_failures.storeRelease(0);
    swapchain_start_failure_first_us = 0;
    const qint64 start_frame_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (start_frame_us >= start_frame_begin_us)
    {
        const qint64 start_frame_block_us = start_frame_us - start_frame_begin_us;
        logLatencyStats("start_frame_block", start_frame_block_us);
        const auto render_schedule_state = snapshotRenderScheduleState();
        logRenderPhaseStall(start_frame_us,
                            "start_frame",
                            start_frame_block_us,
                            queue_depth_before_start,
                            depth_limit,
                            pending_frame_waiting,
                            render_active.loadAcquire() != 0,
                                render_schedule_state.first,
                                render_schedule_state.second);
        logPresentOutlier("start_frame_block",
                          start_frame_us,
                          start_frame_block_us,
                          false,
                          present_vsync_enabled,
                          present_interval_us,
                          queue_depth_before_start,
                          depth_limit,
                          pending_frame_waiting,
                          render_active.loadAcquire() != 0,
                          render_schedule_state.first,
                          render_schedule_state.second);
        if (!start_frame_backlogged && present_interval_us > 0 && start_frame_block_us <= (present_interval_us * 2)) {
            const qint64 prior_estimate_us = start_frame_block_estimate_us.loadAcquire();
            const qint64 next_estimate_us = prior_estimate_us > 0
                ? ((prior_estimate_us * 7) + start_frame_block_us) / 8
                : start_frame_block_us;
            start_frame_block_estimate_us.storeRelease(next_estimate_us);
        }
    }
    const qint64 render_to_start_frame_us = start_frame_begin_us;
    const qint64 render_entry_us_local = last_render_entry_us.loadAcquire();
    if (render_entry_us_local > 0 && render_to_start_frame_us >= render_entry_us_local)
        logLatencyStats("render_to_start_frame", render_to_start_frame_us - render_entry_us_local);
    // Submit immediately and let the swapchain / render path enforce pacing.
    auto close_started_frame = [this, timer_owned_playback, present_interval_us, start_frame_us, depth_limit, pending_frame_waiting, pending_overflow_waiting, snapshotQueueDepth, snapshotRenderScheduleState, present_submit_interval_s](bool present) -> bool {
        const qint64 submit_ready_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        const int queue_depth_now = snapshotQueueDepth();
        if (detailedVerboseTelemetryEnabled()) {
            static qint64 last_present_trace_log_us = 0;
            static int present_true_count = 0;
            static int present_false_count = 0;
            if (present)
                present_true_count++;
            else
                present_false_count++;
            if (last_present_trace_log_us == 0)
                last_present_trace_log_us = submit_ready_us;
            else if (submit_ready_us - last_present_trace_log_us >= 1000000) {
                CHIAKI_NOISY_DEBUG().nospace()
                    << "[latency] present_trace present_true=" << present_true_count
                    << " present_false=" << present_false_count
                    << " queue_depth=" << queue_depth_now
                    << " timer_owned_playback=" << (timer_owned_playback ? 1 : 0)
                    << " render_backend=" << static_cast<int>(render_backend);
                last_present_trace_log_us = submit_ready_us;
                present_true_count = 0;
                present_false_count = 0;
            }
        }
        const double queue_depth_average = queueDepthAverage();
        if (queue_depth_now >= depth_limit ||
            hasPendingFrame()) {
            present_backpressure_active.storeRelease(1);
        } else if (queue_depth_now < depth_limit &&
                   queue_depth_average <= static_cast<double>(depth_limit) - 0.65) {
            present_backpressure_active.storeRelease(0);
        }
        const bool backlog_active = pending_frame_waiting;
        if (submit_ready_us >= start_frame_us) {
            logLatencyStats("start_frame_to_submit", submit_ready_us - start_frame_us);
            const qint64 render_submit_us = submit_ready_us - start_frame_us;
            if (!backlog_active && present_interval_us > 0 && render_submit_us <= (present_interval_us * 2)) {
                const qint64 prior_render_submit_estimate_us = this->render_submit_estimate_us.loadAcquire();
                const qint64 next_render_submit_estimate_us = prior_render_submit_estimate_us > 0
                    ? ((prior_render_submit_estimate_us * 7) + render_submit_us) / 8
                    : render_submit_us;
                this->render_submit_estimate_us.storeRelease(next_render_submit_estimate_us);
            }
        }
        const bool use_deferred_swap = present &&
            render_backend == RenderBackend::Vulkan &&
            vulkan_deferred_swap_enabled &&
            deferred_swap_thread &&
            deferred_present_in_flight.loadAcquire() == 0;
        if (!use_deferred_swap) {
            if (present) {
                const double pace_multiplier = pending_overflow_waiting
                    ? 0.80
                    : (present_backpressure_active.loadAcquire() != 0 ? 0.90 : 1.0);
                const double paced_interval_s = present_submit_interval_s * pace_multiplier;
                throttleFramePresentation(paced_interval_s);
            }
            else
                throttleFramePresentation(0.0);
        }
        bool async_present_completion = false;
        qint64 present_complete_us = submit_ready_us;
        if (use_deferred_swap) {
            const bool async_enqueued = enqueueDeferredSwap(submit_ready_us,
                                                            queue_depth_now,
                                                            depth_limit,
                                                            pending_frame_waiting,
                                                            pending_overflow_waiting,
                                                            present_interval_us,
                                                            static_cast<qint64>(qMax(1.0, present_submit_interval_s * 1000000.0) + 0.5));
            if (async_enqueued) {
                async_present_completion = true;
                present_complete_us = submit_ready_us;
            }
        }
        if (!async_present_completion) {
            const qint64 submit_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            qint64 submit_us = submit_begin_us;
            {
                QMutexLocker locker(&placebo_swapchain_mutex);
                if (!pl_swapchain_submit_frame(placebo_swapchain))
                    qCWarning(chiakiGui) << "Failed to submit Placebo frame!";
                else {
                    submit_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            logLatencyStats("pl_swapchain_submit_frame", submit_us - submit_begin_us);
            CHIAKI_NOISY_DEBUG().nospace()
                << "[latency] pl_swapchain_submit_frame_us=" << (submit_us - submit_begin_us)
                << " queue_depth=" << pl_queue_num_frames(placebo_queue);
            const auto submit_render_state = snapshotRenderScheduleState();
            if (submit_us >= submit_begin_us)
            {
                const qint64 submit_call_us = submit_us - submit_begin_us;
                logLatencyStats("submit_call", submit_call_us);
                logLatencyStats("submit_frame_block", submit_call_us);
                logPresentOutlier("submit_call",
                                  submit_us,
                                  submit_call_us,
                                  false,
                                  present_vsync_enabled,
                                  present_interval_us,
                                  queue_depth_now,
                                  depth_limit,
                                  pending_frame_waiting,
                                  render_active.loadAcquire() != 0,
                                  submit_render_state.first,
                                  submit_render_state.second);
            }
            logRenderPhaseStall(submit_us,
                                "submit",
                                submit_us - submit_begin_us,
                                snapshotQueueDepth(),
                                depth_limit,
                                pending_frame_waiting,
                                render_active.loadAcquire() != 0,
                                submit_render_state.first,
                                submit_render_state.second);
            logPresentSubmitStats(submit_us, present, timer_owned_playback, present_interval_us);
            if (present) {
                static qint64 last_present_submit_us = 0;
                if (last_present_submit_us != 0 && submit_us >= last_present_submit_us) {
                    const qint64 present_submit_interval_us = submit_us - last_present_submit_us;
                    logPresentOutlier("present_interval",
                                      submit_us,
                                      present_submit_interval_us,
                                      false,
                                      present_vsync_enabled,
                                      present_interval_us,
                                      queue_depth_now,
                                      depth_limit,
                                      pending_frame_waiting,
                                      render_active.loadAcquire() != 0,
                                      submit_render_state.first,
                                      submit_render_state.second);
                }
                last_present_submit_us = submit_us;
            }
            }
            }
            if (present && !async_present_completion) {
                const qint64 present_swap_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                if (present_swap_begin_us >= submit_us)
                    logLatencyStats("submit_to_swap", present_swap_begin_us - submit_us);
                {
                    QMutexLocker locker(&placebo_swapchain_mutex);
                    pl_swapchain_swap_buffers(placebo_swapchain);
                }
                const qint64 swap_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                logSwapInterval(swap_us);
                this->last_swap_return_us.storeRelease(swap_us);
                present_complete_us = swap_us;
                const auto swap_render_state = snapshotRenderScheduleState();
                logRenderPhaseStall(swap_us,
                                    "swap",
                                    swap_us - present_swap_begin_us,
                                    snapshotQueueDepth(),
                                    depth_limit,
                                    pending_frame_waiting,
                                    render_active.loadAcquire() != 0,
                                    swap_render_state.first,
                                    swap_render_state.second);
                if (swap_us >= present_swap_begin_us)
                {
                    const qint64 swap_call_us = swap_us - present_swap_begin_us;
                    logLatencyStats("swap_call", swap_call_us);
                    logPresentOutlier("swap_call",
                                      swap_us,
                                      swap_call_us,
                                      false,
                                      present_vsync_enabled,
                                      present_interval_us,
                                      snapshotQueueDepth(),
                                      depth_limit,
                                      pending_frame_waiting,
                                      render_active.loadAcquire() != 0,
                                      swap_render_state.first,
                                      swap_render_state.second);
                }
                const qint64 render_us = last_render_entry_us.loadAcquire();
                if (render_us > 0 && swap_us >= render_us)
                    logLatencyStats("render_to_swap", swap_us - render_us);
                // Update exponential moving average of submit+swap cost so we sleep for right amount in throttle
                {
                    const qint64 observed = swap_us - submit_begin_us;
                    if (observed > 0) {
                        const long long prev = g_submit_swap_est_us.load(std::memory_order_acquire);
                        const long long next = static_cast<long long>(((prev * 7ll) + observed) / 8ll);
                        g_submit_swap_est_us.store(next, std::memory_order_release);
                    }
                }
                logPresentCompletionStats(swap_us,
                                          submit_begin_us,
                                          swap_us,
                                          snapshotQueueDepth(),
                                          render_backend,
                                          false);
            }
            if (present && !async_present_completion) {
                this->last_present_complete_us.storeRelease(present_complete_us);
                if (startup_video_visible_pending.loadAcquire() != 0) {
                    const quint64 startup_visibility_generation = startup_video_visible_generation.loadAcquire();
                    QMetaObject::invokeMethod(this, [this, startup_visibility_generation]() {
                        completeStartupVideoVisibility(startup_visibility_generation);
                    }, Qt::QueuedConnection);
                }
            }
        }
        return true;
    };

    const struct pl_frame *hint_frame = nullptr;
    for (int i = 0; i < frame_mix.num_frames; i++) {
        if (frame_mix.timestamps[i] > 0.0f)
            break;
        hint_frame = frame_mix.frames[i];
    }
    if (!hint_frame && frame_mix.num_frames)
        hint_frame = frame_mix.frames[0];
    struct pl_color_space hint = hint_frame ? hint_frame->color : pl_color_space{};
    struct pl_color_space target_csp = sw_frame.color_space;
    if (target_csp.hdr.max_luma <= 0.0f) {
        target_csp.hdr.max_luma = 0.0f;
        target_csp.hdr.min_luma = 0.0f;
    }

    if(target_trc)
        hint.transfer = static_cast<pl_color_transfer>(target_trc);

    if(target_prim)
    {
        hint.primaries = static_cast<pl_color_primaries>(target_prim);
        hint.hdr.prim = *pl_raw_primaries_get(hint.primaries);
    }

    if(target_peak)
        hint.hdr.max_luma = target_peak;

    switch(target_contrast)
    {
        case -1:
            hint.hdr.min_luma = 1e-7;
            break;
        case 0:
            hint.hdr.min_luma = target_csp.hdr.min_luma;
            break;
        default:
            // Infer max_luma for current pl_color_space
            struct pl_nominal_luma_params hint_params = {};
            hint_params.color = &hint;
            hint_params.metadata = PL_HDR_METADATA_HDR10;
            hint_params.scaling = PL_HDR_NITS;
            hint_params.out_max = &hint.hdr.max_luma;
            pl_color_space_nominal_luma_ex(&hint_params);
            hint.hdr.min_luma = hint.hdr.max_luma / (float)target_contrast;
            break;
    }
    const qint64 render_setup_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    const int queue_depth_before_render_setup = [&]() {
        QMutexLocker locker(&placebo_state_mutex);
        return pl_queue_num_frames(placebo_queue);
    }();
    {
        QMutexLocker locker(&placebo_swapchain_mutex);
        pl_swapchain_colorspace_hint(placebo_swapchain, &hint);
    }
    const qint64 render_setup_config_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (render_setup_config_after_us >= render_setup_begin_us) {
        const auto render_schedule_state = snapshotRenderScheduleState();
        logRenderPhaseStall(render_setup_config_after_us,
                            "render_setup_config",
                            render_setup_config_after_us - render_setup_begin_us,
                            queue_depth_before_render_setup,
                            depth_limit,
                            pending_frame_waiting,
                            render_active.loadAcquire() != 0,
                            render_schedule_state.first,
                            render_schedule_state.second);
    }

    struct pl_frame target_frame = {};
    pl_frame_from_swapchain(&target_frame, &sw_frame);
    if (target_frame.num_planes <= 0) {
        qCWarning(chiakiGui) << "Swapchain frame contains no planes, skipping render";
        close_started_frame(false);
        finalize_render();
        return;
    }
    struct pl_overlay_part overlay_part = {
        .src = {0, 0, static_cast<float>(swapchain_size.width()), static_cast<float>(swapchain_size.height())},
        .dst = {0, 0, static_cast<float>(swapchain_size.width()), static_cast<float>(swapchain_size.height())},
    };
    if (render_backend == RenderBackend::OpenGL) {
        overlay_part.src.y0 = static_cast<float>(swapchain_size.height());
        overlay_part.src.y1 = 0.0f;
    }
    struct pl_color_repr overlay_repr = pl_color_repr_rgb;
    overlay_repr.alpha = PL_ALPHA_PREMULTIPLIED;
    struct pl_overlay overlay = {
        .tex = quick_tex,
        .repr = overlay_repr,
        .color = pl_color_space_srgb,
        .parts = &overlay_part,
        .num_parts = 1,
    };
    target_frame.overlays = &overlay;
    target_frame.num_overlays = 1;
    if(target_prim)
    {
        target_frame.color.primaries = static_cast<pl_color_primaries>(target_prim);
        target_frame.color.hdr.prim = *pl_raw_primaries_get(target_frame.color.primaries);
    }

    if(target_trc)
        target_frame.color.transfer = static_cast<pl_color_transfer>(target_trc);

    if(target_peak && !target_frame.color.hdr.max_luma)
    {
        target_frame.color.hdr.max_luma = target_peak;
    }
    if(!target_frame.color.hdr.min_luma)
    {
        switch(target_contrast)
        {
            case -1:
                target_frame.color.hdr.min_luma = 1e-7;
                break;
            case 0:
                target_frame.color.hdr.min_luma = target_csp.hdr.min_luma;
                break;
            default:
                // Infer max_luma for current pl_color_space
                struct pl_nominal_luma_params target_params = {};
                target_params.color = &target_frame.color;
                target_params.metadata = PL_HDR_METADATA_HDR10;
                target_params.scaling = PL_HDR_NITS;
                target_params.out_max = &target_frame.color.hdr.max_luma;
                pl_color_space_nominal_luma_ex(&target_params);
                target_frame.color.hdr.min_luma = target_frame.color.hdr.max_luma / (float)target_contrast;
                break;
        }
    }
    if (quick_need_render.loadRelaxed()) {
        const bool quick_skip_next = stream_session_active.loadAcquire() != 0 && quick_render_skip_next != 0;
        if (quick_skip_next && !quick_frame) {
            quick_render_skip_next = 0;
            quick_need_render.storeRelaxed(1);
        } else {
            quick_need_render.storeRelaxed(0);
            const qint64 quick_begin_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (!quick_frame)
                beginFrame();
            const qint64 quick_begin_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (quick_begin_after_us >= quick_begin_begin_us)
                logQuickBeginFrameStats(quick_begin_after_us, quick_begin_after_us - quick_begin_begin_us);
            if (quick_frame) {
                const qint64 quick_render_call_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                if (render_backend == RenderBackend::OpenGL)
                    clearQuickOpenGLTarget(quick_fbo);
                quick_render->render();
                const qint64 quick_render_call_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
                if (quick_render_call_after_us >= quick_render_call_begin_us)
                    logQuickRenderStats(quick_render_call_after_us, quick_render_call_after_us - quick_render_call_begin_us);
            }
            const qint64 quick_end_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            endFrame();
            const qint64 quick_end_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
            if (quick_end_after_us >= quick_end_begin_us)
                logQuickEndFrameStats(quick_end_after_us, quick_end_after_us - quick_end_begin_us);
            quick_render_skip_next = stream_session_active.loadAcquire() != 0 &&
                render_pending_during_cycle.loadAcquire() > 1 &&
                quick_begin_wait_last_us > 1500 ? 1 : 0;
        }
    }

    {
    const qint64 render_setup_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (render_setup_after_us >= render_setup_begin_us) {
        const auto render_schedule_state = snapshotRenderScheduleState();
        logRenderPhaseStall(render_setup_after_us,
                            "render_setup",
                            render_setup_after_us - render_setup_begin_us,
                                queue_depth_before_render_setup,
                                depth_limit,
                                pending_frame_waiting,
                                render_active.loadAcquire() != 0,
                                render_schedule_state.first,
                                render_schedule_state.second);
        }
    }
    if (frame_mix.num_frames == 0) {
        if (kept_frame_enqueued) {
            close_started_frame(false);
            bool scheduled_pending = finalize_render();
            if (!scheduled_pending)
                schedulePostRenderWork();
            schedulePostSwapchainRecreateRefresh();
            return;
        }

        const qint64 render_entry_us_local = last_render_entry_us.loadAcquire();
        const qint64 render_call_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (render_entry_us_local > 0 && render_call_begin_us >= render_entry_us_local)
            logLatencyStats("render_prep", render_call_begin_us - render_entry_us_local);
        if (render_call_begin_us >= render_setup_begin_us)
            logRenderSetupStats(render_call_begin_us, render_call_begin_us - render_setup_begin_us);
        const bool rendered = pl_render_image(placebo_renderer, nullptr, &target_frame, &params);
        if (!rendered) {
            qCWarning(chiakiGui) << "Failed to render held/overlay-only frame";
            close_started_frame(false);
            finalize_render();
            schedulePostSwapchainRecreateRefresh();
            return;
        }
        const qint64 render_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (render_after_us >= render_call_begin_us) {
            logLatencyStats("render_image", render_after_us - render_call_begin_us);
            if (render_entry_us_local > 0 && render_after_us >= render_entry_us_local)
                logLatencyStats("render_work", render_after_us - render_entry_us_local);
        }
        close_started_frame(true);
        bool scheduled_pending = finalize_render();
        if (!scheduled_pending)
            schedulePostRenderWork();
        schedulePostSwapchainRecreateRefresh();
        return;
    }

    if (hint_frame) {
        pl_rect2df crop = hint_frame->crop;
        switch (video_mode) {
        case VideoMode::Normal:
            pl_rect2df_aspect_copy(&target_frame.crop, &crop, 0.0);
            break;
        case VideoMode::Stretch:
            // Nothing to do, target.crop already covers the full image
            break;
        case VideoMode::Zoom:
            if(zoom_factor == -1)
                pl_rect2df_aspect_copy(&target_frame.crop, &crop, 1.0);
            else
            {
                const float z = powf(2.0f, zoom_factor);
                const float sx = z * fabsf(pl_rect_w(crop)) / pl_rect_w(target_frame.crop);
                const float sy = z * fabsf(pl_rect_h(crop)) / pl_rect_h(target_frame.crop);
                pl_rect2df_stretch(&target_frame.crop, sx, sy);
            }
            break;
        }
    }
    // Disable background transparency by default if the swapchain does not
    // appear to support alpha transaprency
    if (sw_frame.color_repr.alpha == PL_ALPHA_NONE)
        params.background_transparency = 0.0;

    const struct pl_hook *fsrcnnx_hook = nullptr;
    if (hint_frame) {
        const float src_width = fabsf(pl_rect_w(hint_frame->crop));
        const float src_height = fabsf(pl_rect_h(hint_frame->crop));
        const float dst_width = fabsf(pl_rect_w(target_frame.crop));
        const float dst_height = fabsf(pl_rect_h(target_frame.crop));
        if (src_width > 0.0f && src_height > 0.0f) {
            const float upscale_factor = qMin(dst_width / src_width, dst_height / src_height);
            fsrcnnx_hook = select_spatial_hook(video_preset, configured_upscaler, upscale_factor,
                                               fsr_hook, fsrcnnx_hook_8, fsrcnnx_hook_16);
        }
    }
    const struct pl_hook *active_hooks[] = {fsrcnnx_hook};
    if (fsrcnnx_hook) {
        params.hooks = active_hooks;
        params.num_hooks = 1;
        switch (video_preset) {
        case VideoPreset::HighQualitySpatial:
            if (fsrcnnx_hook == fsr_hook)
                qCDebug(chiakiGui) << "Activating spatial upscaler: FSR (small upscale fallback)";
            else
                qCDebug(chiakiGui) << "Activating spatial upscaler: FSRCNNX x2 8-0-4-1";
            break;
        case VideoPreset::HighQualityAdvancedSpatial:
            if (fsrcnnx_hook == fsr_hook)
                qCDebug(chiakiGui) << "Activating spatial upscaler: FSR (small upscale fallback)";
            else
                qCDebug(chiakiGui) << "Activating spatial upscaler: FSRCNNX x2 16-0-4-1";
            break;
        case VideoPreset::Custom:
            if (fsrcnnx_hook == fsr_hook) {
                qCDebug(chiakiGui) << "Activating custom upscaler: FSR";
            } else {
                switch (configured_upscaler) {
                case PlaceboUpscaler::FSRCNNX8:
                qCDebug(chiakiGui) << "Activating custom upscaler: FSRCNNX x2 8-0-4-1";
                break;
                case PlaceboUpscaler::FSRCNNX16:
                qCDebug(chiakiGui) << "Activating custom upscaler: FSRCNNX x2 16-0-4-1";
                break;
                default:
                    break;
                }
            }
            break;
        default:
            break;
        }
    }

    for (int i = 0; i < frame_mix.num_frames; i++) {
        const struct pl_frame *mix_frame = frame_mix.frames[i];
        if (!mix_frame || mix_frame->num_planes <= 0) {
            qCWarning(chiakiGui) << "Frame mix contains an invalid frame, skipping render";
            if (placebo_reset_pending.loadAcquire() == 0)
                schedulePlaceboReset();
            close_started_frame(false);
            finalize_render();
            schedulePostSwapchainRecreateRefresh();
            return;
        }
    }

    const qint64 render_setup_ready_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (render_setup_ready_us >= render_setup_config_after_us) {
        const auto render_schedule_state = snapshotRenderScheduleState();
        logRenderPhaseStall(render_setup_ready_us,
                            "render_setup_ready",
                            render_setup_ready_us - render_setup_config_after_us,
                            snapshotQueueDepth(),
                            depth_limit,
                            pending_frame_waiting,
                            render_active.loadAcquire() != 0,
                            render_schedule_state.first,
                            render_schedule_state.second);
    }

    if (frame_mix.num_frames > 0 && !has_video && loading_transition_complete.loadAcquire() == 0 &&
        startup_video_visible_pending.loadAcquire() == 0) {
        startup_video_visible_pending.storeRelease(1);
    }

    const qint64 render_call_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
    if (render_entry_us_local > 0 && render_call_begin_us >= render_entry_us_local)
        logLatencyStats("render_prep", render_call_begin_us - render_entry_us_local);
    if (render_call_begin_us >= render_setup_begin_us)
        logRenderSetupStats(render_call_begin_us, render_call_begin_us - render_setup_begin_us);
    if (!(bypass_frame_queue
        ? pl_render_image(placebo_renderer, &direct_render_frame, &target_frame, &params)
        : pl_render_image_mix(placebo_renderer, &frame_mix, &target_frame, &params)))
    {
        qCWarning(chiakiGui) << "Failed to render Placebo frame!";
        close_started_frame(false);
        finalize_render();
        schedulePostSwapchainRecreateRefresh();
        return;
    }
    {
        const qint64 render_after_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (render_after_us >= render_call_begin_us) {
            logLatencyStats("render_image", render_after_us - render_call_begin_us);
            if (render_entry_us_local > 0 && render_after_us >= render_entry_us_local)
                logLatencyStats("render_work", render_after_us - render_entry_us_local);
        }
    }
    close_started_frame(true);

    if (startup_video_visible_refresh_pending.loadAcquire() != 0) {
        loading_transition_complete.storeRelease(1);
        emit loadingTransitionCompleteChanged();
        startup_video_visible_refresh_pending.storeRelease(0);
    }

    bool scheduled_pending = finalize_render();
    if (!scheduled_pending)
        schedulePostRenderWork();
    schedulePostSwapchainRecreateRefresh();

}

bool QmlMainWindow::handleShortcut(QKeyEvent *event)
{
    if (event->modifiers() == Qt::NoModifier) {
        switch (event->key()) {
        case Qt::Key_F11:
            if (windowState() != Qt::WindowFullScreen)
                fullscreenTime();
            else
                normalTime();
            return true;
        default:
            break;
        }
    }

    if (!event->modifiers().testFlag(Qt::ControlModifier))
        return false;

    switch (event->key()) {
    case Qt::Key_S:
        if (has_video)
            setVideoMode(videoMode() == VideoMode::Stretch ? VideoMode::Normal : VideoMode::Stretch);
        return true;
    case Qt::Key_Z:
        if (has_video)
            setVideoMode(videoMode() == VideoMode::Zoom ? VideoMode::Normal : VideoMode::Zoom);
        return true;
    case Qt::Key_M:
        if (session)
            session->ToggleMute();
        return true;
    case Qt::Key_O:
        emit menuRequested();
        return true;
    case Qt::Key_Q:
#ifndef Q_OS_MACOS
        close();
#endif
        return true;
    default:
        return false;
    }
}

bool QmlMainWindow::amdCard() const
{
    return amd_card;
}

bool QmlMainWindow::nvidiaCard() const
{
    return nvidia_card;
}

bool QmlMainWindow::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
        if (static_cast<QMouseEvent*>(event)->source() != Qt::MouseEventNotSynthesized)
            return true;
        if (session && !grab_input) {
            if (event->type() == QEvent::MouseMove)
                session->HandleMouseMoveEvent(static_cast<QMouseEvent*>(event), width(), height());
            else if (event->type() == QEvent::MouseButtonPress)
                session->HandleMousePressEvent(static_cast<QMouseEvent*>(event));
            else if (event->type() == QEvent::MouseButtonRelease)
                session->HandleMouseReleaseEvent(static_cast<QMouseEvent*>(event));
            return true;
        }
        QGuiApplication::sendEvent(quick_window, event);
        break;
    case QEvent::MouseButtonDblClick:
        if(!settings->GetFullscreenDoubleClickEnabled())
            break;
        if (session && !grab_input) {
            if (windowState() != Qt::WindowFullScreen)
                fullscreenTime();
            else
                normalTime();
        }
        break;
    case QEvent::KeyPress:
        if (handleShortcut(static_cast<QKeyEvent*>(event)))
            return true;
    case QEvent::KeyRelease:
        if (session && !grab_input) {
            QKeyEvent *e = static_cast<QKeyEvent*>(event);
            if (!e->spontaneous()) {
                QGuiApplication::sendEvent(quick_window, event);
                return true;
            }
            if (e->timestamp())
                session->HandleKeyboardEvent(e);
            return true;
        }
        QGuiApplication::sendEvent(quick_window, event);
        break;
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
        if (session && !grab_input) {
            session->HandleTouchEvent(static_cast<QTouchEvent*>(event), width(), height());
            return true;
        }
        QGuiApplication::sendEvent(quick_window, event);
        break;
    case QEvent::Close:
        if (!backend->closeRequested()) {
            event->ignore();
            return true;
        }
        if (quick_render->thread() == QThread::currentThread())
            destroySwapchain();
        else
            QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::destroySwapchain, this), Qt::BlockingQueuedConnection);
        break;
    default:
        break;
    }

    bool ret = QWindow::event(event);

    switch (event->type()) {
    case QEvent::Expose:
        if (isExposed())
            updateSwapchain();
        else
            if (quick_render->thread() == QThread::currentThread())
                destroySwapchain();
            else
                QMetaObject::invokeMethod(quick_render, std::bind(&QmlMainWindow::destroySwapchain, this), Qt::BlockingQueuedConnection);
        break;
    case QEvent::Move:
        if(!session && isWindowAdjustable())
            settings->SetGeometry(geometry());
        else if(session && settings->GetWindowType() == WindowType::AdjustableResolution && isStreamWindowAdjustable())
            settings->SetStreamGeometry(geometry());
        updateStatsOverlayGeometry();
        break;
    case QEvent::Resize:
        if(!session && isWindowAdjustable())
            settings->SetGeometry(geometry());
        else if(session && settings->GetWindowType() == WindowType::AdjustableResolution && isStreamWindowAdjustable())
            settings->SetStreamGeometry(geometry());
        if (isExposed())
            updateSwapchain();
        updateStatsOverlayGeometry();
        break;
    default:
        break;
    }

    return ret;
}

QObject *QmlMainWindow::focusObject() const
{
    return quick_window->focusObject();
}
