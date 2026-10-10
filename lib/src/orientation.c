// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/orientation.h>
#include <math.h>

#define SIN_1_4_PI      0.7071067811865475
#define SIN_NEG_1_4_PI -0.7071067811865475
#define COS_1_4_PI      0.7071067811865476
#define COS_NEG_1_4_PI  0.7071067811865476

#define WARMUP_SAMPLES_COUNT 30
#define BETA_WARMUP 20.0f
#define BETA_DEFAULT 0.05f

#define ORIENT_FUZZ 0.0007f
#define FUZZ_FILTER_PREV_WEIGHT 0.75f
#define FUZZ_FILTER_PREV_WEIGHT2x 0.6f

CHIAKI_EXPORT void chiaki_orientation_init(ChiakiOrientation *orient)
{
	// 90 deg rotation around x for Madgwick
	orient->x = SIN_1_4_PI;
	orient->y = 0.0f;
	orient->z = 0.0f;
	orient->w = COS_1_4_PI;
}

static float inv_sqrt(float x);
static void fuzz(const float cur, float *prev, const float fuzz, const float wprev, const float wprev2x);

CHIAKI_EXPORT void chiaki_orientation_update(ChiakiOrientation *orient,
		float gx, float gy, float gz, float ax, float ay, float az, float beta, float time_step_sec, bool fuzz_enabled)
{
	float q0 = orient->w, q1 = orient->x, q2 = orient->y, q3 = orient->z;
	// Madgwick's IMU algorithm.
	// See: http://www.x-io.co.uk/node/8#open_source_ahrs_and_imu_algorithms
	float recip_norm;
	float s0, s1, s2, s3;
	float q_dot1, q_dot2, q_dot3, q_dot4;
	float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2 ,_8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

	// Rate of change of quaternion from gyroscope
	q_dot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
	q_dot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
	q_dot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
	q_dot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

	// Compute feedback only if accelerometer measurement valid (avoids NaN in accelerometer normalisation)
	if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f)))
	{
		// Normalise accelerometer measurement
		recip_norm = inv_sqrt(ax * ax + ay * ay + az * az);
		ax *= recip_norm;
		ay *= recip_norm;
		az *= recip_norm;

		// Auxiliary variables to avoid repeated arithmetic
		_2q0 = 2.0f * q0;
		_2q1 = 2.0f * q1;
		_2q2 = 2.0f * q2;
		_2q3 = 2.0f * q3;
		_4q0 = 4.0f * q0;
		_4q1 = 4.0f * q1;
		_4q2 = 4.0f * q2;
		_8q1 = 8.0f * q1;
		_8q2 = 8.0f * q2;
		q0q0 = q0 * q0;
		q1q1 = q1 * q1;
		q2q2 = q2 * q2;
		q3q3 = q3 * q3;

		// Gradient decent algorithm corrective step
		s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
		s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
		s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
		s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
		recip_norm = s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3; // normalise step magnitude
		// avoid NaN when the orientation is already perfect or inverse to perfect
		if(recip_norm > 0.000001f)
		{
			recip_norm = inv_sqrt(recip_norm);
			s0 *= recip_norm;
			s1 *= recip_norm;
			s2 *= recip_norm;
			s3 *= recip_norm;

			// Apply feedback step
			q_dot1 -= beta * s0;
			q_dot2 -= beta * s1;
			q_dot3 -= beta * s2;
			q_dot4 -= beta * s3;
		}
	}

	// Integrate rate of change of quaternion to yield quaternion
	q0 += q_dot1 * time_step_sec;
	q1 += q_dot2 * time_step_sec;
	q2 += q_dot3 * time_step_sec;
	q3 += q_dot4 * time_step_sec;

	// Normalise quaternion
	recip_norm = inv_sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
	q0 *= recip_norm;
	q1 *= recip_norm;
	q2 *= recip_norm;
	q3 *= recip_norm;

	if(fuzz_enabled)
	{
		fuzz(q0, &orient->w, ORIENT_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x);
		fuzz(q1, &orient->x, ORIENT_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x);
		fuzz(q2, &orient->y, ORIENT_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x);
		fuzz(q3, &orient->z, ORIENT_FUZZ, FUZZ_FILTER_PREV_WEIGHT, FUZZ_FILTER_PREV_WEIGHT2x);
	}
	else
	{
		orient->w = q0;
		orient->x = q1;
		orient->y = q2;
		orient->z = q3;
	}
}

// input fuzz filter like the one used in kernel input system
static void fuzz(const float cur, float *prev, const float fuzz, const float wprev, const float wprev2x)
{
	// smooth (0-1) is weight of previous (0.9 = 90%) to use at fuzz value
	// wprev2x (0-1) is weight of previous (0.5 = 50%) to use at 2x fuzz value
	if ((cur < *prev + fuzz / (float)2) && (cur > *prev - fuzz / (float)2))
		return;
	if ((cur < *prev + fuzz) && (cur > *prev - fuzz))
	{
		*prev = (wprev * *prev + (1 - wprev) * cur);
		return;
	}
	if ((cur < *prev + fuzz * 2) && (cur > *prev - fuzz * 2))
	{
		*prev = (wprev2x * *prev + (1 - wprev2x) * cur);
		return;
	}
	*prev = cur;
}

static float inv_sqrt(float x)
{
#if 1
	return 1.0f / sqrt(x);
#else
	// Fast inverse square-root
	// See: http://en.wikipedia.org/wiki/Fast_inverse_square_root
	float halfx = 0.5f * x;
	float y = x;
	long i = *(long*)&y;
	i = 0x5f3759df - (i>>1);
	y = *(float*)&i;
	y = y * (1.5f - (halfx * y * y));
	return y;
#endif
}

CHIAKI_EXPORT void chiaki_orientation_tracker_init(ChiakiOrientationTracker *tracker)
{
	tracker->accel_x = 0.0f;
	tracker->accel_y = 1.0f;
	tracker->accel_z = 0.0f;
	tracker->gyro_x = tracker->gyro_y = tracker->gyro_z = 0.0f;
	chiaki_orientation_init(&tracker->orient);
	tracker->timestamp = 0;
	tracker->sample_index = 0;
	tracker->fuzz_enabled = true;
}

CHIAKI_EXPORT void chiaki_orientation_tracker_update(ChiakiOrientationTracker *tracker,
		float gx, float gy, float gz, float ax, float ay, float az,
		ChiakiAccelNewZero *accel_zero, bool accel_zero_applied, uint32_t timestamp_us)
{
	tracker->gyro_x = gx;
	tracker->gyro_y = gy;
	tracker->gyro_z = gz;
	if(!accel_zero_applied)
	{
		ax -= accel_zero->accel_x;
		ay -= accel_zero->accel_y;
		az -= accel_zero->accel_z;
	}
	tracker->accel_x = ax;
	tracker->accel_y = ay;
	tracker->accel_z = az;
	tracker->sample_index++;
	if(tracker->sample_index <= 1)
	{
		tracker->timestamp = timestamp_us;
		return;
	}
	uint64_t delta_us = timestamp_us;
	if(delta_us < tracker->timestamp)
		delta_us += (1ULL << 32);
	delta_us -= tracker->timestamp;
	tracker->timestamp = timestamp_us;
	chiaki_orientation_update(&tracker->orient, gx, gy, gz, ax, ay, az,
			tracker->sample_index < WARMUP_SAMPLES_COUNT ? BETA_WARMUP : BETA_DEFAULT,
			(float)delta_us / 1000000.0f, tracker->fuzz_enabled);
}

CHIAKI_EXPORT void chiaki_orientation_tracker_apply_to_controller_state(ChiakiOrientationTracker *tracker,
		ChiakiControllerState *state)
{
	state->gyro_x = tracker->gyro_x;
	state->gyro_y = tracker->gyro_y;
	state->gyro_z = tracker->gyro_z;
	state->accel_x = tracker->accel_x;
	state->accel_y = tracker->accel_y;
	state->accel_z = tracker->accel_z;
	// -90 deg rotation around x from Madgwick
	state->orient_w = COS_NEG_1_4_PI * tracker->orient.w - SIN_NEG_1_4_PI * tracker->orient.x;
	state->orient_x = COS_NEG_1_4_PI * tracker->orient.x + SIN_NEG_1_4_PI * tracker->orient.w;
	state->orient_y = COS_NEG_1_4_PI * tracker->orient.y - SIN_NEG_1_4_PI * tracker->orient.z;
	state->orient_z = COS_NEG_1_4_PI * tracker->orient.z + SIN_NEG_1_4_PI * tracker->orient.y;
}

CHIAKI_EXPORT void chiaki_accel_new_zero_set_inactive(ChiakiAccelNewZero *accel_zero, bool real_accel)
{
	accel_zero->accel_x = 0.0f;
	if(real_accel)
		accel_zero->accel_y = 1.0f;
	else
		accel_zero->accel_y = 0.0f;
	accel_zero->accel_z = 0.0f;
}

CHIAKI_EXPORT void chiaki_accel_new_zero_set_active(ChiakiAccelNewZero *accel_zero, float accel_x, float accel_y, float accel_z, bool real_accel)
{
	accel_zero->accel_x = accel_x;
	if(real_accel)
		accel_zero->accel_y = accel_y;
	else
		accel_zero->accel_y = accel_y - 1.0f;
	accel_zero->accel_z = accel_z;
}