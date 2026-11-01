// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/seqnum.h>
#include <chiaki/packetstats.h>


static MunitResult test_seq_num_16(const MunitParameter params[], void *user)
{
	ChiakiSeqNum16 a = 0;
	do
	{
		ChiakiSeqNum16 b = a + 1;
		munit_assert(chiaki_seq_num_16_gt(b, a));
		munit_assert(!chiaki_seq_num_16_gt(a, b));
		munit_assert(chiaki_seq_num_16_lt(a, b));
		munit_assert(!chiaki_seq_num_16_lt(b, a));
		a = b;
	} while(a);

	a = 0;
	do
	{
		ChiakiSeqNum16 b = a + 0xfff;
		munit_assert(chiaki_seq_num_16_gt(b, a));
		munit_assert(!chiaki_seq_num_16_gt(a, b));
		munit_assert(chiaki_seq_num_16_lt(a, b));
		munit_assert(!chiaki_seq_num_16_lt(b, a));
		a++;
	} while(a);

	munit_assert(chiaki_seq_num_16_gt(1, 0xfff5));
	munit_assert(!chiaki_seq_num_16_gt(0xfff5, 1));

	return MUNIT_OK;
}


static MunitResult test_seq_num_32(const MunitParameter params[], void *user)
{
	munit_assert(chiaki_seq_num_32_gt(1, 0));
	munit_assert(!chiaki_seq_num_32_gt(0, 1));
	munit_assert(!chiaki_seq_num_32_lt(1, 0));
	munit_assert(chiaki_seq_num_32_lt(0, 1));
	munit_assert(chiaki_seq_num_32_gt(1, 0xfffffff5));
	munit_assert(!chiaki_seq_num_32_gt(0xfffffff5, 1));

	return MUNIT_OK;
}


// Packet stats must count a 16-bit sequence wrap as 0 lost packets, not 2^64.
static MunitResult test_packet_stats_wrap(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiPacketStats stats;
	munit_assert_int(chiaki_packet_stats_init(&stats), ==, CHIAKI_ERR_SUCCESS);
	stats.seq_min = stats.seq_max = 65530;
	for(unsigned i = 1; i <= 12; i++)
		chiaki_packet_stats_push_seq(&stats, (ChiakiSeqNum16)(65530 + i));
	uint64_t received, lost;
	chiaki_packet_stats_get(&stats, true, &received, &lost);
	munit_assert_uint64(received, ==, 12);
	munit_assert_uint64(lost, ==, 0);

	// One packet missing after the wrap.
	for(unsigned i = 1; i <= 6; i++)
		if(i != 3)
			chiaki_packet_stats_push_seq(&stats, (ChiakiSeqNum16)(65542 + i));
	chiaki_packet_stats_get(&stats, true, &received, &lost);
	munit_assert_uint64(received, ==, 5);
	munit_assert_uint64(lost, ==, 1);
	chiaki_packet_stats_fini(&stats);
	return MUNIT_OK;
}

MunitTest tests_seq_num[] = {
	{
		"/seq_num_16",
		test_seq_num_16,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/seq_num_32",
		test_seq_num_32,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/packet_stats_wrap",
		test_packet_stats_wrap,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};