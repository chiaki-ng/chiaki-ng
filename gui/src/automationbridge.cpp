// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "automationbridge.h"
#include "automationframes.h"
#include "automationosd.h"

#include <QAbstractSocket>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <algorithm>

#if !defined(Q_OS_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

static constexpr qint64 AUTOMATION_MAX_MESSAGE_SIZE = 64 * 1024;
static constexpr qint64 AUTOMATION_MAX_QUEUED_BYTES = 1024 * 1024;
static constexpr int AUTOMATION_MAX_CLIENTS = 16;
static constexpr int AUTOMATION_COMMANDS_PER_TURN = 32;

struct AutomationButtonMapping
{
	const char *name;
	uint32_t bit;
};

static const AutomationButtonMapping automation_button_mappings[] = {
	{ "CROSS", CHIAKI_CONTROLLER_BUTTON_CROSS },
	{ "MOON", CHIAKI_CONTROLLER_BUTTON_MOON },
	{ "BOX", CHIAKI_CONTROLLER_BUTTON_BOX },
	{ "PYRAMID", CHIAKI_CONTROLLER_BUTTON_PYRAMID },
	{ "DPAD_LEFT", CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT },
	{ "DPAD_RIGHT", CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT },
	{ "DPAD_UP", CHIAKI_CONTROLLER_BUTTON_DPAD_UP },
	{ "DPAD_DOWN", CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN },
	{ "L1", CHIAKI_CONTROLLER_BUTTON_L1 },
	{ "R1", CHIAKI_CONTROLLER_BUTTON_R1 },
	{ "L3", CHIAKI_CONTROLLER_BUTTON_L3 },
	{ "R3", CHIAKI_CONTROLLER_BUTTON_R3 },
	{ "OPTIONS", CHIAKI_CONTROLLER_BUTTON_OPTIONS },
	{ "SHARE", CHIAKI_CONTROLLER_BUTTON_SHARE },
	{ "TOUCHPAD", CHIAKI_CONTROLLER_BUTTON_TOUCHPAD },
	{ "PS", CHIAKI_CONTROLLER_BUTTON_PS },
};

AutomationBridge::AutomationBridge(AutomationFrames *frames, QObject *parent)
	: QObject(parent)
	, frames(frames)
	, osd(new AutomationOsd(this))
{
	if(frames)
		connect(frames, &QObject::destroyed, this, [this]() { this->frames = nullptr; });
}

AutomationBridge::~AutomationBridge()
{
	Stop();
}

bool AutomationBridge::Start(const QString &socket_path)
{
	if(server)
		return server->isListening();

	const QString path = socket_path.isEmpty() ? DefaultSocketPath() : socket_path;
#if !defined(Q_OS_WIN32)
	const QString dir_path = QFileInfo(path).absolutePath();
	if(!QDir().mkpath(dir_path))
	{
		qWarning() << "automation bridge: failed to create socket directory" << dir_path;
		return false;
	}
	if(qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR"))
	{
		// /tmp is shared: only trust a fallback directory owned by this user
		if(QFileInfo(dir_path).ownerId() != geteuid())
		{
			qWarning() << "automation bridge: refusing socket directory owned by another user" << dir_path;
			return false;
		}
		if(!QFile::setPermissions(dir_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
		{
			qWarning() << "automation bridge: failed to restrict permissions on" << dir_path;
			return false;
		}
	}
	else
		QFile::setPermissions(dir_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif

	server = new QLocalServer(this);
	server->setSocketOptions(QLocalServer::UserAccessOption);
	connect(server, &QLocalServer::newConnection, this, &AutomationBridge::HandleNewConnection);
#if !defined(Q_OS_WIN32)
	// Serialize startup between instances; the kernel releases the lock on
	// process death, so a crashed instance never leaves the lock held.
	const QString lock_path = dir_path + QStringLiteral("/automation.lock");
	lock_fd = ::open(lock_path.toUtf8().constData(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if(lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0)
	{
		qWarning() << "automation bridge: another live instance holds" << lock_path;
		if(lock_fd >= 0)
		{
			::close(lock_fd);
			lock_fd = -1;
		}
		server->deleteLater();
		server = nullptr;
		return false;
	}
	// Holding the lock means no live instance owns the path; any file left is stale.
	QLocalServer::removeServer(path);
#else
	// No flock on this path; probe for a live instance before taking the name.
	QLocalSocket probe;
	probe.connectToServer(path);
	if(probe.waitForConnected(200))
	{
		probe.disconnectFromServer();
		qWarning() << "automation bridge:" << path << "is already served by a live instance, not listening";
		server->deleteLater();
		server = nullptr;
		return false;
	}
#endif
	if(!server->listen(path))
	{
		qWarning() << "automation bridge: failed to listen on" << path << server->errorString();
#if !defined(Q_OS_WIN32)
		::close(lock_fd);
		lock_fd = -1;
#endif
		server->deleteLater();
		server = nullptr;
		return false;
	}
	listen_path = path;
	qInfo() << "automation bridge: listening on" << path;
	return true;
}

void AutomationBridge::Stop()
{
	osd->Clear();
	bool had_active_controller = false;
	for(auto it = clients.begin(); it != clients.end(); ++it)
	{
		had_active_controller = had_active_controller || it->controller_active;
		QLocalSocket *client = it.key();
		disconnect(client, nullptr, this, nullptr);
		client->abort();
		client->deleteLater();
	}
	clients.clear();
	frame_subscriber = nullptr;
	control_client = nullptr;
	if(frames)
		frames->Unsubscribe();
	if(server)
	{
		server->close();
		if(!listen_path.isEmpty())
			QLocalServer::removeServer(listen_path);
		server->deleteLater();
		server = nullptr;
		listen_path.clear();
	}
#if !defined(Q_OS_WIN32)
	if(lock_fd >= 0)
	{
		::close(lock_fd);
		lock_fd = -1;
	}
#endif
	if(had_active_controller)
		EmitIdleState();
}

bool AutomationBridge::IsRunning() const
{
	return server && server->isListening();
}

QString AutomationBridge::DefaultSocketPath()
{
#if defined(Q_OS_WIN32)
	// QLocalServer maps a plain name to \\.\pipe\<name> on Windows
	return QStringLiteral("chiaki-ng-automation");
#else
	// Read XDG_RUNTIME_DIR directly: QStandardPaths synthesizes
	// /tmp/runtime-$USER when it is unset, but the protocol specifies the
	// /tmp/chiaki-ng-$USER fallback that clients implement.
	const QString runtime_dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
	if(!runtime_dir.isEmpty())
		return runtime_dir + QStringLiteral("/chiaki-ng/automation.sock");
	QString user = qEnvironmentVariable("USER");
	if(user.isEmpty())
		user = QStringLiteral("unknown");
	return QStringLiteral("/tmp/chiaki-ng-") + user + QStringLiteral("/automation.sock");
#endif
}

void AutomationBridge::SetOsdLines(const QStringList &lines)
{
	if(osd_lines == lines)
		return;
	osd_lines = lines;
	emit osdLinesChanged();
}

void AutomationBridge::SetOsdMarkers(const QVariantList &markers)
{
	if(osd_markers == markers)
		return;
	osd_markers = markers;
	emit osdMarkersChanged();
}

void AutomationBridge::NotifyFrame(uint64_t frame_index, uint32_t slot, uint32_t width, uint32_t height, double pts, uint64_t token)
{
	// Keep at most one advisory frame event queued on the GUI thread.
	if(frame_event_pending.exchange(true, std::memory_order_acq_rel))
		return;
	QMetaObject::invokeMethod(this, [this, token, frame_index, slot, width, height, pts]() {
		frame_event_pending.store(false, std::memory_order_release);
		if(!frame_subscriber || !frames || token != frames->CurrentPublishToken())
			return;
		SendJson(frame_subscriber, {
			{ QStringLiteral("event"), QStringLiteral("frame") },
			{ QStringLiteral("index"), static_cast<qint64>(frame_index) },
			{ QStringLiteral("slot"), static_cast<qint64>(slot) },
			{ QStringLiteral("width"), static_cast<qint64>(width) },
			{ QStringLiteral("height"), static_cast<qint64>(height) },
			{ QStringLiteral("pts"), pts },
		});
	}, Qt::QueuedConnection);
}

void AutomationBridge::HandleNewConnection()
{
	while(QLocalSocket *client = server->nextPendingConnection())
	{
		if(clients.size() >= AUTOMATION_MAX_CLIENTS)
		{
			client->abort();
			client->deleteLater();
			continue;
		}
		client->setReadBufferSize(AUTOMATION_MAX_MESSAGE_SIZE + 1);
		connect(client, &QLocalSocket::readyRead, this, [this, client]() { HandleClientReadyRead(client); });
		connect(client, &QLocalSocket::disconnected, this, [this, client]() { HandleClientDisconnected(client); });
		AutomationBridgeClient state;
		state.controller_timer = new QTimer(client);
		state.controller_timer->setSingleShot(true);
		connect(state.controller_timer, &QTimer::timeout, this, [this, client]() {
			auto it = clients.find(client);
			if(it == clients.end() || !it->controller_active)
				return;
			it->controller_active = false;
			EmitIdleState();
		});
		clients.insert(client, state);
		if(client->bytesAvailable())
			HandleClientReadyRead(client);
	}
}

void AutomationBridge::HandleClientReadyRead(QLocalSocket *client)
{
	auto it = clients.find(client);
	if(it == clients.end() || it->read_scheduled)
		return;
	it->read_scheduled = true;
	QPointer<QLocalSocket> guard(client);
	QTimer::singleShot(0, this, [this, guard]() {
		if(!guard)
			return;
		auto it = clients.find(guard);
		if(it == clients.end())
			return;
		it->read_scheduled = false;
		ProcessClientMessages(guard);
	});
}

void AutomationBridge::ProcessClientMessages(QLocalSocket *client)
{
	for(int processed = 0; processed < AUTOMATION_COMMANDS_PER_TURN; processed++)
	{
		// Sending a reply can abort the socket and erase its client state.
		auto it = clients.find(client);
		if(it == clients.end() || client->state() != QLocalSocket::ConnectedState)
			return;
		QByteArray &buffer = it->buffer;
		buffer += client->read(AUTOMATION_MAX_MESSAGE_SIZE + 1 - buffer.size());
		const qsizetype newline = buffer.indexOf('\n');
		if(newline < 0)
		{
			if(buffer.size() > AUTOMATION_MAX_MESSAGE_SIZE)
				client->abort();
			return;
		}
		const QByteArray line = buffer.left(newline);
		buffer.remove(0, newline + 1);
		if(line.trimmed().isEmpty())
			continue;
		QJsonParseError parse_error;
		const QJsonDocument doc = QJsonDocument::fromJson(line, &parse_error);
		if(parse_error.error != QJsonParseError::NoError || !doc.isObject())
		{
			SendError(client, QString(), QStringLiteral("invalid JSON object"));
			continue;
		}
		ProcessMessage(client, doc.object());
	}
	const auto it = clients.constFind(client);
	if(it != clients.constEnd() && (it->buffer.contains('\n') || client->bytesAvailable()))
		HandleClientReadyRead(client);
}

void AutomationBridge::HandleClientDisconnected(QLocalSocket *client)
{
	const auto it = clients.find(client);
	bool was_active = false;
	if(it != clients.end())
	{
		was_active = it->controller_active;
		it->controller_timer->stop();
		clients.erase(it);
	}
	if(control_client == client)
	{
		control_client = nullptr;
		osd->Clear();
	}
	if(frame_subscriber == client)
	{
		frame_subscriber = nullptr;
		if(frames)
			frames->Unsubscribe();
	}
	client->deleteLater();
	if(was_active)
		EmitIdleState();
}

void AutomationBridge::ProcessMessage(QLocalSocket *client, const QJsonObject &msg)
{
	const QString cmd = msg.value(QStringLiteral("cmd")).toString();
	if(cmd.isEmpty())
	{
		SendError(client, QString(), QStringLiteral("missing cmd field"));
		return;
	}

	if(cmd == QLatin1String("set_controller") || cmd == QLatin1String("controller_idle")
		|| cmd == QLatin1String("osd_image") || cmd == QLatin1String("osd_open")
		|| cmd == QLatin1String("osd_text") || cmd == QLatin1String("osd_markers") || cmd == QLatin1String("osd_clear"))
	{
		if(control_client && control_client != client)
		{
			SendError(client, cmd, QStringLiteral("another control client is active"));
			return;
		}
		control_client = client;
	}

	if(cmd == QLatin1String("hello"))
	{
		SendJson(client, {
			{ QStringLiteral("event"), QStringLiteral("hello") },
			{ QStringLiteral("version"), 1 },
			{ QStringLiteral("session"), session_state },
			{ QStringLiteral("input_blocked"), input_blocked },
		});
	}
	else if(cmd == QLatin1String("ping"))
	{
		SendJson(client, { { QStringLiteral("event"), QStringLiteral("pong") } });
	}
	else if(cmd == QLatin1String("set_controller"))
		CmdSetController(client, msg, cmd);
	else if(cmd == QLatin1String("controller_idle"))
	{
		auto it = clients.find(client);
		if(it != clients.end())
		{
			it->controller_active = false;
			it->controller_timer->stop();
		}
		EmitIdleState();
		SendOk(client, cmd);
	}
	else if(cmd == QLatin1String("subscribe_frames"))
		CmdSubscribeFrames(client, msg, cmd);
	else if(cmd == QLatin1String("unsubscribe_frames"))
		CmdUnsubscribeFrames(client, cmd);
	else if(cmd == QLatin1String("osd_text"))
		CmdOsdText(client, msg, cmd);
	else if(cmd == QLatin1String("osd_markers"))
		CmdOsdMarkers(client, msg, cmd);
	else if(cmd == QLatin1String("osd_image") || cmd == QLatin1String("osd_open"))
	{
		const QPointer<QLocalSocket> recipient(client);
		osd->Command(msg, listen_path, [this, recipient, cmd](QJsonObject result) {
			result.insert(QStringLiteral("cmd"), cmd);
			if(recipient && clients.contains(recipient))
				SendJson(recipient, result);
		});
	}
	else if(cmd == QLatin1String("osd_stats"))
		SendJson(client, {{"event", "ok"}, {"cmd", cmd}, {"supported", osd->supported.load()},
			{"accepted", double(osd->accepted.load())}, {"replaced", double(osd->replaced.load())},
			{"displayed", double(osd->displayed.load())}});
	else if(cmd == QLatin1String("osd_clear"))
	{
		osd->Clear();
		SetOsdLines(QStringList());
		SetOsdMarkers(QVariantList());
		SendOk(client, cmd);
	}
	else
		SendError(client, cmd, QStringLiteral("unknown command"));
}

void AutomationBridge::CmdSetController(QLocalSocket *client, const QJsonObject &msg, const QString &cmd)
{
	if(session_state == QStringLiteral("idle"))
	{
		SendError(client, cmd, QStringLiteral("no active session"));
		return;
	}

	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);

	const QJsonValue buttons_value = msg.value(QStringLiteral("buttons"));
	if(!buttons_value.isUndefined())
	{
		if(!buttons_value.isArray())
		{
			SendError(client, cmd, QStringLiteral("buttons must be an array of button names"));
			return;
		}
		const QJsonArray buttons = buttons_value.toArray();
		for(const QJsonValue &button : buttons)
		{
			if(!button.isString())
			{
				SendError(client, cmd, QStringLiteral("buttons must be an array of button names"));
				return;
			}
			uint32_t bit = 0;
			for(const AutomationButtonMapping &mapping : automation_button_mappings)
			{
				if(button.toString() == QLatin1String(mapping.name))
				{
					bit = mapping.bit;
					break;
				}
			}
			if(!bit)
			{
				SendError(client, cmd, QStringLiteral("unknown button: %1").arg(button.toString()));
				return;
			}
			state.buttons |= bit;
		}
	}

	auto parse_axis = [this, client, cmd, &msg](const char *key, int16_t &target) {
		const QJsonValue value = msg.value(QLatin1String(key));
		if(value.isUndefined())
			return true;
		if(!value.isDouble())
		{
			SendError(client, cmd, QStringLiteral("%1 must be a number").arg(QLatin1String(key)));
			return false;
		}
		target = static_cast<int16_t>(std::clamp(value.toDouble(), -32767.0, 32767.0));
		return true;
	};
	if(!parse_axis("lx", state.left_x) || !parse_axis("ly", state.left_y) || !parse_axis("rx", state.right_x) || !parse_axis("ry", state.right_y))
		return;

	auto parse_trigger = [this, client, cmd, &msg](const char *key, uint8_t &target) {
		const QJsonValue value = msg.value(QLatin1String(key));
		if(value.isUndefined())
			return true;
		if(!value.isDouble())
		{
			SendError(client, cmd, QStringLiteral("%1 must be a number").arg(QLatin1String(key)));
			return false;
		}
		target = static_cast<uint8_t>(std::clamp(value.toDouble(), 0.0, 255.0));
		return true;
	};
	if(!parse_trigger("l2", state.l2_state) || !parse_trigger("r2", state.r2_state))
		return;

	qint64 hold_ms = 0;
	const QJsonValue hold_value = msg.value(QStringLiteral("hold_ms"));
	if(!hold_value.isUndefined())
	{
		if(!hold_value.isDouble())
		{
			SendError(client, cmd, QStringLiteral("hold_ms must be a number"));
			return;
		}
		const double requested_hold_ms = hold_value.toDouble();
		if(requested_hold_ms > 0)
			hold_ms = static_cast<qint64>(std::clamp(requested_hold_ms, 1.0, 60000.0));
	}

	auto it = clients.find(client);
	if(it == clients.end())
		return;
	it->controller_timer->stop();

	ChiakiControllerState idle;
	chiaki_controller_state_set_idle(&idle);
	it->controller_active = !chiaki_controller_state_equals(&state, &idle);
	if(hold_ms > 0 && it->controller_active)
		it->controller_timer->start(static_cast<int>(hold_ms));

	emit controllerStateRequested(state);

	SendOk(client, cmd);
}

void AutomationBridge::CmdSubscribeFrames(QLocalSocket *client, const QJsonObject &msg, const QString &cmd)
{
	if(frame_subscriber)
	{
		SendError(client, cmd, frame_subscriber == client
			? QStringLiteral("frames already subscribed")
			: QStringLiteral("frames already subscribed by another client"));
		return;
	}
	if(!frames)
	{
		SendError(client, cmd, QStringLiteral("frame channel unavailable"));
		return;
	}
	if(!session_frames_supported)
	{
		SendError(client, cmd, QStringLiteral("frames are unavailable with the pi decoder, switch to the ffmpeg decoder in settings"));
		return;
	}

	const QJsonValue format_value = msg.value(QStringLiteral("format"));
	if(!format_value.isUndefined() && !format_value.isString())
	{
		SendError(client, cmd, QStringLiteral("format must be a string"));
		return;
	}
	QString format = format_value.toString(QStringLiteral("nv12"));
	if(format.isEmpty())
		format = QStringLiteral("nv12");
	const QJsonValue max_fps_value = msg.value(QStringLiteral("max_fps"));
	if(!max_fps_value.isUndefined() && !max_fps_value.isDouble())
	{
		SendError(client, cmd, QStringLiteral("max_fps must be a number"));
		return;
	}
	const double max_fps_number = max_fps_value.toDouble(0);
	const uint32_t max_fps = max_fps_number <= 0 ? 0 : static_cast<uint32_t>(std::clamp(max_fps_number, 1.0, 60.0));

	QString error;
	if(!frames->Subscribe(format, max_fps, &error))
	{
		SendError(client, cmd, error.isEmpty() ? QStringLiteral("failed to subscribe frames") : error);
		return;
	}
	frame_subscriber = client;

	const AutomationFramesShmInfo info = frames->GetShmInfo();
	SendJson(client, {
		{ QStringLiteral("event"), QStringLiteral("frames") },
		{ QStringLiteral("shm"), info.path },
		{ QStringLiteral("slots"), static_cast<qint64>(info.slot_count) },
		{ QStringLiteral("slot_size"), static_cast<qint64>(info.slot_size) },
		{ QStringLiteral("width"), static_cast<qint64>(info.width) },
		{ QStringLiteral("height"), static_cast<qint64>(info.height) },
		{ QStringLiteral("format"), info.format },
	});
}

void AutomationBridge::CmdUnsubscribeFrames(QLocalSocket *client, const QString &cmd)
{
	if(frame_subscriber && frame_subscriber != client)
	{
		SendError(client, cmd, QStringLiteral("frames subscribed by another client"));
		return;
	}
	if(frame_subscriber == client)
	{
		frame_subscriber = nullptr;
		if(frames)
			frames->Unsubscribe();
	}
	SendOk(client, cmd);
}

void AutomationBridge::CmdOsdText(QLocalSocket *client, const QJsonObject &msg, const QString &cmd)
{
	const QJsonValue lines_value = msg.value(QStringLiteral("lines"));
	if(!lines_value.isArray())
	{
		SendError(client, cmd, QStringLiteral("lines must be an array of strings"));
		return;
	}
	QStringList lines;
	const QJsonArray array = lines_value.toArray();
	for(const QJsonValue &line : array)
	{
		if(!line.isString())
		{
			SendError(client, cmd, QStringLiteral("lines must be an array of strings"));
			return;
		}
		lines.append(line.toString());
	}
	SetOsdLines(lines);
	SendOk(client, cmd);
}

void AutomationBridge::CmdOsdMarkers(QLocalSocket *client, const QJsonObject &msg, const QString &cmd)
{
	const QJsonValue rects_value = msg.value(QStringLiteral("rects"));
	if(!rects_value.isArray())
	{
		SendError(client, cmd, QStringLiteral("rects must be an array of objects"));
		return;
	}
	QVariantList markers;
	const QJsonArray array = rects_value.toArray();
	for(const QJsonValue &rect_value : array)
	{
		if(!rect_value.isObject())
		{
			SendError(client, cmd, QStringLiteral("rects must be an array of objects"));
			return;
		}
		const QJsonObject rect = rect_value.toObject();
		QVariantMap marker;
		for(const char *key : { "x", "y", "w", "h" })
		{
			const QJsonValue coord = rect.value(QLatin1String(key));
			if(!coord.isDouble())
			{
				SendError(client, cmd, QStringLiteral("rect x/y/w/h must be numbers"));
				return;
			}
			marker.insert(QLatin1String(key), std::clamp(coord.toDouble(), 0.0, 1.0));
		}
		marker[QStringLiteral("w")] = std::min(marker.value(QStringLiteral("w")).toDouble(), 1.0 - marker.value(QStringLiteral("x")).toDouble());
		marker[QStringLiteral("h")] = std::min(marker.value(QStringLiteral("h")).toDouble(), 1.0 - marker.value(QStringLiteral("y")).toDouble());
		const QJsonValue alpha = rect.value(QStringLiteral("alpha"));
		if(!alpha.isUndefined() && !alpha.isDouble())
		{
			SendError(client, cmd, QStringLiteral("rect alpha must be a number"));
			return;
		}
		marker.insert(QStringLiteral("alpha"), std::clamp(alpha.toDouble(0.2), 0.0, 1.0));
		const QJsonValue color = rect.value(QStringLiteral("color"));
		if(color.isUndefined())
			marker.insert(QStringLiteral("color"), QStringLiteral("#00ff00"));
		else if(color.isString())
			marker.insert(QStringLiteral("color"), color.toString());
		else
		{
			SendError(client, cmd, QStringLiteral("rect color must be a string"));
			return;
		}
		const QJsonValue label = rect.value(QStringLiteral("label"));
		if(!label.isUndefined())
		{
			if(!label.isString())
			{
				SendError(client, cmd, QStringLiteral("rect label must be a string"));
				return;
			}
			marker.insert(QStringLiteral("label"), label.toString());
		}
		markers.append(marker);
	}
	SetOsdMarkers(markers);
	SendOk(client, cmd);
}

void AutomationBridge::StopFrames(const QString &message)
{
	if(!frame_subscriber)
		return;
	QLocalSocket *client = frame_subscriber;
	frame_subscriber = nullptr;
	if(frames)
		frames->Unsubscribe();
	SendJson(client, {
		{ QStringLiteral("event"), QStringLiteral("frames_stopped") },
		{ QStringLiteral("message"), message },
	});
}

void AutomationBridge::SetSessionFramesSupported(bool supported)
{
	session_frames_supported = supported;
	if(!supported)
		StopFrames(QStringLiteral("frames are unavailable with the pi decoder, switch to the ffmpeg decoder in settings"));
}

void AutomationBridge::SetSessionState(const QString &state)
{
	if(state == QStringLiteral("idle"))
		session_frames_supported = true;
	if(session_state == state)
		return;
	session_state = state;
	if(state == QStringLiteral("idle"))
	{
		for(auto it = clients.begin(); it != clients.end(); ++it)
		{
			it->controller_timer->stop();
			it->controller_active = false;
		}
		EmitIdleState();
		StopFrames(QStringLiteral("session ended"));
		osd->Clear();
	}
	const QJsonObject msg = {
		{ QStringLiteral("event"), QStringLiteral("session") },
		{ QStringLiteral("state"), session_state },
	};
	const auto recipients = clients.keys();
	for(QLocalSocket *client : recipients)
		SendJson(client, msg);
	if(session_state == QStringLiteral("idle"))
		NotifyInputBlocked(false);
}

void AutomationBridge::NotifyInputBlocked(bool blocked)
{
	if(input_blocked == blocked)
		return;
	input_blocked = blocked;
	const QJsonObject msg = {
		{ QStringLiteral("event"), QStringLiteral("input_blocked") },
		{ QStringLiteral("blocked"), input_blocked },
	};
	const auto recipients = clients.keys();
	for(QLocalSocket *client : recipients)
		SendJson(client, msg);
}

void AutomationBridge::SendJson(QLocalSocket *client, const QJsonObject &msg)
{
	if(!client || !clients.contains(client) || client->state() != QLocalSocket::ConnectedState)
		return;
	const QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n';
	if(data.size() > AUTOMATION_MAX_QUEUED_BYTES - client->bytesToWrite())
	{
		client->abort();
		return;
	}
	if(client->write(data) != data.size())
		client->abort();
}

void AutomationBridge::SendOk(QLocalSocket *client, const QString &cmd)
{
	SendJson(client, {
		{ QStringLiteral("event"), QStringLiteral("ok") },
		{ QStringLiteral("cmd"), cmd },
	});
}

void AutomationBridge::SendError(QLocalSocket *client, const QString &cmd, const QString &message)
{
	QJsonObject msg{
		{ QStringLiteral("event"), QStringLiteral("error") },
		{ QStringLiteral("message"), message },
	};
	if(!cmd.isEmpty())
		msg.insert(QStringLiteral("cmd"), cmd);
	SendJson(client, msg);
}

void AutomationBridge::EmitIdleState()
{
	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);
	emit controllerStateRequested(state);
}
