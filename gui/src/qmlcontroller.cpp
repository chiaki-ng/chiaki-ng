#include "qmlcontroller.h"

#include <QTimer>
#include <QKeyEvent>
#include <QDateTime>
#include <QHash>
#include <QStyleHints>
#include <QGuiApplication>
#include <QQuickWindow>

static QVector<QPair<uint32_t, Qt::Key>> key_map = {
    { CHIAKI_CONTROLLER_BUTTON_DPAD_UP, Qt::Key_Up },
    { CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN, Qt::Key_Down },
    { CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT, Qt::Key_Left },
    { CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, Qt::Key_Right },
    { CHIAKI_CONTROLLER_BUTTON_CROSS, Qt::Key_Return },
    { CHIAKI_CONTROLLER_BUTTON_MOON, Qt::Key_Escape },
    { CHIAKI_CONTROLLER_BUTTON_BOX, Qt::Key_No },
    { CHIAKI_CONTROLLER_BUTTON_PYRAMID, Qt::Key_Yes },
    { CHIAKI_CONTROLLER_BUTTON_L1, Qt::Key_PageUp },
    { CHIAKI_CONTROLLER_BUTTON_R1, Qt::Key_PageDown },
    { CHIAKI_CONTROLLER_BUTTON_OPTIONS, Qt::Key_Menu },
    { CHIAKI_CONTROLLER_BUTTON_L3, Qt::Key_F1},
    { CHIAKI_CONTROLLER_BUTTON_R3, Qt::Key_F2},
};

QmlController::QmlController(Controller *c, uint32_t shortcut, QObject *t, QObject *parent)
    : QObject(parent)
    , target(t)
    , escape_shortcut(shortcut)
    , controller(c)
{
    repeat_timer = new QTimer(this);
    connect(repeat_timer, &QTimer::timeout, this, [this]() {
        if (!repeat_running++)
            repeat_timer->start(80);
        sendKey(pressed_key);
        if (repeat_running == 50)
            repeat_timer->stop();
    });

    connect(controller, &Controller::StateChanged, this, [this]() {
        auto state = controller->GetState();
        auto buttons = state.buttons;

        if (state.left_x > 30000)
            buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
        else if (state.left_x < -30000)
            buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;

        if (state.left_y > 30000)
            buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
        else if (state.left_y < -30000)
            buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;

        for (auto &k : std::as_const(key_map)) {
            const bool pressed = buttons & k.first;
            const bool old_pressed = old_buttons & k.first;
            if (pressed && !old_pressed) {
                pressed_key = k.second;
                sendKey(pressed_key);
                repeat_running = 0;
                repeat_timer->start(250);
            } else if (old_pressed && !pressed && pressed_key == k.second) {
                repeat_timer->stop();
                repeat_running = 1;
            }
        }

        if ((old_buttons & escape_shortcut) == escape_shortcut && (buttons & escape_shortcut) != escape_shortcut)
            sendKey(Qt::Key_O, Qt::ControlModifier);

        old_buttons = buttons;
    });
}

QmlController::~QmlController()
{
    controller->Unref();
}

bool QmlController::isDualSense() const
{
    return controller->IsDualSense();
}

bool QmlController::isHandheld() const
{
    return controller->IsHandheld();
}

bool QmlController::isPS() const
{
    return controller->IsPS();
}

bool QmlController::isSteamVirtual() const
{
    return controller->IsSteamVirtual();
}

bool QmlController::isDualSenseEdge() const
{
    return controller->IsDualSenseEdge();
}

QString QmlController::GetGUID() const
{
    return controller->GetGUIDString();
}

QString QmlController::GetVIDPID() const
{
    return controller->GetVIDPIDString();
}

void QmlController::sendKey(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    // SDL2-compat/SDL3 on macOS can expose a single physical controller as
    // two devices, creating two QmlController instances that both fire for
    // the same button press. Deduplicate by VID:PID + key as a workaround
    // for the aliased SDL devices, but do not refresh the timestamp on
    // suppressed copies so held-button repeat can continue.
    static QHash<QString, qint64> last_key_time_by_device;
    const QString dedup_key = GetVIDPID() + QLatin1Char(':') + QString::number(static_cast<int>(key));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last_time = last_key_time_by_device.value(dedup_key, 0);
    if ((now - last_time) < 50)
        return;

    last_key_time_by_device.insert(dedup_key, now);

    // With the OpenGL renderer the stream menu and session dialogs are separate top-level
    // QML windows, so navigation keys go to whichever of those has focus. Shortcuts
    // (Ctrl+O toggles the stream menu) are handled by the main window.
    QObject *receiver = target;
    if (modifiers == Qt::NoModifier) {
        if (auto *focus_window = qobject_cast<QQuickWindow *>(QGuiApplication::focusWindow()))
            receiver = focus_window;
    }

    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QGuiApplication::sendEvent(receiver, &press);
    QGuiApplication::sendEvent(receiver, &release);
}
