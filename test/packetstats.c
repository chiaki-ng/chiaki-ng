// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/packetstats.h>

static void push_seq_range(ChiakiPacketStats *stats, ChiakiSeqNum16 first, unsigned int count)
{
	for(unsigned int i = 0; i < count; i++)
		chiaki_packet_stats_push_seq(stats, (ChiakiSeqNum16)(first + i));
}

static MunitResult test_seq_no_loss(const MunitParameter params[], void *user)
{
	ChiakiPacketStats stats;
	munit_assert_int(chiaki_packet_stats_init(&stats), ==, CHIAKI_ERR_SUCCESS);

	push_seq_range(&stats, 1, 20);
	uint64_t received, lost;
	chiaki_packet_stats_get(&stats, true, &received, &lost);
	munit_assert_uint64(received, ==, 20);
	munit_assert_uint64(lost, ==, 0);

	chiaki_packet_stats_fini(&stats);
	return MUNIT_OK;
}

static MunitResult test_seq_wraparound(const MunitParameter params[], void *user)
{
	ChiakiPacketStats stats;
	munit_assert_int(chiaki_packet_stats_init(&stats), ==, CHIAKI_ERR_SUCCESS);

	// Advance the window to just before the 16 bit wrap
	push_seq_range(&stats, 1, 0xfff0);
	uint64_t received, lost;
	chiaki_packet_stats_get(&stats, true, &received, &lost);

	// 0xfff1 ... 0xffff, 0x0000 ... 0x0004: 20 packets across the wrap, none missing
	push_seq_range(&stats, 0xfff1, 20);
	chiaki_packet_stats_get(&stats, true, &received, &lost);
	munit_assert_uint64(received, ==, 20);
	munit_assert_uint64(lost, ==, 0);

	// Right after the wrap, with 2 packets missing
	push_seq_range(&stats, 0x0005, 3);
	push_seq_range(&stats, 0x000a, 5);
	chiaki_packet_stats_get(&stats, true, &received, &lost);
	munit_assert_uint64(received, ==, 8);
	munit_assert_uint64(lost, ==, 2);

	chiaki_packet_stats_fini(&stats);
	return MUNIT_OK;
}

MunitTest tests_packet_stats[] = {
	{
		"/seq_no_loss",
		test_seq_no_loss,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/seq_wraparound",
		test_seq_wraparound,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
