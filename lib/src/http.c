// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/http.h>

#include <stdbool.h>
#include <string.h>
#include <chiaki/remote/rudp.h>
#include <chiaki/log.h>
#include <chiaki/time.h>

#if _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

CHIAKI_EXPORT void chiaki_http_header_free(ChiakiHttpHeader *header)
{
	while(header)
	{
		ChiakiHttpHeader *cur = header;
		header = header->next;
		free(cur);
	}
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_http_header_parse(ChiakiHttpHeader **header, char *buf, size_t buf_size)
{
	*header = NULL;
#define FAIL(reason) do { chiaki_http_header_free(*header); return (reason); } while(0);
	char *key_ptr = buf;
	char *value_ptr = NULL;

	for(char *end = buf + buf_size; buf<end; buf++)
	{
		char c = *buf;
		if(!c)
			break;

		if(!value_ptr)
		{
			if(c == ':')
			{
				if(key_ptr == buf)
					FAIL(CHIAKI_ERR_INVALID_DATA);
				*buf = '\0';
				buf++;
				if(buf == end)
					FAIL(CHIAKI_ERR_INVALID_DATA);
				if(*buf == ' ')
					buf++;
				if(buf == end)
					FAIL(CHIAKI_ERR_INVALID_DATA);
				value_ptr = buf;
			}
			else if(c == '\r' || c == '\n')
			{
				if(key_ptr + 1 < buf) // no : encountered yet
					FAIL(CHIAKI_ERR_INVALID_DATA);
				key_ptr = buf + 1;
			}
		}
		else
		{
			if(c == '\r' || c == '\n')
			{
				if(value_ptr == buf) // empty value
					FAIL(CHIAKI_ERR_INVALID_DATA);

				*buf = '\0';
				ChiakiHttpHeader *entry = malloc(sizeof(ChiakiHttpHeader));
				if(!entry)
					FAIL(CHIAKI_ERR_MEMORY);
				entry->key = key_ptr;
				entry->value = value_ptr;
				entry->next = *header;
				*header = entry;

				key_ptr = buf + 1;
				value_ptr = NULL;
			}
		}
	}
	return CHIAKI_ERR_SUCCESS;
#undef FAIL
}

CHIAKI_EXPORT void chiaki_http_response_fini(ChiakiHttpResponse *response)
{
	if(!response)
		return;
	chiaki_http_header_free(response->headers);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_http_response_parse(ChiakiHttpResponse *response, char *buf, size_t buf_size)
{
	static const char *http_version = "HTTP/1.1 ";
	static const size_t http_version_size = 9;

	if(buf_size < http_version_size)
		return CHIAKI_ERR_INVALID_DATA;

	if(strncmp(buf, http_version, http_version_size) != 0)
		return CHIAKI_ERR_INVALID_DATA;

	buf += http_version_size;
	buf_size -= http_version_size;

	char *line_end = memchr(buf, '\n', buf_size);
	if(!line_end)
		return CHIAKI_ERR_INVALID_DATA;
	size_t line_length = (line_end - buf) + 1;
	if(buf_size <= line_length)
		return CHIAKI_ERR_INVALID_DATA;
	if(line_length > 1 && *(line_end - 1) == '\r')
		*(line_end - 1) = '\0';
	else
		*line_end = '\0';

	char *endptr;
	response->code = (int)strtol(buf, &endptr, 10);
	if(response->code == 0)
		return CHIAKI_ERR_INVALID_DATA;

	buf += line_length;
	buf_size -= line_length;

	return chiaki_http_header_parse(&response->headers, buf, buf_size);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_recv_http_header(int sock, char *buf, size_t buf_size, size_t *header_size, size_t *received_size, ChiakiStopPipe *stop_pipe, uint64_t timeout_ms)
{
	// 0 = ""
	// 1 = "\r"
	// 2 = "\r\n"
	// 3 = "\r\n\r"
	// 4 = "\r\n\r\n" (final)
	int nl_state = 0;
	static const int transitions_r[] = { 1, 1, 3, 1 };
	static const int transitions_n[] = { 0, 2, 0, 4 };
	uint64_t deadline_ms = timeout_ms == UINT64_MAX ? UINT64_MAX : chiaki_time_now_monotonic_ms() + timeout_ms;

	char *start = buf;
	*received_size = 0;
	while(true)
	{
		if(*received_size >= buf_size)
			return CHIAKI_ERR_BUF_TOO_SMALL;

		if(stop_pipe)
		{
			uint64_t wait_timeout_ms = UINT64_MAX;
			if(deadline_ms != UINT64_MAX)
			{
				uint64_t now_ms = chiaki_time_now_monotonic_ms();
				if(now_ms >= deadline_ms)
					return CHIAKI_ERR_TIMEOUT;
				wait_timeout_ms = deadline_ms - now_ms;
			}
			ChiakiErrorCode err = chiaki_stop_pipe_select_single(stop_pipe, sock, false, wait_timeout_ms);
			if(err != CHIAKI_ERR_SUCCESS)
				return err;
		}

		CHIAKI_SSIZET_TYPE received;
		do
		{
			received = recv(sock, start + *received_size, (int)(buf_size - *received_size), 0);
#if _WIN32
		} while(false);
#else
		} while(received < 0 && errno == EINTR);
#endif
		if(received < 0)
		{
#ifdef _WIN32
			int err = WSAGetLastError();
			if(err == WSAEWOULDBLOCK)
				continue;
#else
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
#endif
		}
		if(received <= 0)
			return received == 0 ? CHIAKI_ERR_DISCONNECTED : CHIAKI_ERR_NETWORK;

		*received_size += received;
		buf = start + (*received_size - received);
		for(; received > 0; buf++, received--)
		{
			switch(*buf)
			{
				case '\r':
					nl_state = transitions_r[nl_state];
					break;
				case '\n':
					nl_state = transitions_n[nl_state];
					break;
				default:
					nl_state = 0;
					break;
			}
			if(nl_state == 4)
			{
				received--;
				break;
			}
		}

		if(nl_state == 4)
		{
			*header_size = *received_size - received;
			break;
		}
	}

	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_send_recv_http_header_psn(ChiakiRudp rudp, ChiakiLog *log,
	uint16_t *remote_counter, char *send_buf, size_t send_buf_size,
	char *buf, size_t buf_size, size_t *header_size, size_t *received_size)
{
	// 0 = ""
	// 1 = "\r"
	// 2 = "\r\n"
	// 3 = "\r\n\r"
	// 4 = "\r\n\r\n" (final)
	int nl_state = 0;
	static const int transitions_r[] = { 1, 1, 3, 1 };
	static const int transitions_n[] = { 0, 2, 0, 4 };

	*received_size = 0;
	size_t received;
	RudpMessage message;
	ChiakiErrorCode err;
	err = chiaki_rudp_send_recv(rudp, &message, (uint8_t *)send_buf, send_buf_size, *remote_counter, SESSION_MESSAGE, CTRL_MESSAGE, 2, 3);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "Didn't receive http session message response");
		return err;
	}
	received = message.data_size - 2;
	if(received > buf_size)
	{
		CHIAKI_LOGE(log, "Http session message response of %zu bytes doesn't fit in %zu", received, buf_size);
		chiaki_rudp_message_pointers_free(&message);
		return CHIAKI_ERR_BUF_TOO_SMALL;
	}
	memcpy(buf, message.data + 2, received);
	*remote_counter = message.remote_counter;
	chiaki_rudp_message_pointers_free(&message);

	if(received == 0)
		return CHIAKI_ERR_DISCONNECTED;

	*received_size += received;
	for(; received > 0; buf++, received--)
	{
		switch(*buf)
		{
			case '\r':
				nl_state = transitions_r[nl_state];
				break;
			case '\n':
				nl_state = transitions_n[nl_state];
				break;
			default:
				nl_state = 0;
				break;
		}
		if(nl_state == 4)
		{
			received--;
			break;
		}
	}

	if(nl_state != 4)
	{
		CHIAKI_LOGE(log, "Http session message response has no complete header");
		return CHIAKI_ERR_INVALID_RESPONSE;
	}
	*header_size = *received_size - received;

	return CHIAKI_ERR_SUCCESS;
}
