// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/controller.h>
#include <chiaki/feedback.h>

#include "test_log.h"

#define PACKETS_MAX 8

typedef struct packets_t
{
	ChiakiFeedbackHistoryBuffer *history;
	uint8_t buf[PACKETS_MAX][0x40]; // events newest first
	size_t size[PACKETS_MAX];
	size_t count;
} Packets;

static void format_packet(void *user)
{
	Packets *packets = user;
	munit_assert_size(packets->count, <, PACKETS_MAX);
	size_t size = sizeof(packets->buf[0]);
	munit_assert_int(chiaki_feedback_history_buffer_format(packets->history, packets->buf[packets->count], &size), ==, CHIAKI_ERR_SUCCESS);
	packets->size[packets->count++] = size;
}

static void record(const ChiakiControllerState *prev, const ChiakiControllerState *now, Packets *packets)
{
	ChiakiFeedbackHistoryBuffer history;
	munit_assert_int(chiaki_feedback_history_buffer_init(&history, 0x10), ==, CHIAKI_ERR_SUCCESS);
	packets->history = &history;
	packets->count = 0;
	chiaki_feedback_history_buffer_record_state(&history, get_test_log(), prev, now, format_packet, packets);
	chiaki_feedback_history_buffer_fini(&history);
}

static MunitResult test_touchpad_click_release_before_touch_up(const MunitParameter params[], void *user)
{
	// a tap releases click and finger in the same state change; a console seeing the
	// finger lift while the click is still down keeps the finger down
	ChiakiControllerState prev, now;
	chiaki_controller_state_set_idle(&prev);
	chiaki_controller_state_start_touch(&prev, 100, 200);
	prev.buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
	chiaki_controller_state_set_idle(&now);

	Packets packets;
	record(&prev, &now, &packets);
	munit_assert_size(packets.count, ==, 2);
	munit_assert_size(packets.size[0], ==, 2);
	munit_assert_uint8(packets.buf[0][0], ==, 0x80); // click up
	munit_assert_uint8(packets.buf[0][1], ==, 0x91);
	munit_assert_size(packets.size[1], ==, 5 + 2);
	munit_assert_uint8(packets.buf[1][0], ==, 0xc0); // then touch up
	return MUNIT_OK;
}

static MunitResult test_touchpad_touch_down_before_click_press(const MunitParameter params[], void *user)
{
	ChiakiControllerState prev, now;
	chiaki_controller_state_set_idle(&prev);
	chiaki_controller_state_set_idle(&now);
	chiaki_controller_state_start_touch(&now, 100, 200);
	now.buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;

	Packets packets;
	record(&prev, &now, &packets);
	munit_assert_size(packets.count, ==, 2);
	munit_assert_size(packets.size[0], ==, 5);
	munit_assert_uint8(packets.buf[0][0], ==, 0xd0); // touch down
	munit_assert_size(packets.size[1], ==, 2 + 5);
	munit_assert_uint8(packets.buf[1][0], ==, 0x80); // then click press
	munit_assert_uint8(packets.buf[1][1], ==, 0xb1);
	return MUNIT_OK;
}

static MunitResult test_simultaneous_l2_r2_one_packet_each(const MunitParameter params[], void *user)
{
	// the console applies only the newest event of a packet, so L2 sharing a packet with R2 got lost
	ChiakiControllerState prev, now;
	chiaki_controller_state_set_idle(&prev);
	chiaki_controller_state_set_idle(&now);
	now.l2_state = 0xff;
	now.r2_state = 0xff;

	Packets packets;
	record(&prev, &now, &packets);
	munit_assert_size(packets.count, ==, 2);
	munit_assert_size(packets.size[0], ==, 3);
	munit_assert_uint8(packets.buf[0][1], ==, 0x86); // L2
	munit_assert_size(packets.size[1], ==, 3 + 3);
	munit_assert_uint8(packets.buf[1][1], ==, 0x87); // then R2
	return MUNIT_OK;
}

static MunitResult test_no_change_no_packet(const MunitParameter params[], void *user)
{
	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);

	Packets packets;
	record(&state, &state, &packets);
	munit_assert_size(packets.count, ==, 0);
	return MUNIT_OK;
}

MunitTest tests_feedback[] = {
	{
		"/touchpad_click_release_before_touch_up",
		test_touchpad_click_release_before_touch_up,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/touchpad_touch_down_before_click_press",
		test_touchpad_touch_down_before_click_press,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/simultaneous_l2_r2_one_packet_each",
		test_simultaneous_l2_r2_one_packet_each,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/no_change_no_packet",
		test_no_change_no_packet,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
