// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <array>
#include <atomic>
#include <functional>
#include <memory>

extern "C" {
#include <libplacebo/renderer.h>
}

class QLocalServer;
class QLocalSocket;

struct AutomationOsdImage
{
	QString id;
	std::array<float, 4> rect{};
	int width = 0, height = 0;
	QByteArray pixels; // Tightly packed, premultiplied RGBA.
};

struct AutomationOsdScene
{
	uint64_t epoch = 0;
	QList<AutomationOsdImage> images;
};

// Control commands enter on the GUI thread. Parsing and socket I/O run on
// one worker; the renderer only takes immutable snapshots with tryLock().
class AutomationOsd : public QObject
{
	Q_OBJECT
public:
	explicit AutomationOsd(QObject *parent = nullptr);
	~AutomationOsd() override;
	void Command(const QJsonObject &message, const QString &path,
		std::function<void(QJsonObject)> reply);
	void Clear();
	std::shared_ptr<const AutomationOsdScene> Latest();
	std::atomic<uint64_t> epoch{1};
	std::atomic<uint64_t> accepted{0}, replaced{0}, displayed{0};
	std::atomic<bool> supported{false};

signals:
	void cleared();

private:
	QString Apply(const QJsonObject &message, const QByteArray &pixels, uint64_t generation);
	void Read(QLocalSocket *socket, uint64_t generation);
	void Close();
	QThread thread;
	QObject *worker;
	QLocalServer *server = nullptr; // Worker-thread objects below.
	QLocalSocket *socket = nullptr;
	QByteArray input;
	std::shared_ptr<const AutomationOsdScene> scene;
	QMutex mutex;
	std::shared_ptr<const AutomationOsdScene> pending;
	std::atomic<bool> command_pending{false}, clear_pending{false};
	QElapsedTimer rate_timer;
	qint64 rate_bytes = 0;
};

// Fixed GPU storage, allocated outside rendering. An incomplete upload never
// replaces the displayed scene. All GPU calls belong to the render context.
class AutomationOsdRenderer
{
public:
	AutomationOsdRenderer(pl_gpu gpu, AutomationOsd *osd);
	~AutomationOsdRenderer();
	void ResetVideoCrop() { reset_crop = true; }
	const pl_rect2df *VideoCrop(const pl_rect2df *source);
	void Compose(pl_frame &target, const pl_overlay &qml);

private:
	struct Texture
	{
		pl_tex tex = nullptr;
		std::atomic<bool> uploading{false};
		QByteArray pixels;
	};
	void Advance();
	pl_gpu gpu;
	AutomationOsd *osd;
	std::array<Texture, 16> textures;
	std::shared_ptr<const AutomationOsdScene> front, next;
	std::array<int, 8> front_slots{}, next_slots{};
	std::array<pl_overlay_part, 8> parts{};
	std::array<pl_overlay, 9> overlays{};
	std::atomic<bool> reset_crop{false};
	pl_rect2df video_crop{};
	int uploaded = 0;
};
