// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "automationbridge.h"
#include "automationframes.h"

#include <QStandardPaths>

AutomationBridge::AutomationBridge(AutomationFrames *frames, QObject *parent)
	: QObject(parent)
	, frames(frames)
{
}

AutomationBridge::~AutomationBridge() = default;

bool AutomationBridge::Start(const QString &socket_path)
{
	Q_UNUSED(socket_path);
	qWarning() << "automation bridge: socket server not implemented yet";
	return false;
}

void AutomationBridge::Stop()
{
}

bool AutomationBridge::IsRunning() const
{
	return false;
}

QString AutomationBridge::DefaultSocketPath()
{
	QString runtime_dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
	if(runtime_dir.isEmpty())
		runtime_dir = QStringLiteral("/tmp");
	return runtime_dir + QStringLiteral("/chiaki-ng/automation.sock");
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

void AutomationBridge::NotifyFrame(uint64_t frame_index, uint32_t slot, uint32_t width, uint32_t height, double pts)
{
	Q_UNUSED(frame_index);
	Q_UNUSED(slot);
	Q_UNUSED(width);
	Q_UNUSED(height);
	Q_UNUSED(pts);
}
