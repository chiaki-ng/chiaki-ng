// lib/include/chiaki/gyrosteer.h
// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_GYROSTEER_H
#define CHIAKI_GYROSTEER_H

#include "common.h"
#include "orientation.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 把 DS5/DS4 姿态四元数的 roll(左右倾斜)映射为左摇杆 X(绝对模式)。
 * 数值核心,无 SDL/QML 依赖,供 munit 单测。
 */
typedef struct chiaki_gyro_steer_t
{
	bool enabled;
	bool invert;
	float deadzone_deg;   // 死区半宽(°)
	float max_angle_deg;  // 满偏转角(°)
	float rest_roll_deg;  // 回正基准角(内部)
	bool rest_valid;
	float angle_deg;      // 当前识别转角(rest 校准后,供预览)
	float smoothed_left_x;
} ChiakiGyroSteer;

CHIAKI_EXPORT void chiaki_gyro_steer_init(ChiakiGyroSteer *gs,
		bool enabled, bool invert, float deadzone_deg, float max_angle_deg);
CHIAKI_EXPORT void chiaki_gyro_steer_set_rest(ChiakiGyroSteer *gs, const ChiakiOrientation *orient);
CHIAKI_EXPORT void chiaki_gyro_steer_update(ChiakiGyroSteer *gs, const ChiakiOrientation *orient, float dt_sec);
CHIAKI_EXPORT float chiaki_gyro_steer_get_angle_deg(const ChiakiGyroSteer *gs);
CHIAKI_EXPORT float chiaki_gyro_steer_get_left_x(const ChiakiGyroSteer *gs);
CHIAKI_EXPORT bool chiaki_gyro_steer_is_active(const ChiakiGyroSteer *gs);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_GYROSTEER_H
