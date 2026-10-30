// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "utils.h"

#include <chiaki/regist.h>
#include <chiaki/rpcrypt.h>
#include <chiaki/http.h>
#include <chiaki/random.h>
#include <chiaki/time.h>
#include <chiaki/base64.h>
#include <chiaki/session.h>

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <inttypes.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef uint32_t in_addr_t;
#else
#include <netdb.h>
#endif

#define REGIST_PORT 9295

#define SEARCH_REQUEST_SLEEP_MS 100
#define REGIST_SEARCH_TIMEOUT_MS 3000
#define REGIST_REPONSE_TIMEOUT_MS 3000

static void *regist_thread_func(void *user);
static ChiakiErrorCode regist_search(ChiakiRegist *regist, struct addrinfo *addrinfos, struct sockaddr *recv_addr, socklen_t *recv_addr_size);
static chiaki_socket_t regist_search_connect(ChiakiRegist *regist, struct addrinfo *addrinfos, struct sockaddr *send_addr, socklen_t *send_addr_len);
static ChiakiErrorCode regist_request_connect(ChiakiRegist *regist, const struct sockaddr *addr, size_t addr_len, chiaki_socket_t *sock_out);
static ChiakiErrorCode regist_recv_response(ChiakiRegist *regist, ChiakiRegisteredHost *host, chiaki_socket_t sock, ChiakiRPCrypt *rpcrypt, uint16_t remote_counter, char *send_buf, size_t send_buf_size);
static ChiakiErrorCode regist_parse_response_payload(ChiakiRegist *regist, ChiakiRegisteredHost *host, char *buf, size_t buf_size);

CHIAKI_EXPORT ChiakiErrorCode chiaki_regist_start(ChiakiRegist *regist, ChiakiLog *log, const ChiakiRegistInfo *info, ChiakiRegistCb cb, void *cb_user)
{
	regist->log = log;
	regist->info = *info;
	if(!regist->info.holepunch_info)
	{
		regist->info.host = strdup(regist->info.host);
		if(!regist->info.host)
			return CHIAKI_ERR_MEMORY;
	}

	ChiakiErrorCode err = CHIAKI_ERR_UNKNOWN;
	if(regist->info.psn_online_id)
	{
		regist->info.psn_online_id = strdup(regist->info.psn_online_id);
		if(!regist->info.psn_online_id)
		{
			err = CHIAKI_ERR_MEMORY;
			goto error_host;
		}
	}

	regist->cb = cb;
	regist->cb_user = cb_user;

	err = chiaki_stop_pipe_init(&regist->stop_pipe);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_psn_id;

	err = chiaki_thread_create(&regist->thread, regist_thread_func, regist);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_stop_pipe;

	return CHIAKI_ERR_SUCCESS;

error_stop_pipe:
	chiaki_stop_pipe_fini(&regist->stop_pipe);
error_psn_id:
	free((char *)regist->info.psn_online_id);
error_host:
	free((char *)regist->info.host);
	return err;
}

CHIAKI_EXPORT void chiaki_regist_fini(ChiakiRegist *regist)
{
	chiaki_thread_join(&regist->thread, NULL);
	chiaki_stop_pipe_fini(&regist->stop_pipe);
	free((char *)regist->info.psn_online_id);
	free((char *)regist->info.host);
}

CHIAKI_EXPORT void chiaki_regist_stop(ChiakiRegist *regist)
{
	chiaki_stop_pipe_stop(&regist->stop_pipe);
}

static void regist_event_simple(ChiakiRegist *regist, ChiakiRegistEventType type)
{
	ChiakiRegistEvent event = { 0 };
	event.type = type;
	regist->cb(&event, regist->cb_user);
}

static const char *const request_head_fmt =
	"POST %s HTTP/1.1\r\n HTTP/1.1\r\n"
	"HOST: %s\r\n"
	"User-Agent: remoteplay Windows\r\n"
	"Connection: close\r\n"
	"Content-Length: %llu\r\n";

static const char *request_path_ps5 = "/sie/ps5/rp/sess/rgst";
static const char *request_path_ps4 = "/sie/ps4/rp/sess/rgst";
static const char *request_path_ps4_pre10 = "/sce/rp/regist";

static const char *request_path(ChiakiTarget target)
{
	switch(target)
	{
		case CHIAKI_TARGET_PS5_UNKNOWN:
		case CHIAKI_TARGET_PS5_1:
			return request_path_ps5;
		case CHIAKI_TARGET_PS4_8:
		case CHIAKI_TARGET_PS4_9:
			return request_path_ps4_pre10;
		default:
			return request_path_ps4;
	}
}

static const char *const request_rp_version_fmt = "RP-Version: %s\r\n";

static const char *const request_tail = "\r\n";

const char *client_type = "dabfa2ec873de5839bee8d3f4c0239c4282c07c25c6077a2931afcf0adc0d34f";
const char *client_type_ps4_pre10 = "Windows";

static const char *const request_inner_account_id_fmt =
	"Client-Type: %s\r\n"
	"Np-AccountId: %s\r\n";

static const char *const request_inner_online_id_fmt =
	"Client-Type: Windows\r\n"
	"Np-Online-Id: %s\r\n";


static int request_header_format(char *buf, size_t buf_size, size_t payload_size, ChiakiTarget target, char *regist_local_addr)
{
	int cur = snprintf(buf, buf_size, request_head_fmt, request_path(target),
			regist_local_addr, (unsigned long long)payload_size);
	if(cur < 0 || cur >= payload_size)
		return -1;
	if(target >= CHIAKI_TARGET_PS4_9)
	{
		const char *rp_version_str = chiaki_rp_version_string(target);
		size_t s = buf_size - cur;
		int r = snprintf(buf + cur, s, request_rp_version_fmt, rp_version_str);
		if(r < 0 || r >= s)
			return -1;
		cur += r;
	}
	size_t tail_size = strlen(request_tail) + 1;
	if(cur + tail_size > payload_size)
		return -1;
	memcpy(buf + cur, request_tail, tail_size);
	cur += (int)tail_size - 1;
	return cur;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_regist_request_payload_format(ChiakiTarget target, const uint8_t *ambassador, uint8_t *buf, size_t *buf_size, ChiakiRPCrypt *crypt, const char *psn_online_id, const uint8_t *psn_account_id, uint32_t pin, ChiakiHolepunchRegistInfo *holepunch_info)
{
	size_t buf_size_val = *buf_size;
	static const size_t inner_header_off = 0x1e0;
	if(buf_size_val < inner_header_off)
		return CHIAKI_ERR_BUF_TOO_SMALL;
	memset(buf, 'A', inner_header_off); // can be random

	if(target < CHIAKI_TARGET_PS4_10)
	{
		chiaki_rpcrypt_init_regist_ps4_pre10(crypt, ambassador, pin);
		chiaki_rpcrypt_aeropause_ps4_pre10(buf + 0x11c, crypt->ambassador);
	}
	else
	{
		size_t key_0_off = buf[0x18D] & 0x1F;
		size_t key_1_off = buf[0] >> 3;
		uint8_t aeropause[0x10];
		ChiakiErrorCode err;
		if(holepunch_info)
		{
			err = chiaki_rpcrypt_init_regist_psn(crypt, target, ambassador, key_0_off, holepunch_info->custom_data1, holepunch_info->data1, holepunch_info->data2);
			if(err != CHIAKI_ERR_SUCCESS)
				return err;
			err = chiaki_rpcrypt_aeropause_psn(target, key_1_off, aeropause, crypt->ambassador);
		}
		else
		{
			err = chiaki_rpcrypt_init_regist(crypt, target, ambassador, key_0_off, pin);
			if(err != CHIAKI_ERR_SUCCESS)
				return err;
			err = chiaki_rpcrypt_aeropause(target, key_1_off, aeropause, crypt->ambassador);
		}
		if(err != CHIAKI_ERR_SUCCESS)
			return err;
		memcpy(buf + 0xc7, aeropause + 8, 8);
		memcpy(buf + 0x191, aeropause, 8);
		psn_online_id = NULL; // don't need this
	}

	int inner_header_size;
	if(psn_online_id)
		inner_header_size = snprintf((char *)buf + inner_header_off, buf_size_val - inner_header_off, request_inner_online_id_fmt, psn_online_id);
	else if(psn_account_id)
	{
		char account_id_b64[CHIAKI_PSN_ACCOUNT_ID_SIZE * 2];
		ChiakiErrorCode err = chiaki_base64_encode(psn_account_id, CHIAKI_PSN_ACCOUNT_ID_SIZE, account_id_b64, sizeof(account_id_b64));
		if(err != CHIAKI_ERR_SUCCESS)
			return err;
		inner_header_size = snprintf((char *)buf + inner_header_off, buf_size_val - inner_header_off,
				request_inner_account_id_fmt,
				target < CHIAKI_TARGET_PS4_10 ? client_type_ps4_pre10 : client_type,
				account_id_b64);
	}
	else
		return CHIAKI_ERR_INVALID_DATA;
	if(inner_header_size < 0 || inner_header_size >= buf_size_val - inner_header_off)
		return CHIAKI_ERR_BUF_TOO_SMALL;
	ChiakiErrorCode err = chiaki_rpcrypt_encrypt(crypt, 0, buf + inner_header_off, buf + inner_header_off, inner_header_size);
	*buf_size = inner_header_off + inner_header_size;
	return err;
}

static void *regist_thread_func(void *user)
{
	ChiakiRegist *regist = user;
	chiaki_thread_set_affinity(CHIAKI_THREAD_NAME_REGIST);

	bool canceled = false;
	bool success = false;
	bool psn = false;
	// if holepunch info is filled out, this is a psn regist
	if(regist->info.holepunch_info)
		psn = true;

	ChiakiRPCrypt crypt;
	uint8_t ambassador[CHIAKI_RPCRYPT_KEY_SIZE];
	ChiakiErrorCode err = chiaki_random_bytes_crypt(ambassador, sizeof(ambassador));
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(regist->log, "Regist failed to generate random ambassador");
		goto fail;
	}

	uint8_t payload[0x400];
	size_t payload_size = sizeof(payload);
	err = chiaki_regist_request_payload_format(regist->info.target, ambassador, payload, &payload_size, &crypt, regist->info.psn_online_id, regist->info.psn_account_id, regist->info.pin, regist->info.holepunch_info);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(regist->log, "Regist failed to format payload");
		goto fail;
	}

	char request_header[0x100];
	// random local addr if our local addr is not provided
	char regist_local_addr[INET6_ADDRSTRLEN] = "10.0.2.15";
	if(regist->info.holepunch_info)
		memcpy(regist_local_addr, regist->info.holepunch_info->regist_local_ip, sizeof(regist_local_addr));
	int request_header_size = request_header_format(request_header, sizeof(request_header), payload_size, regist->info.target, regist_local_addr);

	if(request_header_size < 0 || request_header_size >= sizeof(request_header))
	{
		CHIAKI_LOGE(regist->log, "Regist failed to format request");
		goto fail;
	}

	CHIAKI_LOGV(regist->log, "Regist formatted request header:");
	chiaki_log_hexdump(regist->log, CHIAKI_LOG_VERBOSE, (uint8_t *)request_header, request_header_size);

	chiaki_socket_t sock = CHIAKI_INVALID_SOCKET;
	uint16_t remote_counter = 0;
	struct addrinfo *addrinfos;
	if(psn)
	{
		CHIAKI_LOGI(regist->log, "REGIST - Starting RUDP session");
		RudpMessage message;
		err = chiaki_rudp_send_recv(regist->info.rudp, &message, NULL, 0, 0, INIT_REQUEST, INIT_RESPONSE, 8, 3);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(regist->log, "REGIST - Failed to init rudp");
			goto fail;
		}
		size_t init_response_size = message.data_size - 8;
		uint8_t init_response[init_response_size];
		memcpy(init_response, message.data + 8, init_response_size);
		chiaki_rudp_message_pointers_free(&message);
		err = chiaki_rudp_send_recv(regist->info.rudp, &message, init_response, init_response_size, 0, COOKIE_REQUEST, COOKIE_RESPONSE, 2, 3);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(regist->log, "REGIST - Failed to pass rudp cookie");
			goto fail;
		}
		remote_counter = message.remote_counter;
		chiaki_rudp_message_pointers_free(&message);
	}
	else
	{
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_socktype = SOCK_DGRAM;
		char *ipv6 = strchr(regist->info.host, ':');
		if(ipv6)
			hints.ai_family = AF_INET6;
		else
			hints.ai_family = AF_INET;
		int r = getaddrinfo(regist->info.host, NULL, &hints, &addrinfos);
		if(r != 0)
		{
			CHIAKI_LOGE(regist->log, "Regist failed to getaddrinfo on %s", regist->info.host);
			goto fail;
		}

		struct sockaddr_storage recv_addr = { 0 };
		socklen_t recv_addr_size;
		recv_addr_size = sizeof(recv_addr);
		err = regist_search(regist, addrinfos, (struct sockaddr *)&recv_addr, &recv_addr_size);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			if(err == CHIAKI_ERR_CANCELED)
				canceled = true;
			else
				CHIAKI_LOGE(regist->log, "Regist search failed");
			goto fail_addrinfos;
		}

		err = chiaki_stop_pipe_sleep(&regist->stop_pipe, SEARCH_REQUEST_SLEEP_MS); // PS4 doesn't accept requests immediately
		if(err != CHIAKI_ERR_TIMEOUT)
		{
			canceled = true;
			goto fail_addrinfos;
		}

		err = regist_request_connect(regist, (struct sockaddr *)&recv_addr, recv_addr_size, &sock);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			if(err == CHIAKI_ERR_CANCELED)
				canceled = true;
			else
				CHIAKI_LOGE(regist->log, "Regist eventually failed to connect for request");
			goto fail_addrinfos;
		}
		CHIAKI_LOGI(regist->log, "Regist connected to %s, sending request", regist->info.host);
	}
	if(!psn)
	{
		ChiakiErrorCode send_err = chiaki_send_fully(&regist->stop_pipe, sock, (const uint8_t *)request_header, request_header_size, REGIST_REPONSE_TIMEOUT_MS);
		if(send_err != CHIAKI_ERR_SUCCESS)
		{
			if(send_err == CHIAKI_ERR_CANCELED)
			{
				canceled = true;
				CHIAKI_LOGI(regist->log, "Regist canceled while sending request header");
			}
			else
			{
#ifdef _WIN32
				CHIAKI_LOGE(regist->log, "Regist failed to send request header: %u", WSAGetLastError());
#else
				CHIAKI_LOGE(regist->log, "Regist failed to send request header: %s", strerror(errno));
#endif
			}
			goto fail_socket;
		}
	}
	char *send_buf = NULL;
	size_t send_buf_size = 0;
	if(psn)
	{
		send_buf_size = request_header_size + payload_size;
		send_buf = calloc(send_buf_size, sizeof(char));
		memcpy(send_buf, request_header, request_header_size);
		memcpy(send_buf + request_header_size, payload, payload_size);
	}
	else
	{
		ChiakiErrorCode send_err = chiaki_send_fully(&regist->stop_pipe, sock, payload, payload_size, REGIST_REPONSE_TIMEOUT_MS);
		if(send_err != CHIAKI_ERR_SUCCESS)
		{
			if(send_err == CHIAKI_ERR_CANCELED)
			{
				canceled = true;
				CHIAKI_LOGI(regist->log, "Regist canceled while sending payload");
			}
			else
			{
#ifdef _WIN32
				CHIAKI_LOGE(regist->log, "Regist failed to send payload: %u", WSAGetLastError());
#else
				CHIAKI_LOGE(regist->log, "Regist failed to send payload: %s", strerror(errno));
#endif
			}
			goto fail_socket;
		}
		CHIAKI_LOGI(regist->log, "Regist waiting for response");
	}


	ChiakiRegisteredHost host;
	err = regist_recv_response(regist, &host, sock, &crypt, remote_counter, send_buf, send_buf_size);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		if(err == CHIAKI_ERR_CANCELED)
			canceled = true;
		else
			CHIAKI_LOGE(regist->log, "Regist eventually failed");
		goto fail_socket;
	}

	CHIAKI_LOGI(regist->log, "Regist successfully received response");

	success = true;

fail_socket:
	if(!psn)
	{
		if(!CHIAKI_SOCKET_IS_INVALID(sock))
		{
			CHIAKI_SOCKET_CLOSE(sock);
			sock = CHIAKI_INVALID_SOCKET;
		}
	}
fail_addrinfos:
	if(!psn)
		freeaddrinfo(addrinfos);
fail:
	if(canceled)
	{
		CHIAKI_LOGI(regist->log, "Regist canceled");
		regist_event_simple(regist, CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED);
	}
	else if(success)
	{
		host.console_pin = regist->info.console_pin;
		ChiakiRegistEvent event = { 0 };
		event.type = CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS;
		event.registered_host = &host;
		regist->cb(&event, regist->cb_user);
	}
	else
		regist_event_simple(regist, CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED);
	return NULL;
}

static ChiakiErrorCode regist_search(ChiakiRegist *regist, struct addrinfo *addrinfos, struct sockaddr *recv_addr, socklen_t *recv_addr_size)
{
	CHIAKI_LOGI(regist->log, "Regist starting search");
	struct sockaddr_storage send_addr;
	socklen_t send_addr_len = sizeof(send_addr);
	chiaki_socket_t sock = regist_search_connect(regist, addrinfos, (struct sockaddr *)&send_addr, &send_addr_len);
	if(CHIAKI_SOCKET_IS_INVALID(sock))
	{
		CHIAKI_LOGE(regist->log, "Regist eventually failed to connect for search");
		return CHIAKI_ERR_NETWORK;
	}

	ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

	const char *src = chiaki_target_is_ps5(regist->info.target) ? "SRC3" : "SRC2";
	const char *res = chiaki_target_is_ps5(regist->info.target) ? "RES3" : "RES2";
	size_t res_size = strlen(res);

	CHIAKI_LOGI(regist->log, "Regist sending search packet");
	int r;
	if(regist->info.broadcast)
		r = sendto_broadcast(regist->log, sock, src, strlen(src) + 1, 0, (struct sockaddr *)&send_addr, send_addr_len);
	else
		r = send(sock, src, strlen(src) + 1, 0);
	if(r < 0)
	{
		CHIAKI_LOGE(regist->log, "Regist failed to send search: %s", strerror(errno));
		err = CHIAKI_ERR_NETWORK;
		goto done;
	}

	uint64_t timeout_abs_ms = chiaki_time_now_monotonic_ms() + REGIST_SEARCH_TIMEOUT_MS;
	while(true)
	{
		uint64_t now_ms = chiaki_time_now_monotonic_ms();
		if(now_ms > timeout_abs_ms)
			err = CHIAKI_ERR_TIMEOUT;
		else
			err = chiaki_stop_pipe_select_single(&regist->stop_pipe, sock, false, timeout_abs_ms - now_ms);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			if(err == CHIAKI_ERR_TIMEOUT)
				CHIAKI_LOGE(regist->log, "Regist timed out waiting for search response");
			break;
		}

		uint8_t buf[0x100];
		CHIAKI_SSIZET_TYPE n = recvfrom(sock, (CHIAKI_SOCKET_BUF_TYPE)buf, sizeof(buf) - 1, 0, recv_addr, recv_addr_size);
		if(n <= 0)
		{
			if(n < 0)
				CHIAKI_LOGE(regist->log, "Regist failed to receive search response: %s", strerror(errno));
			else
				CHIAKI_LOGE(regist->log, "Regist failed to receive search response");
			err = CHIAKI_ERR_NETWORK;
			goto done;
		}

		CHIAKI_LOGV(regist->log, "Regist received packet: %zd >= %zu", n, res_size);
		chiaki_log_hexdump(regist->log, CHIAKI_LOG_VERBOSE, buf, n);

		if(n >= res_size && !memcmp(buf, res, res_size))
		{
			char addr[64];
			const char *addr_str = sockaddr_str(recv_addr, addr, sizeof(addr));
			CHIAKI_LOGI(regist->log, "Regist received search response from %s", addr_str ? addr_str : "");
			break;
		}
	}

done:
	if(!CHIAKI_SOCKET_IS_INVALID(sock))
	{
		CHIAKI_SOCKET_CLOSE(sock);
		sock = CHIAKI_INVALID_SOCKET;
	}
	return err;
}

static chiaki_socket_t regist_search_connect(ChiakiRegist *regist, struct addrinfo *addrinfos, struct sockaddr *send_addr, socklen_t *send_addr_len)
{
	chiaki_socket_t sock = CHIAKI_INVALID_SOCKET;
	bool connected = false;
	for(struct addrinfo *ai=addrinfos; ai; ai=ai->ai_next)
	{
		//if(ai->ai_protocol != IPPROTO_UDP)
		//	continue;

		if(ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
			continue;

		if(ai->ai_addrlen > *send_addr_len)
			continue;
		memcpy(send_addr, ai->ai_addr, ai->ai_addrlen);
		*send_addr_len = ai->ai_addrlen;

		set_port(send_addr, htons(REGIST_PORT));

		sock = socket(send_addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
		if(CHIAKI_SOCKET_IS_INVALID(sock))
		{
			CHIAKI_LOGE(regist->log, "Regist failed to create socket for search");
			continue;
		}
		int r = 0;
		if(regist->info.broadcast)
		{
			const int broadcast = 1;
			if(send_addr->sa_family == AF_INET)
			{
				r = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const CHIAKI_SOCKET_BUF_TYPE)&broadcast, sizeof(broadcast));
				if(r < 0)
				{
#ifdef _WIN32
					CHIAKI_LOGE(regist->log, "Regist failed to setsockopt SO_BROADCAST, error %u", WSAGetLastError());
#else
					CHIAKI_LOGE(regist->log, "Regist failed to setsockopt SO_BROADCAST");
#endif
					goto connect_fail;
				}
			}
			// Bind to a separate wildcard address on an ephemeral port so send_addr keeps the
			// destination port. Zeroing the port inside send_addr sent the search request to
			// port 0, so the console never received it.
			struct sockaddr_storage bind_addr;
			memset(&bind_addr, 0, sizeof(bind_addr));
			if(send_addr->sa_family == AF_INET)
			{
				struct sockaddr_in *bind_in = (struct sockaddr_in *)&bind_addr;
				bind_in->sin_family = AF_INET;
				bind_in->sin_addr.s_addr = htonl(INADDR_ANY);
			}
			else
			{
				struct sockaddr_in6 *bind_in6 = (struct sockaddr_in6 *)&bind_addr;
				bind_in6->sin6_family = AF_INET6;
				bind_in6->sin6_addr = in6addr_any;
			}
			r = bind(sock, (struct sockaddr *)&bind_addr, *send_addr_len);
			if(r < 0)
			{
				CHIAKI_LOGE(regist->log, "Regist failed to bind socket, trying next address...");
				continue;
			}
		}
		else
		{
			int r = connect(sock, send_addr, *send_addr_len);
			if(r < 0)
			{
#ifdef _WIN32
				CHIAKI_LOGE(regist->log, "Regist connect failed, error %u. Trying next address...", WSAGetLastError());
#else
				int errsv = errno;
				CHIAKI_LOGE(regist->log, "Regist connect failed, error: %s. Trying next address...", strerror(errsv));
#endif
				continue;
			}
		}
		connected = true;
		break;

connect_fail:
		if(!CHIAKI_SOCKET_IS_INVALID(sock))
		{
			CHIAKI_SOCKET_CLOSE(sock);
			sock = CHIAKI_INVALID_SOCKET;
		}
	}
	if(!connected)
	{
		CHIAKI_LOGI(regist->log, "Regist connect failed: tried all addresses");
		if(!CHIAKI_SOCKET_IS_INVALID(sock))
		{
			CHIAKI_SOCKET_CLOSE(sock);
			sock = CHIAKI_INVALID_SOCKET;
		}
	}
	return sock;
}

static ChiakiErrorCode regist_request_connect(ChiakiRegist *regist, const struct sockaddr *addr, size_t addr_len, chiaki_socket_t *sock_out)
{
	chiaki_socket_t sock = socket(addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
	if(CHIAKI_SOCKET_IS_INVALID(sock))
	{
		return CHIAKI_ERR_NETWORK;
	}

	ChiakiErrorCode err = chiaki_socket_set_nonblock(sock, true);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(regist->log, "Regist failed to set request socket non-blocking: %s", chiaki_error_string(err));
		CHIAKI_SOCKET_CLOSE(sock);
		return err;
	}

	err = chiaki_stop_pipe_connect(&regist->stop_pipe, sock, (struct sockaddr *)addr, addr_len, REGIST_REPONSE_TIMEOUT_MS);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		if(err == CHIAKI_ERR_CANCELED)
		{
			CHIAKI_LOGI(regist->log, "Regist canceled while connecting request socket");
		}
		else
		{
#ifdef _WIN32
			CHIAKI_LOGE(regist->log, "Regist connect failed: %u", WSAGetLastError());
#else
			CHIAKI_LOGE(regist->log, "Regist connect failed: %s", chiaki_error_string(err));
#endif
		}
		if(!CHIAKI_SOCKET_IS_INVALID(sock))
		{
			CHIAKI_SOCKET_CLOSE(sock);
			sock = CHIAKI_INVALID_SOCKET;
		}
		return err;
	}

	*sock_out = sock;
	return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode regist_recv_response(ChiakiRegist *regist, ChiakiRegisteredHost *host, chiaki_socket_t sock, ChiakiRPCrypt *rpcrypt, uint16_t remote_counter, char *send_buf, size_t send_buf_size)
{
	uint8_t buf[1500];
	size_t buf_filled_size;
	size_t header_size;
	ChiakiErrorCode err;
	if(regist->info.holepunch_info)
	{
		err = chiaki_send_recv_http_header_psn(regist->info.rudp, regist->log, &remote_counter, send_buf, send_buf_size, (char *)buf, sizeof(buf), &header_size, &buf_filled_size);
		free(send_buf);
	}
	else
		err = chiaki_recv_http_header(sock, (char *)buf, sizeof(buf), &header_size, &buf_filled_size, &regist->stop_pipe, REGIST_REPONSE_TIMEOUT_MS);
	if(err == CHIAKI_ERR_CANCELED)
		return err;
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(regist->log, "Regist failed to receive response HTTP header");
		return err;
	}

	if(regist->info.holepunch_info)
	{
		RudpMessage message;
		err = chiaki_rudp_send_recv(regist->info.rudp, &message, NULL, 0, remote_counter, ACK, FINISH, 0, 3);
		if(err != CHIAKI_ERR_SUCCESS)
			CHIAKI_LOGW(regist->log, "REGIST - Failed to finish rudp, continuing...");
		else
			chiaki_rudp_message_pointers_free(&message);
	}

	CHIAKI_LOGV(regist->log, "Regist response HTTP header:");
	chiaki_log_hexdump(regist->log, CHIAKI_LOG_VERBOSE, buf, header_size);

	ChiakiHttpResponse http_response;
	err = chiaki_http_response_parse(&http_response, (char *)buf, header_size);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(regist->log, "Regist failed to pare response HTTP header");
		return err;
	}

	if(http_response.code != 200)
	{
		CHIAKI_LOGE(regist->log, "Regist received HTTP code %d", http_response.code);

		for(ChiakiHttpHeader *header=http_response.headers; header; header=header->next)
		{
			if(strcmp(header->key, "RP-Application-Reason") == 0)
			{
				uint32_t reason = strtoul(header->value, NULL, 0x10);
				CHIAKI_LOGE(regist->log, "Reported Application Reason: %#x (%s)", (unsigned int)reason, chiaki_rp_application_reason_string(reason));
				break;
			}
		}

		chiaki_http_response_fini(&http_response);
		return CHIAKI_ERR_UNKNOWN;
	}

	size_t content_size = 0;
	for(ChiakiHttpHeader *header=http_response.headers; header; header=header->next)
	{
		if(strcmp(header->key, "Content-Length") == 0)
		{
			content_size = (size_t)strtoull(header->value, NULL, 0);
		}
	}

	chiaki_http_response_fini(&http_response);

	if(!content_size)
	{
		CHIAKI_LOGE(regist->log, "Regist response does not contain or contains invalid Content-Length");
		return CHIAKI_ERR_INVALID_RESPONSE;
	}

	if(content_size + header_size > sizeof(buf))
	{
		CHIAKI_LOGE(regist->log, "Regist response content too big");
		return CHIAKI_ERR_BUF_TOO_SMALL;
	}

	if(regist->info.holepunch_info)
	{
		if(buf_filled_size < content_size + header_size)
		{
			CHIAKI_LOGE(regist->log, "Received %zu which is less than content + header of size %zu", buf_filled_size, content_size + header_size);
			return CHIAKI_ERR_NETWORK;
		}
	}
	else
	{
		uint64_t deadline_ms = chiaki_time_now_monotonic_ms() + REGIST_REPONSE_TIMEOUT_MS;
		while(buf_filled_size < content_size + header_size)
		{
			uint64_t now_ms = chiaki_time_now_monotonic_ms();
			if(now_ms >= deadline_ms)
			{
				CHIAKI_LOGE(regist->log, "Regist timed out receiving response content");
				return CHIAKI_ERR_TIMEOUT;
			}
			err = chiaki_stop_pipe_select_single(&regist->stop_pipe, sock, false, deadline_ms - now_ms);
			if(err != CHIAKI_ERR_SUCCESS)
			{
				if(err == CHIAKI_ERR_TIMEOUT)
					CHIAKI_LOGE(regist->log, "Regist timed out receiving response content");
				return err;
			}

			CHIAKI_SSIZET_TYPE received = recv(sock,  (CHIAKI_SOCKET_BUF_TYPE)buf + buf_filled_size, (content_size + header_size) - buf_filled_size, 0);
			if(received < 0)
			{
#ifdef _WIN32
				int recv_err = WSAGetLastError();
				if(recv_err == WSAEWOULDBLOCK)
					continue;
#else
				if(errno == EAGAIN || errno == EWOULDBLOCK)
					continue;
#endif
			}
			if(received <= 0)
			{
				CHIAKI_LOGE(regist->log, "Regist failed to receive response content");
				return CHIAKI_ERR_NETWORK;
			}
			buf_filled_size += received;
		}
	}

	uint8_t *payload = buf + header_size;
	size_t payload_size = buf_filled_size - header_size;
	chiaki_rpcrypt_decrypt(rpcrypt, 0, payload, payload, payload_size);

	CHIAKI_LOGI(regist->log, "Regist response payload (decrypted):");
	chiaki_log_hexdump(regist->log, CHIAKI_LOG_VERBOSE, payload, payload_size);

	err = regist_parse_response_payload(regist, host, (char *)payload, payload_size);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(regist->log, "Regist failed to parse response payload");
		return err;
	}

	return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode regist_parse_response_payload(ChiakiRegist *regist, ChiakiRegisteredHost *host, char *buf, size_t buf_size)
{
	ChiakiHttpHeader *headers;
	ChiakiErrorCode err = chiaki_http_header_parse(&headers, buf, buf_size);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(regist->log, "Regist failed to parse response payload HTTP header");
		return err;
	}

	memset(host, 0, sizeof(*host));
	host->target = regist->info.target;

	bool mac_found = false;
	bool regist_key_found = false;
	bool key_found = false;
	bool ps5 = chiaki_target_is_ps5(regist->info.target);

	for(ChiakiHttpHeader *header=headers; header; header=header->next)
	{
#define COPY_STRING(name, key_str) \
		if(strcmp(header->key, (key_str)) == 0) \
		{ \
			size_t len = strlen(header->value); \
			if(len >= sizeof(host->name)) \
			{ \
				CHIAKI_LOGE(regist->log, "Regist value for %s in response is too long", (key_str)); \
				continue; \
			} \
			memcpy(host->name, header->value, len); \
			host->name[len] = 0; \
			continue; \
		}
		COPY_STRING(ap_ssid, "AP-Ssid")
		COPY_STRING(ap_bssid, "AP-Bssid")
		COPY_STRING(ap_key, "AP-Key")
		COPY_STRING(ap_name, "AP-Name")
		COPY_STRING(server_nickname, ps5 ? "PS5-Nickname" : "PS4-Nickname")
#undef COPY_STRING

		if(strcmp(header->key, ps5 ? "PS5-RegistKey" : "PS4-RegistKey") == 0)
		{
			memset(host->rp_regist_key, 0, sizeof(host->rp_regist_key));
			size_t buf_size = sizeof(host->rp_regist_key);
			err = parse_hex((uint8_t *)host->rp_regist_key, &buf_size, header->value, strlen(header->value));
			if(err != CHIAKI_ERR_SUCCESS)
			{
				CHIAKI_LOGE(regist->log, "Regist received invalid RegistKey in response");
				memset(host->rp_regist_key, 0, sizeof(host->rp_regist_key));
			}
			else
			{
				regist_key_found = true;
			}
		}
		else if(strcmp(header->key, "RP-KeyType") == 0)
		{
			host->rp_key_type = (uint32_t)strtoul(header->value, NULL, 0);
		}
		else if(strcmp(header->key, "RP-Key") == 0)
		{
			size_t buf_size = sizeof(host->rp_key);
			err = parse_hex((uint8_t *)host->rp_key, &buf_size, header->value, strlen(header->value));
			if(err != CHIAKI_ERR_SUCCESS || buf_size != sizeof(host->rp_key))
			{
				CHIAKI_LOGE(regist->log, "Regist received invalid key in response");
				memset(host->rp_key, 0, sizeof(host->rp_key));
			}
			else
			{
				key_found = true;
			}
		}
		else if(strcmp(header->key, ps5 ? "PS5-Mac" : "PS4-Mac") == 0)
		{
			size_t buf_size = sizeof(host->server_mac);
			err = parse_hex((uint8_t *)host->server_mac, &buf_size, header->value, strlen(header->value));
			if(err != CHIAKI_ERR_SUCCESS || buf_size != sizeof(host->server_mac))
			{
				CHIAKI_LOGE(regist->log, "Regist received invalid MAC Address in response");
				memset(host->server_mac, 0, sizeof(host->server_mac));
			}
			else
			{
				mac_found = true;
			}
		}
		else if(strcmp(header->key, "RP-SupportCmd") == 0)
		{
			uint32_t support_cmd = (uint32_t)strtoul(header->value, NULL, 0);
			CHIAKI_LOGI(regist->log, "RP-Support Cmd: %"PRIu32, support_cmd);
		}
		else
		{
			CHIAKI_LOGI(regist->log, "Regist received unknown key %s in response payload", header->key);
		}
	}

	chiaki_http_header_free(headers);

	if(!regist_key_found)
	{
		CHIAKI_LOGE(regist->log, "Regist response is missing RegistKey (or it was invalid)");
		return CHIAKI_ERR_INVALID_RESPONSE;
	}

	if(!key_found)
	{
		CHIAKI_LOGE(regist->log, "Regist response is missing key (or it was invalid)");
		return CHIAKI_ERR_INVALID_RESPONSE;
	}

	if(!mac_found)
	{
		CHIAKI_LOGE(regist->log, "Regist response is missing MAC Adress (or it was invalid)");
		return CHIAKI_ERR_INVALID_RESPONSE;
	}

	return CHIAKI_ERR_SUCCESS;
}
