// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "aia.h"
#include <chiaki/config.h>

#include <chiaki/thread.h>
#include <chiaki/time.h>
#include <chiaki/sock.h>
#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AIA_PEM_BEGIN "-----BEGIN CERTIFICATE-----"
#define AIA_MAX_HOPS 4

bool chiaki_aia_should_stop(const ChiakiAiaControl *control)
{
	return control && ((control->canceled && control->canceled(control->user)) ||
		(control->deadline_ms && chiaki_time_now_monotonic_ms() >= control->deadline_ms));
}

bool chiaki_aia_public_ipv4(uint32_t a)
{
	return !((a >> 24) == 0 || (a >> 24) == 10 || (a >> 24) == 127 ||
		(a >> 22) == (0x64400000U >> 22) ||
		(a >> 16) == 0xa9fe || (a >> 20) == (0xac100000U >> 20) ||
		(a >> 16) == 0xc0a8 ||
		(a >> 8) == 0xc00000 || (a >> 8) == 0xc00002 || (a >> 8) == 0xc05863 ||
		(a >> 15) == (0xc6120000U >> 15) || (a >> 8) == 0xc63364 ||
		(a >> 8) == 0xcb0071 || a >= 0xe0000000U);
}

static curl_socket_t aia_open_socket(void *user, curlsocktype purpose, struct curl_sockaddr *address)
{
	if(chiaki_aia_should_stop(user) || purpose != CURLSOCKTYPE_IPCXN || address->family != AF_INET)
		return CURL_SOCKET_BAD;
	const struct sockaddr_in *addr = (const struct sockaddr_in *)&address->addr;
	if(!chiaki_aia_public_ipv4(ntohl(addr->sin_addr.s_addr)))
		return CURL_SOCKET_BAD;
	return socket(address->family, address->socktype, address->protocol);
}

static int aia_transfer_progress(void *user, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un)
{
	(void)dt; (void)dn; (void)ut; (void)un;
	return chiaki_aia_should_stop(user) ? 1 : 0;
}

bool chiaki_aia_configure_transfer(CURL *curl, const ChiakiAiaControl *control)
{
	if(chiaki_aia_should_stop(control))
		return false;
	uint64_t timeout = 10000;
	if(control && control->deadline_ms) {
		uint64_t now = chiaki_time_now_monotonic_ms();
		if(now >= control->deadline_ms)
			return false;
		if(control->deadline_ms - now < timeout)
			timeout = control->deadline_ms - now;
	}
	// Direct IPv4 connections allow checking every resolved destination, including redirects.
	return curl_easy_setopt(curl, CURLOPT_PROXY, "") == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https") == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, aia_open_socket) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, control) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, aia_transfer_progress) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, control) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout) == CURLE_OK;
}

static ChiakiMutex aia_blob_mutex;
static char *aia_blob = NULL;
static size_t aia_blob_size = 0;
static uint32_t aia_blob_gen = 0;

ChiakiErrorCode chiaki_aia_init(void)
{
	return chiaki_mutex_init(&aia_blob_mutex, false);
}

void chiaki_aia_fini(void)
{
	chiaki_aia_blob_reset();
	chiaki_mutex_fini(&aia_blob_mutex);
}

bool chiaki_aia_blob_add_pem(const char *pem, size_t pem_len)
{
	if(!pem || pem_len < sizeof(AIA_PEM_BEGIN) - 1)
		return false;

	if(memcmp(pem, AIA_PEM_BEGIN, sizeof(AIA_PEM_BEGIN) - 1) != 0)
		return false;

	chiaki_mutex_lock(&aia_blob_mutex);

	char *grown = (char *)realloc(aia_blob, aia_blob_size + pem_len + 2);
	if(!grown)
	{
		chiaki_mutex_unlock(&aia_blob_mutex);
		return false;
	}

	memcpy(grown + aia_blob_size, pem, pem_len);
	size_t added = pem_len;
	if(pem[pem_len - 1] != '\n')
		grown[aia_blob_size + added++] = '\n';
	grown[aia_blob_size + added] = 0;

	aia_blob = grown;
	aia_blob_size += added;
	aia_blob_gen++;

	chiaki_mutex_unlock(&aia_blob_mutex);
	return true;
}

char *chiaki_aia_blob_take(size_t *len_out)
{
	chiaki_mutex_lock(&aia_blob_mutex);

	char *copy = NULL;
	if(aia_blob && aia_blob_size)
	{
		copy = (char *)malloc(aia_blob_size + 1);
		if(copy)
		{
			memcpy(copy, aia_blob, aia_blob_size);
			copy[aia_blob_size] = 0;
			if(len_out)
				*len_out = aia_blob_size;
		}
	}
	if(!copy && len_out)
		*len_out = 0;

	chiaki_mutex_unlock(&aia_blob_mutex);
	return copy;
}

char *chiaki_aia_blob_with_ca(FILE *ca_file, size_t *len_out)
{
	*len_out = 0;
	size_t issuers_len = 0;
	char *issuers = chiaki_aia_blob_take(&issuers_len);
	if(!issuers)
		return NULL;
	if(!ca_file) {
		*len_out = issuers_len;
		return issuers;
	}

	char *bundle = NULL;
	size_t size = 0;
	char chunk[4096];
	size_t n;
	while((n = fread(chunk, 1, sizeof(chunk), ca_file)) > 0) {
		if(size > SIZE_MAX - n)
			goto error;
		char *grown = realloc(bundle, size + n);
		if(!grown)
			goto error;
		bundle = grown;
		memcpy(bundle + size, chunk, n);
		size += n;
	}
	if(ferror(ca_file) || issuers_len > SIZE_MAX - 2 || size > SIZE_MAX - issuers_len - 2)
		goto error;
	char *grown = realloc(bundle, size + issuers_len + 2);
	if(!grown)
		goto error;
	bundle = grown;
	bundle[size++] = '\n';
	memcpy(bundle + size, issuers, issuers_len);
	size += issuers_len;
	bundle[size] = 0;
	free(issuers);
	*len_out = size;
	return bundle;
error:
	free(bundle);
	free(issuers);
	return NULL;
}

size_t chiaki_aia_blob_len(void)
{
	chiaki_mutex_lock(&aia_blob_mutex);
	size_t len = aia_blob_size;
	chiaki_mutex_unlock(&aia_blob_mutex);
	return len;
}

uint32_t chiaki_aia_blob_generation(void)
{
	chiaki_mutex_lock(&aia_blob_mutex);
	uint32_t gen = aia_blob_gen;
	chiaki_mutex_unlock(&aia_blob_mutex);
	return gen;
}

void chiaki_aia_blob_reset(void)
{
	chiaki_mutex_lock(&aia_blob_mutex);
	free(aia_blob);
	aia_blob = NULL;
	aia_blob_size = 0;
	aia_blob_gen++;
	chiaki_mutex_unlock(&aia_blob_mutex);
}

#ifndef CHIAKI_LIB_ENABLE_MBEDTLS
static void aia_free_all(uint8_t **certs, size_t count)
{
	for(size_t i = 0; i < count; i++)
		free(certs[i]);
}

static ChiakiErrorCode aia_recover_with_store(
	const uint8_t *leaf_der, size_t leaf_len,
	ChiakiAiaFetch fetch, void *fetch_user,
	const uint8_t *const *roots, const size_t *root_lens, size_t root_count,
	char **pem_out, size_t *pem_len_out, ChiakiLog *log, void *trust_store)
{
	if(!leaf_der || leaf_len == 0 || !fetch || !pem_out)
		return CHIAKI_ERR_INVALID_DATA;

	*pem_out = NULL;
	if(pem_len_out)
		*pem_len_out = 0;

	uint8_t *found[AIA_MAX_HOPS];
	size_t found_lens[AIA_MAX_HOPS];
	size_t found_count = 0;

	const uint8_t *current = leaf_der;
	size_t current_len = leaf_len;
	bool complete = false;

	char *leaf_desc = chiaki_aia_cert_describe(leaf_der, leaf_len);
	CHIAKI_LOGI(log, "aia: walking from %s", leaf_desc ? leaf_desc : "(unreadable certificate)");
	free(leaf_desc);

	for(size_t hop = 0; hop <= AIA_MAX_HOPS; hop++)
	{
		if(chiaki_aia_verify_with_store(leaf_der, leaf_len,
				(const uint8_t *const *)found, found_lens, found_count,
				roots, root_lens, root_count, log, NULL, NULL, trust_store))
		{
			complete = true;
			break;
		}
		if(hop == AIA_MAX_HOPS)
			break;

		char *url = chiaki_aia_issuer_url(current, current_len);
		if(!url)
		{
			CHIAKI_LOGW(log, "aia: certificate names no issuer to fetch, cannot continue");
			break;
		}

		if(strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
		{
			CHIAKI_LOGW(log, "aia: refusing non-http issuer URL");
			free(url);
			break;
		}

		CHIAKI_LOGI(log, "aia: fetching issuer %zu from %s", hop + 1, url);
		uint8_t *der = NULL;
		size_t der_len = 0;
		bool got = fetch(url, &der, &der_len, fetch_user);
		free(url);

		if(!got || !der || der_len == 0)
		{
			CHIAKI_LOGW(log, "aia: issuer fetch failed");
			free(der);
			break;
		}

		char *desc = chiaki_aia_cert_describe(der, der_len);
		CHIAKI_LOGI(log, "aia:   hop %zu -> %s", hop + 1, desc ? desc : "(unreadable certificate)");
		free(desc);

		found[found_count] = der;
		found_lens[found_count] = der_len;
		found_count++;
		current = der;
		current_len = der_len;
	}

	if(!complete)
	{
		CHIAKI_LOGW(log, "aia: could not complete the chain after %zu fetch(es)", found_count);
		aia_free_all(found, found_count);
		return CHIAKI_ERR_INVALID_DATA;
	}

	if(found_count == 0)
	{
		CHIAKI_LOGI(log, "aia: chain already complete, nothing to recover");
		return CHIAKI_ERR_SUCCESS;
	}

	char *bundle = NULL;
	size_t bundle_len = 0;
	bool verified = chiaki_aia_verify_with_store(leaf_der, leaf_len,
		(const uint8_t *const *)found, found_lens, found_count,
		roots, root_lens, root_count, log, &bundle, &bundle_len, trust_store);

	aia_free_all(found, found_count);

	if(!verified || !bundle || bundle_len == 0)
	{
		free(bundle);
		return CHIAKI_ERR_MEMORY;
	}

	CHIAKI_LOGI(log, "aia: path complete after %zu hop(s), %zu byte bundle", found_count, bundle_len);
	*pem_out = bundle;
	if(pem_len_out)
		*pem_len_out = bundle_len;
	return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_aia_recover_with(
	const uint8_t *leaf_der, size_t leaf_len,
	ChiakiAiaFetch fetch, void *fetch_user,
	const uint8_t *const *roots, const size_t *root_lens, size_t root_count,
	char **pem_out, size_t *pem_len_out, ChiakiLog *log)
{
	return aia_recover_with_store(leaf_der, leaf_len, fetch, fetch_user,
		roots, root_lens, root_count, pem_out, pem_len_out, log, NULL);
}

typedef struct
{
	uint8_t *data;
	size_t len;
	bool failed;
} AiaDownload;

static size_t aia_download_write(void *contents, size_t size, size_t nmemb, void *userp)
{
	AiaDownload *dl = (AiaDownload *)userp;
	size_t total = size * nmemb;
	if(dl->failed)
		return 0;
	if(dl->len + total > 64 * 1024)
	{
		dl->failed = true;
		return 0;
	}
	uint8_t *grown = (uint8_t *)realloc(dl->data, dl->len + total);
	if(!grown)
	{
		dl->failed = true;
		return 0;
	}
	memcpy(grown + dl->len, contents, total);
	dl->data = grown;
	dl->len += total;
	return total;
}

static bool aia_fetch_curl(const char *url, uint8_t **der_out, size_t *der_len_out, void *user)
{
	const ChiakiAiaControl *control = user;
	if(chiaki_aia_should_stop(control))
		return false;
	CURL *curl = curl_easy_init();
	if(!curl)
		return false;

	AiaDownload dl = { NULL, 0, false };
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, aia_download_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dl);
	if(!chiaki_aia_configure_transfer(curl, control)) {
		curl_easy_cleanup(curl);
		return false;
	}
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if(chiaki_aia_should_stop(control) || res != CURLE_OK || dl.failed || !dl.data || dl.len == 0)
	{
		free(dl.data);
		return false;
	}

	if(dl.data[0] != 0x30)
	{
		uint8_t *der = NULL;
		size_t der_len = 0;
		bool converted = chiaki_aia_pem_to_der((const char *)dl.data, dl.len, &der, &der_len);
		free(dl.data);
		if(!converted)
			return false;
		*der_out = der;
		*der_len_out = der_len;
		return true;
	}

	*der_out = dl.data;
	*der_len_out = dl.len;
	return true;
}

ChiakiErrorCode chiaki_aia_recover(
	const uint8_t *leaf_der, size_t leaf_len,
	char **pem_out, size_t *pem_len_out, ChiakiLog *log, const ChiakiAiaControl *control)
{
	*pem_out = NULL;
	if(pem_len_out)
		*pem_len_out = 0;
	void *store = chiaki_aia_trust_store(control ? control->connection : NULL);
	if(!store) {
		CHIAKI_LOGE(log, "aia: could not load the connection trust store");
		return CHIAKI_ERR_INVALID_DATA;
	}
	ChiakiErrorCode err = aia_recover_with_store(leaf_der, leaf_len, aia_fetch_curl, (void *)control,
		NULL, NULL, 0, pem_out, pem_len_out, log, store);
	chiaki_aia_trust_store_free(store);
	return err;
}
#else
// Fail closed until certificate-chain recovery has an mbedTLS implementation.
bool chiaki_aia_peek_leaf(const char *host, uint8_t **der_out, size_t *der_len_out, ChiakiLog *log,
	const ChiakiAiaControl *control)
{
	(void)control;
	(void)host;
	(void)log;
	*der_out = NULL;
	*der_len_out = 0;
	return false;
}

ChiakiErrorCode chiaki_aia_recover(
	const uint8_t *leaf_der, size_t leaf_len,
	char **pem_out, size_t *pem_len_out, ChiakiLog *log, const ChiakiAiaControl *control)
{
	(void)control;
	(void)leaf_der;
	(void)leaf_len;
	(void)log;
	*pem_out = NULL;
	*pem_len_out = 0;
	return CHIAKI_ERR_UNKNOWN;
}
#endif
