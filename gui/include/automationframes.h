// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once

#include <QObject>
#include <QString>
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
	using EventCallback = std::function<void(uint64_t frame_index, uint32_t slot, uint32_t width, uint32_t height, double pts)>;

	explicit AutomationFrames(QObject *parent = nullptr);
	~AutomationFrames() override;

	bool Subscribe(const QString &format, uint32_t max_fps, QString *error_out);
	void Unsubscribe();
	bool IsSubscribed() const;
	AutomationFramesShmInfo GetShmInfo() const;
	void SetEventCallback(EventCallback callback);

	void WriteFrame(const AVFrame *frame, double pts);
};
