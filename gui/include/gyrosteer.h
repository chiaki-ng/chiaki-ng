// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_GYROSTEER_BRIDGE_H
#define CHIAKI_GYROSTEER_BRIDGE_H

#include <chiaki/gyrosteer.h>

#include <QObject>
#include <QElapsedTimer>

class Settings;

class GyroSteerBridge : public QObject
{
	Q_OBJECT

	public:
		explicit GyroSteerBridge(QObject *parent = nullptr);

		void ApplyConfig(const Settings *settings);
		void UpdateFromOrientation(const ChiakiOrientation &orient);
		void SetRestPoint();
		void RequestRecenter();

		bool IsActive() const { return chiaki_gyro_steer_is_active(&steer); }
		float GetAngleDeg() const { return chiaki_gyro_steer_get_angle_deg(&steer); }
		float GetLeftX() const { return chiaki_gyro_steer_get_left_x(&steer); }

	signals:
		void StateChanged();

	private:
		ChiakiGyroSteer steer;
		QElapsedTimer elapsed;
		ChiakiOrientation last_orient{};
		bool have_orient = false;
		bool rest_pending = false;
};

#endif // CHIAKI_GYROSTEER_BRIDGE_H
