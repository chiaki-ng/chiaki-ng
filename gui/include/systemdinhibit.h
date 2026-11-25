#pragma once

#include <QObject>

class WaylandIdleInhibitor;

class SystemdInhibit : public QObject
{
    Q_OBJECT

public:
    SystemdInhibit(const QString &who, const QString &why, const QString &what, const QString &mode, QObject *parent = nullptr);

    void inhibit();
    void simulateUserActivity();
    void release();

signals:
    void sleep();
    void resume();

private slots:
    void login1PrepareForSleep(bool start);

private:
    QString who;
    QString why;
    QString what;
    QString mode;
    int fd = -1;
    bool screensaver_available = true;
    WaylandIdleInhibitor *wayland_idle_inhibitor = nullptr;
};
