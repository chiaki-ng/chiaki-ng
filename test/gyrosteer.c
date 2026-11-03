// test/gyrosteer.c
// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/gyrosteer.h>

#include <math.h>

// 构造绕体轴 Z(前进轴)旋转 angle_deg 的四元数(模块内 BODY_RIGHT = 体轴 X)
static ChiakiOrientation roll_quat(float angle_deg)
{
	float half = angle_deg * (float)M_PI / 180.0f / 2.0f;
	ChiakiOrientation q;
	q.x = 0.0f;
	q.y = 0.0f;
	q.z = sinf(half);
	q.w = cosf(half);
	return q;
}

static void settle(ChiakiGyroSteer *gs, const ChiakiOrientation *orient)
{
	// 多次 update 让低通收敛
	for(int i = 0; i < 30; i++)
		chiaki_gyro_steer_update(gs, orient, 0.016f);
}

static MunitResult test_init_inactive(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, 3.0f, 30.0f);
	munit_assert_false(chiaki_gyro_steer_is_active(&gs));
	munit_assert_float(chiaki_gyro_steer_get_left_x(&gs), ==, 0.0f);
	return MUNIT_OK;
}

static MunitResult test_zero_orientation(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, 3.0f, 30.0f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	munit_assert_true(chiaki_gyro_steer_is_active(&gs));
	settle(&gs, &ident);
	munit_assert_float(chiaki_gyro_steer_get_left_x(&gs), ==, 0.0f);
	munit_assert_float(chiaki_gyro_steer_get_angle_deg(&gs), ==, 0.0f);
	return MUNIT_OK;
}

static MunitResult test_full_deflection(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, 3.0f, 30.0f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	ChiakiOrientation roll = roll_quat(30.0f); // 满偏
	settle(&gs, &roll);
	// 低通 30 次迭代仍有收敛残差(~6.8e-5),用容差断言而非精确 ==
	munit_assert_double_equal(chiaki_gyro_steer_get_left_x(&gs), 1.0, 3);
	return MUNIT_OK;
}

static MunitResult test_negative_deflection(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, 3.0f, 30.0f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	ChiakiOrientation roll = roll_quat(-30.0f);
	settle(&gs, &roll);
	munit_assert_double_equal(chiaki_gyro_steer_get_left_x(&gs), -1.0, 3);
	return MUNIT_OK;
}

static MunitResult test_deadzone(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, 3.0f, 30.0f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	ChiakiOrientation roll = roll_quat(2.0f); // 死区内
	settle(&gs, &roll);
	munit_assert_float(chiaki_gyro_steer_get_left_x(&gs), ==, 0.0f);
	return MUNIT_OK;
}

static MunitResult test_invert(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, true, 3.0f, 30.0f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	ChiakiOrientation roll = roll_quat(30.0f);
	settle(&gs, &roll);
	munit_assert_double_equal(chiaki_gyro_steer_get_left_x(&gs), -1.0, 3);
	return MUNIT_OK;
}

static MunitResult test_set_rest(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, 3.0f, 30.0f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	ChiakiOrientation tilted = roll_quat(20.0f);
	chiaki_gyro_steer_set_rest(&gs, &tilted); // 把 20° 处设为回正中心
	settle(&gs, &tilted);
	munit_assert_float(chiaki_gyro_steer_get_left_x(&gs), ==, 0.0f);
	settle(&gs, &ident); // 回到平放,相对 rest 反向 20°
	munit_assert_double_equal(chiaki_gyro_steer_get_left_x(&gs), -(20.0 / 30.0), 3);
	return MUNIT_OK;
}

MunitTest tests_gyrosteer[] = {
	{ "/init_inactive", test_init_inactive, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/zero_orientation", test_zero_orientation, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/full_deflection", test_full_deflection, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/negative_deflection", test_negative_deflection, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/deadzone", test_deadzone, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/invert", test_invert, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/set_rest", test_set_rest, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
