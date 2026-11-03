// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/http.h>
#include <stdio.h>

static char * const response_crlf =
		"HTTP/1.1 200 OK\r\n"
		"Content-type: text/html, text, plain\r\n"
		"Ultimate Ability: Gamer\r\n"
		"\r\n";

static char * const response_lf =
		"HTTP/1.1 200 Ok\n"
		"Content-type: text/html, text, plain\n"
		"Ultimate Ability:Gamer\n";

static void *test_http_response_parse_setup(const MunitParameter params[], void *user)
{
	const char *response = NULL;
	if(strcmp(params[0].value, "crlf") == 0)
		response = response_crlf;
	else if(strcmp(params[0].value, "lf") == 0)
		response = response_lf;
	return response ? strdup(response) : NULL;
}

static void test_http_response_parse_teardown(void *fixture)
{
	free(fixture);
}

static MunitResult test_http_response_parse(const MunitParameter params[], void *fixture)
{
	char *buf = fixture;
	ChiakiHttpResponse parsed_response;
	ChiakiErrorCode err = chiaki_http_response_parse(&parsed_response, buf, strlen(buf));
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(parsed_response.code, ==, 200);

	ChiakiHttpHeader *header = parsed_response.headers;
	munit_assert_ptr_not_null(header);
	munit_assert_string_equal(header->key, "Ultimate Ability");
	munit_assert_string_equal(header->value, "Gamer");

	header = header->next;
	munit_assert_ptr_not_null(header);
	munit_assert_string_equal(header->key, "Content-type");
	munit_assert_string_equal(header->value, "text/html, text, plain");

	header = header->next;
	munit_assert_ptr_null(header);

	chiaki_http_response_fini(&parsed_response);
	return MUNIT_OK;
}

static char *response_params[] = {
		"crlf", "lf", NULL
};

static MunitParameterEnum params[] = {
		{ "line ending", response_params },
		{ NULL, NULL }
};

#ifndef _WIN32
#include <chiaki/thread.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct
{
	int fd;
	const char *data;
} DelayedWrite;

static void *delayed_write(void *arg)
{
	DelayedWrite *write_info = arg;
	// Long enough that the receiver has already read the first part on its own
	usleep(100 * 1000);
	size_t size = strlen(write_info->data);
	return (void *)(intptr_t)(write(write_info->fd, write_info->data, size) == (ssize_t)size);
}

static MunitResult test_http_recv_header_split(const MunitParameter params[], void *user)
{
	int fds[2];
	munit_assert_int(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), ==, 0);
	const char *part1 = "HTTP/1.1 200 OK\r\n";
	const char *part2 = "Content-Length: 0\r\n\r\nBODY";
	const char *header_end = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
	munit_assert_int(write(fds[1], part1, strlen(part1)), ==, (int)strlen(part1));
	// Send the rest from another thread after a delay, so the header arrives over two reads
	DelayedWrite second = { fds[1], part2 };
	ChiakiThread writer;
	munit_assert_int(chiaki_thread_create(&writer, delayed_write, &second), ==, CHIAKI_ERR_SUCCESS);
	char buf[256];
	size_t header_size = 0, received_size = 0;
	ChiakiErrorCode err = chiaki_recv_http_header(fds[0], buf, sizeof(buf), &header_size, &received_size, NULL, 1000);
	void *wrote = NULL;
	munit_assert_int(chiaki_thread_join(&writer, &wrote), ==, CHIAKI_ERR_SUCCESS);
	close(fds[0]);
	close(fds[1]);
	munit_assert_true(wrote != NULL);
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(header_size, ==, strlen(header_end));
	munit_assert_size(received_size, ==, strlen(part1) + strlen(part2));
	munit_assert_memory_equal(strlen(part1), buf, part1);
	munit_assert_memory_equal(strlen(part2), buf + strlen(part1), part2);
	return MUNIT_OK;
}

static MunitResult test_http_recv_header_too_long(const MunitParameter params[], void *user)
{
	int fds[2];
	munit_assert_int(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), ==, 0);
	uint8_t raw[256];
	memset(raw, 0xA5, sizeof(raw));
	uint8_t msg[200];
	memset(msg, 'a', sizeof(msg));
	memcpy(msg + 146, "\r\n\r\n", 4); // terminator ends at byte 150
	munit_assert_int(write(fds[1], msg, sizeof(msg)), ==, (int)sizeof(msg));
	size_t header_size = 0, received_size = 0;
	ChiakiErrorCode err = chiaki_recv_http_header(fds[0], (char *)raw + 64, 128, &header_size, &received_size, NULL, 1000);
	close(fds[0]);
	close(fds[1]);
	munit_assert_int(err, ==, CHIAKI_ERR_BUF_TOO_SMALL);
	for(size_t i = 0; i < 64; i++)
	{
		munit_assert_uint8(raw[i], ==, 0xA5);
		munit_assert_uint8(raw[192 + i], ==, 0xA5);
	}
	return MUNIT_OK;
}
#endif

MunitTest tests_http[] = {
	{
		"/response_parse",
		test_http_response_parse,
		test_http_response_parse_setup,
		test_http_response_parse_teardown,
		MUNIT_TEST_OPTION_NONE,
		params
	},
#ifndef _WIN32
	{
		"/recv_http_header_split",
		test_http_recv_header_split,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/recv_http_header_too_long",
		test_http_recv_header_too_long,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
#endif
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};