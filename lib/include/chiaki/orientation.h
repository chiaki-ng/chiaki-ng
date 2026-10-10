// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_ORIENTATION_H
#define CHIAKI_ORIENTATION_H

#include "common.h"
#include "controller.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Quaternion orientation from accelerometer and gyroscope
 * using Madgwick's algorithm.
 * See: http://www.x-io.co.uk/node/8#open_source_ahrs_and_imu_algorithms
 */
typedef struct chiaki_orientation_t
{
	float x, y, z, w;
} ChiakiOrientation;

typedef struct chiaki_accel_new_zero
{
	float accel_x, accel_y, accel_z;
} ChiakiAccelNewZero;

CHIAKI_EXPORT void chiaki_orientation_init(ChiakiOrientation *orient);
CHIAKI_EXPORT void chiaki_orientation_update(ChiakiOrientation *orient,
		float gx, float gy, float gz, float ax, float ay, float az, float beta, float time_step_sec, bool fuzz_enabled);

/**
 * Extension of ChiakiOrientation, also tracking an absolute timestamp and the current gyro/accel state
 */
typedef struct chiaki_orientation_tracker_t
{
	float gyro_x, gyro_y, gyro_z;
	float accel_x, accel_y, accel_z;
	ChiakiOrientation orient;
	uint32_t timestamp;
	uint64_t sample_index;
	bool fuzz_enabled;
} ChiakiOrientationTracker;

CHIAKI_EXPORT void chiaki_orientation_tracker_init(ChiakiOrientationTracker *tracker);
CHIAKI_EXPORT void chiaki_orientation_tracker_update(ChiakiOrientationTracker *tracker,
		float gx, float gy, float gz, float ax, float ay, float az,
		ChiakiAccelNewZero *accel_zero, bool accel_zero_applied, uint32_t timestamp_us);
CHIAKI_EXPORT void chiaki_orientation_tracker_apply_to_controller_state(ChiakiOrientationTracker *tracker,
		ChiakiControllerState *state);
CHIAKI_EXPORT void chiaki_accel_new_zero_set_inactive(ChiakiAccelNewZero *accel_zero, bool real_accel);
CHIAKI_EXPORT void chiaki_accel_new_zero_set_active(ChiakiAccelNewZero *accel_zero, float accel_x, float accel_y, float accel_z, bool real_accel);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_ORIENTATION_H
