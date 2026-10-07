// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <chiaki/controller.h>

class AutomationFrames;

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
	static QString DefaultSocketPath();

	QStringList OsdLines() const { return osd_lines; }
	QVariantList OsdMarkers() const { return osd_markers; }
	void SetOsdLines(const QStringList &lines);
	void SetOsdMarkers(const QVariantList &markers);

	// Called by the AutomationFrames event callback from the frame thread.
	// Implementations must marshal to the GUI thread before touching sockets.
	void NotifyFrame(uint64_t frame_index, uint32_t slot, uint32_t width, uint32_t height, double pts);

signals:
	void osdLinesChanged();
	void osdMarkersChanged();
	void controllerStateRequested(const ChiakiControllerState &state);

private:
	AutomationFrames *frames;
	QStringList osd_lines;
	QVariantList osd_markers;
};
