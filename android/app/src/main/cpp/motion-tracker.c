// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <jni.h>
#include <stdint.h>
#include <stdlib.h>

#include <chiaki/controller.h>
#include <chiaki/orientation.h>

#include "chiaki-jni.h"

#define MOTION_VALUES_COUNT 10

typedef struct android_chiaki_motion_tracker_t
{
	ChiakiOrientationTracker tracker;
	ChiakiAccelNewZero real_accel;
	ChiakiAccelNewZero accel_zero;
	uint32_t timestamp_us;
} AndroidChiakiMotionTracker;

static void motion_tracker_write(JNIEnv *env, AndroidChiakiMotionTracker *mt, jfloatArray out)
{
	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);
	chiaki_orientation_tracker_apply_to_controller_state(&mt->tracker, &state);
	jfloat values[MOTION_VALUES_COUNT] = {
		state.gyro_x, state.gyro_y, state.gyro_z,
		state.accel_x, state.accel_y, state.accel_z,
		state.orient_x, state.orient_y, state.orient_z, state.orient_w
	};
	E->SetFloatArrayRegion(env, out, 0, MOTION_VALUES_COUNT, values);
}

JNIEXPORT jlong JNICALL JNI_FCN(motionTrackerCreate)(JNIEnv *env, jobject obj)
{
	AndroidChiakiMotionTracker *mt = calloc(1, sizeof(AndroidChiakiMotionTracker));
	if(!mt)
		return 0;
	chiaki_orientation_tracker_init(&mt->tracker);
	chiaki_accel_new_zero_set_inactive(&mt->accel_zero, false);
	chiaki_accel_new_zero_set_inactive(&mt->real_accel, true);
	return (jlong)(intptr_t)mt;
}

JNIEXPORT void JNICALL JNI_FCN(motionTrackerFree)(JNIEnv *env, jobject obj, jlong ptr)
{
	free((AndroidChiakiMotionTracker *)(intptr_t)ptr);
}

JNIEXPORT void JNICALL JNI_FCN(motionTrackerUpdateAccel)(JNIEnv *env, jobject obj, jlong ptr, jfloat x, jfloat y, jfloat z, jint timestamp_us, jfloatArray out)
{
	AndroidChiakiMotionTracker *mt = (AndroidChiakiMotionTracker *)(intptr_t)ptr;
	chiaki_accel_new_zero_set_active(&mt->real_accel, x, y, z, true);
	mt->timestamp_us = (uint32_t)timestamp_us;
	chiaki_orientation_tracker_update(&mt->tracker,
			mt->tracker.gyro_x, mt->tracker.gyro_y, mt->tracker.gyro_z,
			x, y, z, &mt->accel_zero, false, mt->timestamp_us);
	motion_tracker_write(env, mt, out);
}

JNIEXPORT void JNICALL JNI_FCN(motionTrackerUpdateGyro)(JNIEnv *env, jobject obj, jlong ptr, jfloat x, jfloat y, jfloat z, jint timestamp_us, jfloatArray out)
{
	AndroidChiakiMotionTracker *mt = (AndroidChiakiMotionTracker *)(intptr_t)ptr;
	mt->timestamp_us = (uint32_t)timestamp_us;
	// accel stored in the tracker already has the zero applied
	chiaki_orientation_tracker_update(&mt->tracker,
			x, y, z,
			mt->tracker.accel_x, mt->tracker.accel_y, mt->tracker.accel_z,
			&mt->accel_zero, true, mt->timestamp_us);
	motion_tracker_write(env, mt, out);
}

JNIEXPORT void JNICALL JNI_FCN(motionTrackerReset)(JNIEnv *env, jobject obj, jlong ptr, jfloatArray out)
{
	AndroidChiakiMotionTracker *mt = (AndroidChiakiMotionTracker *)(intptr_t)ptr;
	float gx = mt->tracker.gyro_x, gy = mt->tracker.gyro_y, gz = mt->tracker.gyro_z;
	chiaki_accel_new_zero_set_active(&mt->accel_zero, mt->real_accel.accel_x, mt->real_accel.accel_y, mt->real_accel.accel_z, false);
	chiaki_orientation_tracker_init(&mt->tracker);
	chiaki_orientation_tracker_update(&mt->tracker, gx, gy, gz,
			mt->real_accel.accel_x, mt->real_accel.accel_y, mt->real_accel.accel_z,
			&mt->accel_zero, false, mt->timestamp_us);
	motion_tracker_write(env, mt, out);
}
