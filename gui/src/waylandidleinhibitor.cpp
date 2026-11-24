#include "waylandidleinhibitor.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

#include <cstring>

#include <wayland-client.h>
#include "idle-inhibit-unstable-v1-client-protocol.h"

Q_LOGGING_CATEGORY(chiakiIdleInhibit, "chiaki.gui.idleinhibit", QtInfoMsg);

struct WaylandIdleInhibitorRegistryListener
{
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
    {
        auto self = static_cast<WaylandIdleInhibitor *>(data);
        if (!self->manager && strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0)
            self->manager = static_cast<zwp_idle_inhibit_manager_v1 *>(
                wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1));
        (void)version;
    }

    static void global_remove(void *, wl_registry *, uint32_t) {}

    static constexpr wl_registry_listener listener = { global, global_remove };
};

WaylandIdleInhibitor::WaylandIdleInhibitor(int hold_ms, QObject *parent)
    : QObject(parent)
{
    release_timer = new QTimer(this);
    release_timer->setSingleShot(true);
    release_timer->setInterval(hold_ms);
    connect(release_timer, &QTimer::timeout, this, &WaylandIdleInhibitor::release);
}

WaylandIdleInhibitor::~WaylandIdleInhibitor()
{
    release();
    if (manager)
        zwp_idle_inhibit_manager_v1_destroy(manager);
    if (registry)
        wl_registry_destroy(registry);
    if (queue)
        wl_event_queue_destroy(queue);
    if (display)
        wl_display_flush(display);
}

bool WaylandIdleInhibitor::isSupported()
{
    if (!initialized)
    {
        initialized = true;
        supported = init();
    }
    return supported;
}

bool WaylandIdleInhibitor::init()
{
    if (!QGuiApplication::platformName().startsWith("wayland"))
        return false;

    auto native = QGuiApplication::platformNativeInterface();
    if (!native)
        return false;
    display = static_cast<wl_display *>(native->nativeResourceForIntegration("wl_display"));
    if (!display)
        return false;

    // Use a private event queue so we never dispatch Qt's own Wayland events.
    queue = wl_display_create_queue(display);
    auto wrapped_display = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapped_display), queue);
    registry = wl_display_get_registry(wrapped_display);
    wl_proxy_wrapper_destroy(wrapped_display);
    wl_registry_add_listener(registry, &WaylandIdleInhibitorRegistryListener::listener, this);
    wl_display_roundtrip_queue(display, queue);

    if (!manager)
    {
        qCInfo(chiakiIdleInhibit) << "Compositor does not support idle-inhibit-unstable-v1";
        return false;
    }
    qCInfo(chiakiIdleInhibit) << "Using Wayland idle-inhibit to keep the display awake";
    return true;
}

QWindow *WaylandIdleInhibitor::targetWindow()
{
    if (QWindow *w = QGuiApplication::focusWindow())
        return w;
    for (QWindow *w : QGuiApplication::topLevelWindows())
        if (w->isVisible())
            return w;
    return nullptr;
}

void WaylandIdleInhibitor::poke()
{
    if (!isSupported())
        return;

    QWindow *window = targetWindow();
    if (inhibitor && window != inhibited_window)
        release();

    if (!inhibitor && window)
    {
        auto surface = static_cast<wl_surface *>(
            QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window));
        if (!surface)
            return;
        inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(manager, surface);
        inhibited_window = window;
        wl_display_flush(display);
        qCDebug(chiakiIdleInhibit) << "Idle inhibitor created";
    }

    if (inhibitor)
        release_timer->start();
}

void WaylandIdleInhibitor::release()
{
    release_timer->stop();
    if (!inhibitor)
        return;
    zwp_idle_inhibitor_v1_destroy(inhibitor);
    inhibitor = nullptr;
    inhibited_window = nullptr;
    wl_display_flush(display);
    qCDebug(chiakiIdleInhibit) << "Idle inhibitor released";
}
