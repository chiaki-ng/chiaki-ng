// lib/src/gyrosteer.c
// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/gyrosteer.h>

#include <math.h>

// 握方向盘时"侧向"轴(体轴右侧)。真机校准:若倾斜方向反了改 {0,0,1} 或取负。
static const float BODY_RIGHT[3] = { 1.0f, 0.0f, 0.0f };

#define GYRO_STEER_CURVE 1.0f    // 响应曲线指数(1.0 线性)
#define GYRO_STEER_ALPHA 20.0f   // 低通:alpha = 1 - exp(-dt*ALPHA),~50ms 时间常数

static void quat_rotate_vec(const ChiakiOrientation *q, const float v[3], float out[3])
{
	float qx = q->x, qy = q->y, qz = q->z, qw = q->w;
	float tx = 2.0f * (qy * v[2] - qz * v[1]);
	float ty = 2.0f * (qz * v[0] - qx * v[2]);
	float tz = 2.0f * (qx * v[1] - qy * v[0]);
	out[0] = v[0] + qw * tx + (qy * tz - qz * ty);
	out[1] = v[1] + qw * ty + (qz * tx - qx * tz);
	out[2] = v[2] + qw * tz + (qx * ty - qy * tx);
}

static float wrap_180(float deg)
{
	while(deg > 180.0f)
		deg -= 360.0f;
	while(deg < -180.0f)
		deg += 360.0f;
	return deg;
}

static float roll_deg(const ChiakiOrientation *orient)
{
	float r[3];
	quat_rotate_vec(orient, BODY_RIGHT, r);
	// 侧轴相对水平面的倾角,即"握方向盘"时的左右倾斜角
	return atan2f(r[1], hypotf(r[0], r[2])) * 180.0f / (float)M_PI;
}

CHIAKI_EXPORT void chiaki_gyro_steer_init(ChiakiGyroSteer *gs,
		bool enabled, bool invert, float deadzone_deg, float max_angle_deg)
{
	gs->enabled = enabled;
	gs->invert = invert;
	gs->deadzone_deg = deadzone_deg;
	gs->max_angle_deg = max_angle_deg;
	gs->rest_valid = false;
	gs->rest_roll_deg = 0.0f;
	gs->angle_deg = 0.0f;
	gs->smoothed_left_x = 0.0f;
}

CHIAKI_EXPORT void chiaki_gyro_steer_set_rest(ChiakiGyroSteer *gs, const ChiakiOrientation *orient)
{
	gs->rest_roll_deg = roll_deg(orient);
	gs->rest_valid = true;
}

CHIAKI_EXPORT void chiaki_gyro_steer_update(ChiakiGyroSteer *gs, const ChiakiOrientation *orient, float dt_sec)
{
	float angle_deg = wrap_180(roll_deg(orient) - gs->rest_roll_deg);
	gs->angle_deg = angle_deg;
	float out = 0.0f;
	if(gs->rest_valid && fabsf(angle_deg) > gs->deadzone_deg)
	{
		float norm = angle_deg / gs->max_angle_deg;
		if(norm > 1.0f)
			norm = 1.0f;
		else if(norm < -1.0f)
			norm = -1.0f;
		out = copysignf(powf(fabsf(norm), GYRO_STEER_CURVE), norm);
	}
	if(gs->invert)
		out = -out;
	float alpha = dt_sec > 0.0f ? 1.0f - expf(-dt_sec * GYRO_STEER_ALPHA) : 1.0f;
	gs->smoothed_left_x += alpha * (out - gs->smoothed_left_x);
}

CHIAKI_EXPORT float chiaki_gyro_steer_get_angle_deg(const ChiakiGyroSteer *gs)
{
	return gs->angle_deg;
}

CHIAKI_EXPORT float chiaki_gyro_steer_get_left_x(const ChiakiGyroSteer *gs)
{
	return gs->smoothed_left_x;
}

CHIAKI_EXPORT bool chiaki_gyro_steer_is_active(const ChiakiGyroSteer *gs)
{
	return gs->enabled && gs->rest_valid;
}
