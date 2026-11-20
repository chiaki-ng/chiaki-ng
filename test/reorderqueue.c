// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/reorderqueue.h>

#define DROP_RECORD_MAX 16

typedef struct drop_record_t
{
	uint64_t count[DROP_RECORD_MAX];
	uint64_t seq_num[DROP_RECORD_MAX];
	bool failed;
} DropRecord;

static void drop(uint64_t seq_num, void *elem_user, void *cb_user)
{
	DropRecord *record = cb_user;
	uint64_t v = (uint64_t)(size_t)elem_user;
	if(v > DROP_RECORD_MAX)
	{
		record->failed = true;
		return;
	}
	record->count[v]++;
	record->seq_num[v] = seq_num;
}

static MunitResult test_reorder_queue_16(const MunitParameter params[], void *test_user)
{
	ChiakiReorderQueue queue;
	ChiakiErrorCode err = chiaki_reorder_queue_init_16(&queue, 2, 42);
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(chiaki_reorder_queue_size(&queue), ==, 4);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 0);

	chiaki_reorder_queue_set_drop_strategy(&queue, CHIAKI_REORDER_QUEUE_DROP_STRATEGY_END);

	DropRecord drop_record = { 0 };
	chiaki_reorder_queue_set_drop_cb(&queue, drop, &drop_record);

	uint64_t seq_num = 0;
	void *user = NULL;

	// pull from empty
	bool pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(!pulled);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 0);
	munit_assert(!drop_record.failed);

	// push one
	chiaki_reorder_queue_push(&queue, 42, (void *)0);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 1);

	// pull one
	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(pulled);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 0);
	munit_assert(!drop_record.failed);
	munit_assert_uint64(drop_record.count[0], ==, 0);
	munit_assert_uint64((uint64_t)(size_t)user, ==, 0);
	munit_assert_uint64(seq_num, ==, 42);

	// push outdated
	chiaki_reorder_queue_push(&queue, 42, (void *)0);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 0);
	munit_assert(!drop_record.failed);
	munit_assert_uint64(drop_record.count[0], ==, 1);
	munit_assert_uint64(drop_record.seq_num[0], ==, 42);
	memset(&drop_record, 0, sizeof(drop_record));

	// push until full out of order and try to pull in between
	chiaki_reorder_queue_push(&queue, 46, (void *)1);
	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(!pulled);
	chiaki_reorder_queue_push(&queue, 45, (void *)2);
	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(!pulled);
	chiaki_reorder_queue_push(&queue, 44, (void *)3);
	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(!pulled);
	chiaki_reorder_queue_push(&queue, 43, (void *)4);
	munit_assert(!drop_record.failed);
	for(size_t i=0; i<DROP_RECORD_MAX; i++)
		munit_assert_uint64(drop_record.count[i], ==, 0);

	// push more, because of CHIAKI_REORDER_QUEUE_DROP_STRATEGY_END this should be dropped
	chiaki_reorder_queue_push(&queue, 47, (void *)5);
	munit_assert(!drop_record.failed);
	for(size_t i=0; i<DROP_RECORD_MAX; i++)
		munit_assert_uint64(drop_record.count[i], ==, i == 5 ? 1 : 0);
	munit_assert_uint64(drop_record.seq_num[5], ==, 47);
	memset(&drop_record, 0, sizeof(drop_record));

	// push more with CHIAKI_REORDER_QUEUE_DROP_STRATEGY_BEGIN, so older elements should be dropped
	chiaki_reorder_queue_set_drop_strategy(&queue, CHIAKI_REORDER_QUEUE_DROP_STRATEGY_BEGIN);
	chiaki_reorder_queue_push(&queue, 47, (void *)5);
	munit_assert(!drop_record.failed);
	for(size_t i=0; i<DROP_RECORD_MAX; i++)
		munit_assert_uint64(drop_record.count[i], ==, i == 4 ? 1 : 0);
	munit_assert_uint64(drop_record.seq_num[4], ==, 43);
	memset(&drop_record, 0, sizeof(drop_record));
	
	// pull all, elements should arrive in order
	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(pulled);
	munit_assert_uint64(seq_num, ==, 44);
	munit_assert_uint64((uint64_t)(size_t)user, ==, 3);

	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(pulled);
	munit_assert_uint64(seq_num, ==, 45);
	munit_assert_uint64((uint64_t)(size_t)user, ==, 2);

	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(pulled);
	munit_assert_uint64(seq_num, ==, 46);
	munit_assert_uint64((uint64_t)(size_t)user, ==, 1);

	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(pulled);
	munit_assert_uint64(seq_num, ==, 47);
	munit_assert_uint64((uint64_t)(size_t)user, ==, 5);

	// should be empty now again
	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(!pulled);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 0);

	munit_assert(!drop_record.failed);
	for(size_t i=0; i<DROP_RECORD_MAX; i++)
		munit_assert_uint64(drop_record.count[i], ==, 0);

	// now push something much higher, because of CHIAKI_REORDER_QUEUE_DROP_STRATEGY_BEGIN, the queue should be relocated
	chiaki_reorder_queue_push(&queue, 1337, (void *)6);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 1);
	munit_assert(!drop_record.failed);
	for(size_t i=0; i<DROP_RECORD_MAX; i++)
		munit_assert_uint64(drop_record.count[i], ==, 0);

	// and pull again
	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(pulled);
	munit_assert_uint64(seq_num, ==, 1337);
	munit_assert_uint64((uint64_t)(size_t)user, ==, 6);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 0);

	// same as before, but with an element in the queue that will be dropped
	chiaki_reorder_queue_push(&queue, 1338, (void *)7);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 1);
	munit_assert(!drop_record.failed);
	for(size_t i=0; i<DROP_RECORD_MAX; i++)
		munit_assert_uint64(drop_record.count[i], ==, 0);

	chiaki_reorder_queue_push(&queue, 2000, (void *)8);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 1);
	munit_assert(!drop_record.failed);
	for(size_t i=0; i<DROP_RECORD_MAX; i++)
		munit_assert_uint64(drop_record.count[i], ==, i == 7 ? 1 : 0);
	munit_assert_uint64(drop_record.seq_num[7], ==, 1338);

	// pull again
	pulled = chiaki_reorder_queue_pull(&queue, &seq_num, &user);
	munit_assert(pulled);
	munit_assert_uint64(seq_num, ==, 2000);
	munit_assert_uint64((uint64_t)(size_t)user, ==, 8);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 0);

	chiaki_reorder_queue_fini(&queue);

	return MUNIT_OK;
}

static MunitResult test_reorder_queue_drop(const MunitParameter params[], void *test_user)
{
	ChiakiReorderQueue queue;
	ChiakiErrorCode err = chiaki_reorder_queue_init_16(&queue, 2, 42);
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);

	DropRecord drop_record = { 0 };
	chiaki_reorder_queue_set_drop_cb(&queue, drop, &drop_record);

	uint64_t seq_num = 0;
	void *user = NULL;

	chiaki_reorder_queue_push(&queue, 42, (void *)0);
	chiaki_reorder_queue_push(&queue, 43, (void *)1);
	chiaki_reorder_queue_push(&queue, 44, (void *)2);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 3);

	// drop the head, it must not be peeked or pulled afterwards
	chiaki_reorder_queue_drop(&queue, 0);
	munit_assert(!drop_record.failed);
	munit_assert_uint64(drop_record.count[0], ==, 1);
	munit_assert_uint64(drop_record.seq_num[0], ==, 42);
	munit_assert(!chiaki_reorder_queue_peek(&queue, 0, &seq_num, &user));
	munit_assert(!chiaki_reorder_queue_pull(&queue, &seq_num, &user));

	// dropping it again does nothing
	chiaki_reorder_queue_drop(&queue, 0);
	munit_assert_uint64(drop_record.count[0], ==, 1);

	// drop the tail, count shrinks to the last set element
	chiaki_reorder_queue_drop(&queue, 2);
	munit_assert_uint64(drop_record.count[2], ==, 1);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 2);

	// a packet pushed into the dropped head slot is pulled, then the remaining one
	chiaki_reorder_queue_push(&queue, 42, (void *)3);
	munit_assert(!drop_record.failed);
	munit_assert_uint64(drop_record.count[3], ==, 0);
	munit_assert(chiaki_reorder_queue_pull(&queue, &seq_num, &user));
	munit_assert_uint64(seq_num, ==, 42);
	munit_assert_uint64((uint64_t)(size_t)user, ==, 3);
	munit_assert(chiaki_reorder_queue_pull(&queue, &seq_num, &user));
	munit_assert_uint64(seq_num, ==, 43);
	munit_assert_uint64((uint64_t)(size_t)user, ==, 1);
	munit_assert_uint64(chiaki_reorder_queue_count(&queue), ==, 0);

	chiaki_reorder_queue_fini(&queue);

	return MUNIT_OK;
}


MunitTest tests_reorder_queue[] = {
	{
		"/reorder_queue_16",
		test_reorder_queue_16,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/reorder_queue_drop",
		test_reorder_queue_drop,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
