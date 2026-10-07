// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once

#include <QObject>
#include <QString>
#include <QMutex>
#include <QSet>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>

struct AVFrame;

struct AutomationFramesShmInfo
{
	QString path;
	uint32_t slot_count = 0;
	uint32_t slot_size = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	QString format;
};

// Publishes decoded video frames into a shared-memory ring buffer for
// external automation clients. See docs/diy/automationbridge.md.
// WriteFrame() is called from the frame thread and must never block:
// with no active subscriber or a busy ring it returns immediately.
class AutomationFrames : public QObject
{
	Q_OBJECT

public:
	using EventCallback = std::function<void(uint64_t frame_index, uint32_t slot, uint32_t width, uint32_t height, double pts, uint64_t token)>;

	explicit AutomationFrames(QObject *parent = nullptr);
	~AutomationFrames() override;

	bool Subscribe(const QString &format, uint32_t max_fps, QString *error_out);
	void Unsubscribe();
	bool IsSubscribed() const;
	AutomationFramesShmInfo GetShmInfo() const;
	void SetEventCallback(EventCallback callback);
	uint64_t CurrentPublishToken() const { return publish_token.load(std::memory_order_acquire); }

	void WriteFrame(const AVFrame *frame, double pts);

private:
	void UnsubscribeLocked();

	// Serializes Subscribe/Unsubscribe (GUI thread) against WriteFrame
	// (frame thread, tryLock only; drops the frame when contended).
	QMutex write_mutex;
	std::atomic<bool> subscribed{false};
	std::atomic<uint32_t> current_width{0};
	std::atomic<uint32_t> current_height{0};

#if defined(Q_OS_WIN32)
	void *shm_handle = nullptr;
#else
	int shm_fd = -1;
#endif
	void *shm_base = nullptr;
	size_t shm_size = 0;
	QString shm_name;
	QString shm_path;

	uint32_t max_fps = 0;
	uint64_t frames_published = 0;
	std::atomic<uint64_t> publish_token{0};
	std::chrono::steady_clock::time_point last_write_time;
	bool have_last_write = false;
	EventCallback event_callback;
	QSet<int> warned_formats;
};
