// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/takion.h>
#include <chiaki/seqnum.h>
#include <chiaki/base64.h>

#define CHIAKI_UNIT_TEST
#include "../lib/src/takionsendbuffer.c"

#include "test_log.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif


static MunitResult test_av_packet_parse(const MunitParameter params[], void *user)
{
	uint8_t packet[] = {
			0x2, 0x0, 0x2d, 0x0, 0x5, 0x0, 0xc0, 0x1c, 0x1, 0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
			0xe4, 0x10, 0x3, 0x67, 0x0, 0x29, 0xf3, 0x2f, 0x98, 0xf6, 0x99, 0x82, 0x83, 0x78, 0xdb, 0x29,
			0x43, 0xa9, 0xe5, 0x88, 0xf2, 0x11, 0x4, 0x20, 0xe6, 0x20, 0x96, 0xe9, 0x6, 0xee, 0xd, 0x27,
			0xa1, 0x83, 0x82, 0x88, 0xe6, 0x21, 0x49, 0x2, 0x75, 0x74, 0x32, 0x5b, 0xf6, 0xe9, 0xdc, 0x93,
			0xea, 0x31, 0x88, 0xd, 0x2b, 0x4b, 0x34, 0xf9, 0xec, 0x1b, 0x26, 0xcc, 0xbb, 0xbb, 0x81, 0xf2,
			0xd9, 0x2d, 0x8e, 0xa1, 0xb9, 0xe2, 0xb3, 0xca, 0xb2, 0x7d, 0xa3, 0x31, 0xf0, 0x42, 0xb7, 0xb6,
			0x1e, 0x8f, 0x6d, 0xa2, 0x70, 0x46, 0xfd, 0x7e, 0x9b, 0x60, 0x85, 0xb0, 0xed, 0x4f, 0x20, 0xb5,
			0x1, 0x71, 0xa9, 0xaa, 0x18, 0x6b, 0x2a, 0x90, 0xf3, 0xa7, 0x84, 0x36, 0xfd, 0x6d, 0x14, 0x83,
			0x68, 0xa3, 0x9b, 0x3a, 0xc8, 0xd4, 0x3a, 0x31, 0xa0, 0x9b, 0x61, 0xde, 0xa7, 0xed, 0x46, 0xb4,
			0xa3, 0xdf, 0x3f, 0x44, 0x8f, 0xad, 0x64, 0x9, 0xfc, 0x7a, 0xe7, 0x24, 0xf0, 0xd2, 0x42, 0xd3,
			0x57, 0x5a, 0x76, 0x0, 0xc5, 0xe0, 0x93, 0xa9, 0xf5, 0x32, 0x5d, 0xee, 0xf7, 0x9d
	};

	ChiakiKeyState key_state;
	chiaki_key_state_init(&key_state);

	ChiakiTakionAVPacket av_packet;

	ChiakiErrorCode err = chiaki_takion_v9_av_packet_parse(&av_packet, &key_state, packet, sizeof(packet));
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);

	munit_assert(av_packet.is_video);
	munit_assert_uint16(av_packet.packet_index, ==, 45);
	munit_assert_uint16(av_packet.frame_index, ==, 5);
	// TODO: uses_nalu_info_structs
	munit_assert_uint16(av_packet.unit_index, ==, 6);
	munit_assert_uint16(av_packet.units_in_frame_total, ==, 8);
	munit_assert_uint16(av_packet.units_in_frame_fec, ==, 1);
	munit_assert_uint32(av_packet.codec, ==, 3);
//	munit_assert_uint16(av_packet.word_at_0x18, ==, 871);
	munit_assert_uint8(av_packet.adaptive_stream_index, ==, 0);
//	munit_assert_uint8(av_packet.byte_at_0x2c, ==, 0);

	munit_assert_ptr_equal(av_packet.data, packet + 0x15);
	munit_assert_size(av_packet.data_size, ==, 0x99);

	return MUNIT_OK;
}


static MunitResult test_av_packet_parse_real_video(const MunitParameter params[], void *user)
{
#include "takion_av_packet_parse_real_video.inl"
	return MUNIT_OK;
}

static void random_seqnums(ChiakiSeqNum32 *nums, size_t count)
{
	for(size_t i=0; i<count; i++)
	{
		ChiakiSeqNum32 seqnum;
		retry:
		seqnum = munit_rand_uint32();
		for(size_t j=0; j<i; j++)
		{
			if(nums[j] == seqnum)
				goto retry;
		}
		nums[i] = seqnum;
	}
}

static bool check_send_buffer_contents(ChiakiTakionSendBuffer *send_buffer, const ChiakiSeqNum32 *nums_expected, size_t nums_expected_count)
{
	// nums_expected must be unique

	if(chiaki_mutex_lock(&send_buffer->mutex) != CHIAKI_ERR_SUCCESS)
		return false;

	if(send_buffer->packets_count != nums_expected_count)
		goto fail;

	for(size_t i=0; i<nums_expected_count; i++)
	{
		bool found = false;
		for(size_t j=0; j<send_buffer->packets_count; j++)
		{
			if(send_buffer->packets[j].seq_num == nums_expected[i])
			{
				found = true;
				break;
			}
		}
		if(!found)
			goto fail;
	}

	chiaki_mutex_unlock(&send_buffer->mutex);
	return true;
fail:
	chiaki_mutex_unlock(&send_buffer->mutex);
	return false;
}

static void seqnums_ack(ChiakiSeqNum32 *nums, size_t *nums_count, ChiakiSeqNum32 ack_num)
{
	// simulate ack of ack_num
	for(size_t i=0; i<*nums_count; i++)
	{
		if(nums[i] == ack_num || chiaki_seq_num_32_lt(nums[i], ack_num))
		{
			for(size_t j=i+1; j<*nums_count; j++)
				nums[j-1] = nums[j];
			(*nums_count)--;
			i--;
		}
	}
}

static MunitResult test_takion_send_buffer(const MunitParameter params[], void *user)
{
#define nums_count 0x30
	ChiakiTakionSendBuffer send_buffer;
	ChiakiErrorCode err = chiaki_takion_send_buffer_init(&send_buffer, NULL, nums_count);
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
	send_buffer.log = get_test_log();

	ChiakiSeqNum32 nums_expected[nums_count + 1];
	random_seqnums(nums_expected, nums_count + 1);

	for(size_t i=0; i<nums_count; i++)
	{
		err = chiaki_takion_send_buffer_push(&send_buffer, nums_expected[i], malloc(8), 8);
		munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
	}

	err = chiaki_takion_send_buffer_push(&send_buffer, nums_expected[nums_count], malloc(8), 8);
	munit_assert_int(err, ==, CHIAKI_ERR_OVERFLOW);

	size_t nums_count_cur = nums_count;
	while(nums_count_cur > 0)
	{
		ChiakiSeqNum32 ack_num = nums_expected[nums_count_cur - 1]
				+ munit_rand_int_range(-1, 1) * munit_rand_int_range(1, 32);
		chiaki_takion_send_buffer_ack(&send_buffer, ack_num, NULL, NULL); // TODO: test acked seqnums params
		seqnums_ack(nums_expected, &nums_count_cur, ack_num);
		bool correct = check_send_buffer_contents(&send_buffer, nums_expected, nums_count_cur);
		munit_assert(correct);
	}

	chiaki_takion_send_buffer_fini(&send_buffer);
	return MUNIT_OK;
#undef nums_count
}

static MunitResult test_takion_format_congestion(const MunitParameter params[], void *user)
{
	static const uint8_t handshake_key[] = { 0x54, 0x65, 0x4c, 0x34, 0x5c, 0xac, 0x56, 0xb8, 0xea, 0xe6, 0x15, 0x2a, 0xde, 0x1c, 0xe2, 0xe8 };
	static const uint8_t ecdh_secret[] = { 0x00, 0x34, 0xf8, 0x21, 0xc7, 0xd9, 0xde, 0xa9, 0xe9, 0x11, 0xca, 0x5a, 0xd6, 0x7d, 0x11, 0xce, 0x4f, 0x02, 0xb1, 0xce, 0x1e, 0xe7, 0xc3, 0x8d, 0x54, 0x39, 0xfa, 0x64, 0xe3, 0xdb, 0xd8, 0x0d };

	ChiakiGKCrypt gkcrypt;
	ChiakiErrorCode err = chiaki_gkcrypt_init(&gkcrypt, NULL, 0, 2, handshake_key, ecdh_secret);
	if(err != CHIAKI_ERR_SUCCESS)
		return MUNIT_ERROR;

	ChiakiTakionCongestionPacket packet;
	packet.word_0 = 0x42;
	packet.received = 26;
	packet.lost = 10;

	const uint64_t key_pos = 0x1e5;

	uint8_t buf[CHIAKI_TAKION_CONGESTION_PACKET_SIZE];
	chiaki_takion_format_congestion(buf, &packet, key_pos);

	static const uint8_t buf_expected[] = { 0x05, 0x00, 0x42, 0x00, 0x1a, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xe5 };
	munit_assert_memory_equal(sizeof(buf), buf, buf_expected);

	err = chiaki_takion_packet_mac(&gkcrypt, buf, sizeof(buf), key_pos, NULL, NULL);
	if(err != CHIAKI_ERR_SUCCESS)
		return MUNIT_ERROR;

	static const uint8_t buf_expected_mac[] = { 0x05, 0x00, 0x42, 0x00, 0x1a, 0x00, 0x0a, 0x64, 0x8a, 0x7c, 0x74, 0x00, 0x00, 0x01, 0xe5 };
	munit_assert_memory_equal(sizeof(buf), buf, buf_expected_mac);

	chiaki_gkcrypt_fini(&gkcrypt);

	return MUNIT_OK;
}

#ifndef _WIN32
typedef struct takion_teardown_test_t
{
	ChiakiTakion takion;
	ChiakiMutex mutex;
	ChiakiCond cond;
	bool connected;
	bool disconnected;
} TakionTeardownTest;

static void takion_teardown_test_cb(ChiakiTakionEvent *event, void *user)
{
	TakionTeardownTest *test = user;
	chiaki_mutex_lock(&test->mutex);
	if(event->type == CHIAKI_TAKION_EVENT_TYPE_CONNECTED)
		test->connected = true;
	else if(event->type == CHIAKI_TAKION_EVENT_TYPE_DISCONNECT)
		test->disconnected = true;
	chiaki_cond_broadcast(&test->cond);
	chiaki_mutex_unlock(&test->mutex);
}

static bool takion_teardown_test_connected(void *user)
{
	return ((TakionTeardownTest *)user)->connected;
}

static bool takion_teardown_test_disconnected(void *user)
{
	return ((TakionTeardownTest *)user)->disconnected;
}

static void takion_test_write_header(uint8_t *buf, uint32_t tag, uint8_t chunk_type, size_t payload_size)
{
	memset(buf, 0, 1 + 0x10);
	buf[0] = 0; // control packet
	*((chiaki_unaligned_uint32_t *)(buf + 1)) = htonl(tag);
	buf[1 + 0xc] = chunk_type;
	*((chiaki_unaligned_uint16_t *)(buf + 1 + 0xe)) = htons((uint16_t)(payload_size + 4));
}
#endif

static MunitResult test_takion_send_after_receive_error(const MunitParameter params[], void *user)
{
#ifdef _WIN32
	return MUNIT_SKIP;
#else
	// Fake console: a UDP socket on localhost that completes the Takion handshake
	int peer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	munit_assert_int(peer, >=, 0);
	struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
	setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	struct sockaddr_in peer_addr = { 0 };
	peer_addr.sin_family = AF_INET;
	peer_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	munit_assert_int(bind(peer, (struct sockaddr *)&peer_addr, sizeof(peer_addr)), ==, 0);
	socklen_t peer_addr_len = sizeof(peer_addr);
	munit_assert_int(getsockname(peer, (struct sockaddr *)&peer_addr, &peer_addr_len), ==, 0);

	TakionTeardownTest test = { 0 };
	munit_assert_int(chiaki_mutex_init(&test.mutex, false), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_cond_init(&test.cond), ==, CHIAKI_ERR_SUCCESS);

	ChiakiTakionConnectInfo info = { 0 };
	info.log = get_test_log();
	info.sa = (struct sockaddr *)&peer_addr;
	info.sa_len = sizeof(peer_addr);
	info.cb = takion_teardown_test_cb;
	info.cb_user = &test;
	info.enable_crypt = false;
	info.protocol_version = 12;
	info.close_socket = false; // the caller owns the socket (like Senkusha on a PSN connection), so later sends reach the send buffer
	munit_assert_int(chiaki_takion_connect(&test.takion, &info, NULL), ==, CHIAKI_ERR_SUCCESS);

	uint8_t buf[0x100];
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	ssize_t r = recvfrom(peer, buf, sizeof(buf), 0, (struct sockaddr *)&client_addr, &client_addr_len);
	munit_assert_int(r, ==, 1 + 0x10 + 0x10); // INIT
	uint32_t tag_local = ntohl(*((chiaki_unaligned_uint32_t *)(buf + 1 + 0x10)));

	uint8_t init_ack[1 + 0x10 + 0x10 + 0x20] = { 0 };
	takion_test_write_header(init_ack, tag_local, 2, 0x10 + 0x20);
	uint8_t *pl = init_ack + 1 + 0x10;
	*((chiaki_unaligned_uint32_t *)(pl + 0)) = htonl(0x1337);
	*((chiaki_unaligned_uint32_t *)(pl + 4)) = htonl(0x19000);
	*((chiaki_unaligned_uint16_t *)(pl + 8)) = htons(0x64);
	*((chiaki_unaligned_uint16_t *)(pl + 0xa)) = htons(0x64);
	*((chiaki_unaligned_uint32_t *)(pl + 0xc)) = htonl(0x1337);
	munit_assert_int(sendto(peer, init_ack, sizeof(init_ack), 0, (struct sockaddr *)&client_addr, client_addr_len), ==, sizeof(init_ack));

	r = recvfrom(peer, buf, sizeof(buf), 0, NULL, NULL);
	munit_assert_int(r, ==, 1 + 0x10 + 0x20); // COOKIE

	uint8_t cookie_ack[1 + 0x10];
	takion_test_write_header(cookie_ack, tag_local, 0xb, 0);
	munit_assert_int(sendto(peer, cookie_ack, sizeof(cookie_ack), 0, (struct sockaddr *)&client_addr, client_addr_len), ==, sizeof(cookie_ack));

	chiaki_mutex_lock(&test.mutex);
	munit_assert_int(chiaki_cond_timedwait_pred(&test.cond, &test.mutex, 5000, takion_teardown_test_connected, &test), ==, CHIAKI_ERR_SUCCESS);
	chiaki_mutex_unlock(&test.mutex);

	// An empty datagram makes recv() return 0, so the Takion receive loop ends with an error
	munit_assert_int(sendto(peer, buf, 0, 0, (struct sockaddr *)&client_addr, client_addr_len), ==, 0);

	chiaki_mutex_lock(&test.mutex);
	munit_assert_int(chiaki_cond_timedwait_pred(&test.cond, &test.mutex, 5000, takion_teardown_test_disconnected, &test), ==, CHIAKI_ERR_SUCCESS);
	chiaki_mutex_unlock(&test.mutex);

	// The send buffer has been finalized now, but the socket is still open
	uint8_t msg[8] = { 0 };
	ChiakiErrorCode err = chiaki_takion_send_message_data(&test.takion, 1, 1, msg, sizeof(msg), NULL);

	chiaki_takion_close(&test.takion);
	CHIAKI_SOCKET_CLOSE(test.takion.sock);
	CHIAKI_SOCKET_CLOSE(peer);
	chiaki_cond_fini(&test.cond);
	chiaki_mutex_fini(&test.mutex);

	munit_assert_int(err, ==, CHIAKI_ERR_DISCONNECTED);
	return MUNIT_OK;
#endif
}

MunitTest tests_takion[] = {
	{
		"/av_packet_parse",
		test_av_packet_parse,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/av_packet_parse_real_video",
		test_av_packet_parse_real_video,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/send_buffer",
		test_takion_send_buffer,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/send_after_receive_error",
		test_takion_send_after_receive_error,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/format_congestion",
		test_takion_format_congestion,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
