#include "systemdinhibit.h"

#ifdef CHIAKI_HAVE_WAYLAND_IDLE_INHIBIT
#include "waylandidleinhibitor.h"

// How long the display is kept awake after the last controller movement when
// falling back to Wayland idle-inhibit. The compositor's own idle timeout
// starts counting once the inhibitor is released.
#ifndef WAYLAND_IDLE_INHIBIT_HOLD_MS
#define WAYLAND_IDLE_INHIBIT_HOLD_MS (60 * 1000)
#endif
#endif

#ifdef CHIAKI_HAVE_DBUS
#include <fcntl.h>
#include <unistd.h>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusPendingReply>
#include <QDBusPendingCallWatcher>
#include <QDBusUnixFileDescriptor>
#endif

SystemdInhibit::SystemdInhibit(const QString &who, const QString &why, const QString &what, const QString &mode, QObject *parent)
    : QObject(parent)
    , who(who)
    , why(why)
    , what(what)
    , mode(mode)
{
#ifdef CHIAKI_HAVE_DBUS
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
                                         QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"),
                                         QStringLiteral("PrepareForSleep"),
                                         this,
                                         SIGNAL(login1PrepareForSleep(bool)));
#endif
}

void SystemdInhibit::inhibit()
{
#ifdef CHIAKI_HAVE_DBUS
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                                       QStringLiteral("/org/freedesktop/login1"),
                                                       QStringLiteral("org.freedesktop.login1.Manager"),
                                                       QStringLiteral("Inhibit"));
    call << what << who << why << mode;

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
        if (reply.isError()) {
            qWarning() << "Inhibit Error:" << reply.error().name() << reply.error().message();
        } else {
            fd = reply.value().fileDescriptor();
            if (fd == -1)
                qWarning() << "Received invalid fd";
            else
                fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        }
    });
#endif
}

void SystemdInhibit::simulateUserActivity()
{
#ifdef CHIAKI_HAVE_WAYLAND_IDLE_INHIBIT
    if (!screensaver_available)
    {
        if (wayland_idle_inhibitor)
            wayland_idle_inhibitor->poke();
        return;
    }
#endif
#ifdef CHIAKI_HAVE_DBUS
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ScreenSaver"),
                                                       QStringLiteral("/ScreenSaver"),
                                                       QStringLiteral("org.freedesktop.ScreenSaver"),
                                                       QStringLiteral("SimulateUserActivity"));
    call;

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        watcher->deleteLater();
        const QDBusPendingReply<void> reply = *watcher;
        if (!reply.isError())
            return;
#ifdef CHIAKI_HAVE_WAYLAND_IDLE_INHIBIT
        // No org.freedesktop.ScreenSaver (e.g. Hyprland, Sway): fall back to
        // the Wayland idle-inhibit protocol instead of failing on every movement.
        const auto type = reply.error().type();
        if (screensaver_available && (type == QDBusError::ServiceUnknown || type == QDBusError::UnknownObject
                    || type == QDBusError::UnknownInterface || type == QDBusError::UnknownMethod))
        {
            screensaver_available = false;
            qInfo() << "org.freedesktop.ScreenSaver not available (" << reply.error().name() << "), trying Wayland idle-inhibit";
            auto inhibitor = new WaylandIdleInhibitor(WAYLAND_IDLE_INHIBIT_HOLD_MS, this);
            if (inhibitor->isSupported())
            {
                wayland_idle_inhibitor = inhibitor;
                wayland_idle_inhibitor->poke();
            }
            else
                delete inhibitor;
            return;
        }
#endif
        qWarning() << "Simulate User Activity Error:" << reply.error().name() << reply.error().message();
    });
#endif
}

void SystemdInhibit::login1PrepareForSleep(bool start)
{
    if (start)
        emit sleep();
    else
        emit resume();
}

void SystemdInhibit::release()
{
#ifdef CHIAKI_HAVE_DBUS
    if (fd >= 0)
        close(fd);
    fd = -1;
#endif
}
