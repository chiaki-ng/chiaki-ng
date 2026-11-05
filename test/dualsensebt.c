// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/dualsensebt.h>

#include <string.h>

static MunitResult test_crc(const MunitParameter params[], void *user)
{
	uint8_t report[CHIAKI_DUALSENSE_BT_STATE_REPORT_SIZE] = { 0x31, 0x10, 0x10 };
	chiaki_dualsense_bt_fill_crc(report, sizeof(report));
	// zlib.crc32(b'\xa2' + report[:-4])
	static const uint8_t expected[] = { 0x1c, 0xf5, 0x29, 0x5f };
	munit_assert_memory_equal(sizeof(expected), report + sizeof(report) - 4, expected);
	return MUNIT_OK;
}

static MunitResult test_audio_report(const MunitParameter params[], void *user)
{
	uint8_t haptics[CHIAKI_DUALSENSE_BT_HAPTICS_SIZE];
	uint8_t opus[CHIAKI_DUALSENSE_BT_OPUS_FRAMES * CHIAKI_DUALSENSE_BT_OPUS_FRAME_SIZE];
	memset(haptics, 0x11, sizeof(haptics));
	memset(opus, 0x22, sizeof(opus));
	uint8_t report[CHIAKI_DUALSENSE_BT_AUDIO_REPORT_SIZE];
	chiaki_dualsense_bt_audio_report(report, 5, 42, haptics, opus, false);

	munit_assert_uint8(report[0], ==, 0x39);
	munit_assert_uint8(report[1], ==, 0x50);
	munit_assert_uint8(report[2], ==, 0x91);
	munit_assert_uint8(report[3], ==, 6);
	munit_assert_uint8(report[4] & 1, ==, 0); // the microphone stays off
	munit_assert_uint8(report[9], ==, 42);
	munit_assert_uint8(report[10], ==, 0xd2);
	munit_assert_uint8(report[11], ==, 64);
	munit_assert_memory_equal(sizeof(haptics), report + 12, haptics);
	munit_assert_uint8(report[140], ==, 0xd3);
	munit_assert_uint8(report[141], ==, 200);
	munit_assert_memory_equal(sizeof(opus), report + 142, opus);

	chiaki_dualsense_bt_audio_report(report, 5, 42, haptics, opus, true);
	munit_assert_uint8(report[140], ==, 0xd6);
	return MUNIT_OK;
}

static MunitResult test_output_route_report(const MunitParameter params[], void *user)
{
	uint8_t report[CHIAKI_DUALSENSE_BT_STATE_REPORT_SIZE];
	chiaki_dualsense_bt_output_route_report(report, 3, CHIAKI_DUALSENSE_BT_OUTPUT_SPEAKER);
	munit_assert_uint8(report[0], ==, 0x31);
	munit_assert_uint8(report[1], ==, 0x30);
	munit_assert_uint8(report[2], ==, 0x10);
	munit_assert_uint8(report[3], ==, 0xa0);
	munit_assert_uint8(report[3 + 7], ==, 0x38);

	chiaki_dualsense_bt_output_route_report(report, 3, CHIAKI_DUALSENSE_BT_OUTPUT_HEADPHONES);
	munit_assert_uint8(report[3], ==, 0x90);
	munit_assert_uint8(report[3 + 4], ==, 0x50);
	munit_assert_uint8(report[3 + 7], ==, 0);

	chiaki_dualsense_bt_output_route_report(report, 3, CHIAKI_DUALSENSE_BT_OUTPUT_DEFAULT);
	munit_assert_uint8(report[3], ==, 0x80); // leaves the volumes alone
	munit_assert_uint8(report[3 + 4], ==, 0);
	munit_assert_uint8(report[3 + 5], ==, 0);
	munit_assert_uint8(report[3 + 7], ==, 0);
	return MUNIT_OK;
}

static MunitResult test_input_headphones(const MunitParameter params[], void *user)
{
	uint8_t report[78] = { 0x31 };
	munit_assert_true(chiaki_dualsense_bt_input_has_status(report, sizeof(report)));
	munit_assert_false(chiaki_dualsense_bt_input_headphones(report, sizeof(report)));
	report[55] = 0x01;
	munit_assert_true(chiaki_dualsense_bt_input_headphones(report, sizeof(report)));
	munit_assert_false(chiaki_dualsense_bt_input_has_status(report, 10));
	report[0] = 0x01; // the short report the pad sends before it switches to the full one
	munit_assert_false(chiaki_dualsense_bt_input_has_status(report, sizeof(report)));
	munit_assert_false(chiaki_dualsense_bt_input_headphones(report, sizeof(report)));
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
		"/audio_report",
		test_audio_report,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/output_route_report",
		test_output_route_report,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/input_headphones",
		test_input_headphones,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
