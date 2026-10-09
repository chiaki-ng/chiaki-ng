// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <chiaki/controller.h>
#include <atomic>

class AutomationFrames;
class AutomationOsd;
class QJsonObject;
class QLocalServer;
class QLocalSocket;
class QTimer;

struct AutomationBridgeClient
{
	QByteArray buffer;
	bool controller_active = false;
	QTimer *controller_timer = nullptr;
	bool read_scheduled = false;
};

// Local IPC bridge for external automation clients (virtual controller
// input, frame subscription, OSD debug overlay).
// See docs/diy/automationbridge.md.
class AutomationBridge : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QStringList osdLines READ OsdLines NOTIFY osdLinesChanged)
	Q_PROPERTY(QVariantList osdMarkers READ OsdMarkers NOTIFY osdMarkersChanged)

public:
	AutomationBridge(AutomationFrames *frames, QObject *parent = nullptr);
	~AutomationBridge() override;

	bool Start(const QString &socket_path = QString());
	void Stop();
	bool IsRunning() const;
	AutomationOsd *Osd() const { return osd; }
	static QString DefaultSocketPath();

	QStringList OsdLines() const { return osd_lines; }
	QVariantList OsdMarkers() const { return osd_markers; }
	void SetOsdLines(const QStringList &lines);
	void SetOsdMarkers(const QVariantList &markers);

	// Called by the AutomationFrames event callback from the frame thread.
	// Implementations must marshal to the GUI thread before touching sockets.
	// token is the publish token captured while the ring lock was held;
	// events from a previous subscription must be discarded.
	void NotifyFrame(uint64_t frame_index, uint32_t slot, uint32_t width, uint32_t height, double pts, uint64_t token);

	void SetSessionState(const QString &state);
	void NotifyInputBlocked(bool blocked);
	// Unsupported decoders cancel existing frame subscriptions as well.
	void SetSessionFramesSupported(bool supported);

signals:
	void osdLinesChanged();
	void osdMarkersChanged();
	void controllerStateRequested(const ChiakiControllerState &state);

private:
	void HandleNewConnection();
	void HandleClientReadyRead(QLocalSocket *client);
	void ProcessClientMessages(QLocalSocket *client);
	void HandleClientDisconnected(QLocalSocket *client);
	void ProcessMessage(QLocalSocket *client, const QJsonObject &msg);
	void CmdSetController(QLocalSocket *client, const QJsonObject &msg, const QString &cmd);
	void CmdSubscribeFrames(QLocalSocket *client, const QJsonObject &msg, const QString &cmd);
	void CmdUnsubscribeFrames(QLocalSocket *client, const QString &cmd);
	void CmdOsdText(QLocalSocket *client, const QJsonObject &msg, const QString &cmd);
	void CmdOsdMarkers(QLocalSocket *client, const QJsonObject &msg, const QString &cmd);
	void SendJson(QLocalSocket *client, const QJsonObject &msg);
	void SendOk(QLocalSocket *client, const QString &cmd);
	void SendError(QLocalSocket *client, const QString &cmd, const QString &message);
	void EmitIdleState();
	void StopFrames(const QString &message);

	AutomationFrames *frames;
	AutomationOsd *osd;
	QLocalServer *server = nullptr;
	QString listen_path;
	QHash<QLocalSocket *, AutomationBridgeClient> clients;
	QLocalSocket *frame_subscriber = nullptr;
	QLocalSocket *control_client = nullptr;
	QStringList osd_lines;
	QVariantList osd_markers;
	QString session_state = QStringLiteral("idle");
	bool input_blocked = false;
	bool session_frames_supported = true;
	std::atomic<bool> frame_event_pending{false};
#if !defined(Q_OS_WIN32)
	int lock_fd = -1;
#endif
};
