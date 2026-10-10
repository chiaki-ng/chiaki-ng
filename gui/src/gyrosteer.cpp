// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <gyrosteer.h>

#include <settings.h>

GyroSteerBridge::GyroSteerBridge(QObject *parent)
	: QObject(parent)
{
	chiaki_gyro_steer_init(&steer, false, false, 3.0f, 30.0f);
	elapsed.start();
}

void GyroSteerBridge::ApplyConfig(const Settings *settings)
{
	chiaki_gyro_steer_init(&steer,
		settings->GetGyroSteeringEnabled(),
		settings->GetGyroSteeringInvert(),
		settings->GetGyroSteeringDeadzone(),
		settings->GetGyroSteeringSensitivity());
	// 任何配置变更后都以当前握持为新回正中心
	if(steer.enabled)
		rest_pending = true;
	emit StateChanged();
}

void GyroSteerBridge::UpdateFromOrientation(const ChiakiOrientation &orient)
{
	last_orient = orient;
	have_orient = true;
	if(!steer.enabled)
		return;
	if(rest_pending)
	{
		chiaki_gyro_steer_set_rest(&steer, &orient);
		rest_pending = false;
	}
	float dt = (float)elapsed.restart() / 1000.0f;
	chiaki_gyro_steer_update(&steer, &orient, dt);
	emit StateChanged();
}

void GyroSteerBridge::SetRestPoint()
{
	if(!have_orient)
		return;
	chiaki_gyro_steer_set_rest(&steer, &last_orient);
	rest_pending = false;
	chiaki_gyro_steer_update(&steer, &last_orient, 0.016f);
	emit StateChanged();
}

void GyroSteerBridge::RequestRecenter()
{
	if(have_orient)
	{
		chiaki_gyro_steer_set_rest(&steer, &last_orient);
		rest_pending = false;
	}
	else
		rest_pending = true; // 等下一个传感器事件校准
}
