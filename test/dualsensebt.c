// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/dualsensebt.h>

#include <string.h>

static void fill_pattern(uint8_t *buf, size_t size)
{
	for(size_t i = 0; i < size; i++)
		buf[i] = (uint8_t)(i * 7 + 3);
}

static MunitResult test_crc(const MunitParameter params[], void *user)
{
	uint8_t report[78] = { 0x31, 0x10, 0x10 };
	chiaki_dualsense_bt_fill_crc(report, sizeof(report));
	// zlib.crc32(b'\xa2' + report[:-4])
	static const uint8_t expected[] = { 0x1c, 0xf5, 0x29, 0x5f };
	munit_assert_memory_equal(sizeof(expected), report + sizeof(report) - 4, expected);
	return MUNIT_OK;
}

static MunitResult test_haptics_report_double(const MunitParameter params[], void *user)
{
	uint8_t samples[2 * CHIAKI_DUALSENSE_BT_HAPTICS_BLOCK_SIZE];
	fill_pattern(samples, sizeof(samples));
	uint8_t report[CHIAKI_DUALSENSE_BT_HAPTICS_REPORT_SIZE];
	chiaki_dualsense_bt_haptics_report(report, 5, 42, 48, samples, sizeof(samples), true);

	munit_assert_uint8(report[0], ==, 0x32);
	munit_assert_uint8(report[1], ==, 0x50);
	munit_assert_uint8(report[2], ==, 0x91);
	munit_assert_uint8(report[3], ==, 4);
	munit_assert_uint8(report[4], ==, 0x66);
	munit_assert_uint8(report[4] & 1, ==, 0); // the microphone stays off
	munit_assert_uint8(report[5], ==, 48);
	munit_assert_uint8(report[6], ==, 48);
	munit_assert_uint8(report[7], ==, 42);
	munit_assert_uint8(report[8], ==, 0xd2);
	munit_assert_uint8(report[9], ==, 64);
	munit_assert_memory_equal(sizeof(samples), report + 10, samples);
	// zlib.crc32(b'\xa2' + report[:138])
	static const uint8_t expected_crc[] = { 0x3f, 0x5f, 0xff, 0xe1 };
	munit_assert_memory_equal(sizeof(expected_crc), report + 138, expected_crc);
	return MUNIT_OK;
}

static MunitResult test_haptics_report_single(const MunitParameter params[], void *user)
{
	uint8_t samples[2 * CHIAKI_DUALSENSE_BT_HAPTICS_BLOCK_SIZE];
	fill_pattern(samples, sizeof(samples));
	uint8_t report[CHIAKI_DUALSENSE_BT_HAPTICS_REPORT_SIZE];
	memset(report, 0xff, sizeof(report));
	chiaki_dualsense_bt_haptics_report(report, 15, 7, 16, samples, sizeof(samples), false);

	munit_assert_uint8(report[1], ==, 0xf0);
	munit_assert_uint8(report[5], ==, 16);
	munit_assert_uint8(report[7], ==, 7);
	munit_assert_uint8(report[8], ==, 0x92);
	// only one block is taken, the rest stays zero
	munit_assert_memory_equal(CHIAKI_DUALSENSE_BT_HAPTICS_BLOCK_SIZE, report + 10, samples);
	for(size_t i = 10 + CHIAKI_DUALSENSE_BT_HAPTICS_BLOCK_SIZE; i < 138; i++)
		munit_assert_uint8(report[i], ==, 0);
	static const uint8_t expected_crc[] = { 0xfa, 0x73, 0xbf, 0x69 };
	munit_assert_memory_equal(sizeof(expected_crc), report + 138, expected_crc);
	return MUNIT_OK;
}

static MunitResult test_haptics_report_short(const MunitParameter params[], void *user)
{
	uint8_t samples[60];
	fill_pattern(samples, sizeof(samples));
	uint8_t report[CHIAKI_DUALSENSE_BT_HAPTICS_REPORT_SIZE];
	memset(report, 0xff, sizeof(report));
	chiaki_dualsense_bt_haptics_report(report, 0, 0, 48, samples, sizeof(samples), true);

	munit_assert_memory_equal(sizeof(samples), report + 10, samples);
	for(size_t i = 10 + sizeof(samples); i < 138; i++)
		munit_assert_uint8(report[i], ==, 0);
	static const uint8_t expected_crc[] = { 0x35, 0x2c, 0x7e, 0xa8 };
	munit_assert_memory_equal(sizeof(expected_crc), report + 138, expected_crc);
	return MUNIT_OK;
}

static MunitResult test_haptics_convert(const MunitParameter params[], void *user)
{
	static const int16_t in[] = { 0, 255, 256, -256, -255, 1280, -1280, INT16_MAX, INT16_MIN, 32767 / 2 };
	int8_t out[sizeof(in) / sizeof(in[0])];

	munit_assert_true(chiaki_dualsense_bt_haptics_convert(out, in, sizeof(out), 1.0f));
	static const int8_t unity[] = { 0, 0, 1, -1, 0, 5, -5, 127, -128, 63 };
	munit_assert_memory_equal(sizeof(unity), out, unity);

	munit_assert_true(chiaki_dualsense_bt_haptics_convert(out, in, sizeof(out), 0.5f));
	static const int8_t half[] = { 0, 0, 0, 0, 0, 2, -2, 63, -64, 31 };
	munit_assert_memory_equal(sizeof(half), out, half);

	// gain above 1 clamps instead of wrapping
	munit_assert_true(chiaki_dualsense_bt_haptics_convert(out, in, sizeof(out), 4.0f));
	static const int8_t clamped[] = { 0, 3, 4, -4, -3, 20, -20, 127, -128, 127 };
	munit_assert_memory_equal(sizeof(clamped), out, clamped);

	static const int16_t quiet[] = { 0, 100, -100, 255 };
	int8_t quiet_out[4];
	munit_assert_false(chiaki_dualsense_bt_haptics_convert(quiet_out, quiet, 4, 1.0f));
	munit_assert_false(chiaki_dualsense_bt_haptics_convert(out, in, sizeof(out), 0.0f));
	return MUNIT_OK;
}

MunitTest tests_dualsense_bt[] = {
	{
		"/crc",
		test_crc,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/haptics_report_double",
		test_haptics_report_double,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/haptics_report_single",
		test_haptics_report_single,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/haptics_report_short",
		test_haptics_report_short,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/haptics_convert",
		test_haptics_convert,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
