// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "automationframes.h"

AutomationFrames::AutomationFrames(QObject *parent)
	: QObject(parent)
{
}

AutomationFrames::~AutomationFrames() = default;

bool AutomationFrames::Subscribe(const QString &format, uint32_t max_fps, QString *error_out)
{
	Q_UNUSED(format);
	Q_UNUSED(max_fps);
	if(error_out)
		*error_out = QStringLiteral("frame channel not implemented");
	return false;
}

void AutomationFrames::Unsubscribe()
{
}

bool AutomationFrames::IsSubscribed() const
{
	return false;
}

AutomationFramesShmInfo AutomationFrames::GetShmInfo() const
{
	return AutomationFramesShmInfo{};
}

void AutomationFrames::SetEventCallback(EventCallback callback)
{
	Q_UNUSED(callback);
}

void AutomationFrames::WriteFrame(const AVFrame *frame, double pts)
{
	Q_UNUSED(frame);
	Q_UNUSED(pts);
}
