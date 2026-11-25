#pragma once

#include <QObject>
#include <QPointer>

class QTimer;
class QWindow;
struct wl_display;
struct wl_event_queue;
struct wl_registry;
struct zwp_idle_inhibit_manager_v1;
struct zwp_idle_inhibitor_v1;

/**
 * Keeps the screen awake on Wayland compositors through the
 * idle-inhibit-unstable-v1 protocol.
 *
 * Used as a fallback when org.freedesktop.ScreenSaver is not available
 * (e.g. Hyprland, Sway, niri...). To keep the behavior of
 * SimulateUserActivity (only stay awake while the controller is in use),
 * every poke() (re)arms an inhibitor that is released again after
 * hold_ms without further pokes.
 */
class WaylandIdleInhibitor : public QObject
{
    Q_OBJECT

public:
    explicit WaylandIdleInhibitor(int hold_ms, QObject *parent = nullptr);
    ~WaylandIdleInhibitor() override;

    /// true if running on Wayland and the compositor supports idle-inhibit
    bool isSupported();

    void poke();
    void release();

private:
    bool init();
    static QWindow *targetWindow();

    QTimer *release_timer = nullptr;
    bool initialized = false;
    bool supported = false;

    wl_display *display = nullptr;
    wl_event_queue *queue = nullptr;
    wl_registry *registry = nullptr;
    zwp_idle_inhibit_manager_v1 *manager = nullptr;
    zwp_idle_inhibitor_v1 *inhibitor = nullptr;
    QPointer<QWindow> inhibited_window;

    friend struct WaylandIdleInhibitorRegistryListener;
};
