// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#ifndef CHIAKI_AIA_H
#define CHIAKI_AIA_H

#include <chiaki/common.h>
#include <chiaki/log.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <curl/curl.h>

#ifdef __cplusplus
extern "C" {
#endif

ChiakiErrorCode chiaki_aia_init(void);
void chiaki_aia_fini(void);

char *chiaki_aia_issuer_url(const uint8_t *cert_der, size_t cert_len);

bool chiaki_aia_path_completes(
	const uint8_t *leaf_der, size_t leaf_len,
	const uint8_t *const *candidates, const size_t *candidate_lens, size_t candidate_count,
	const uint8_t *const *roots, const size_t *root_lens, size_t root_count,
	ChiakiLog *log);

char *chiaki_aia_cert_describe(const uint8_t *der, size_t der_len);

bool chiaki_aia_verified_pem(
	const uint8_t *leaf_der, size_t leaf_len,
	const uint8_t *const *candidates, const size_t *candidate_lens, size_t candidate_count,
	const uint8_t *const *roots, const size_t *root_lens, size_t root_count,
	char **pem_out, size_t *pem_len_out, ChiakiLog *log);

char *chiaki_aia_der_to_pem(const uint8_t *der, size_t der_len, size_t *pem_len_out);

bool chiaki_aia_pem_to_der(const char *pem, size_t pem_len, uint8_t **der_out, size_t *der_len_out);

typedef bool (*ChiakiAiaFetch)(const char *url, uint8_t **der_out, size_t *der_len_out, void *user);

ChiakiErrorCode chiaki_aia_recover_with(
	const uint8_t *leaf_der, size_t leaf_len,
	ChiakiAiaFetch fetch, void *fetch_user,
	const uint8_t *const *roots, const size_t *root_lens, size_t root_count,
	char **pem_out, size_t *pem_len_out, ChiakiLog *log);

typedef struct {
	bool (*canceled)(void *user);
	void *user;
	uint64_t deadline_ms;
	CURL *connection;
} ChiakiAiaControl;

void *chiaki_aia_trust_store(CURL *connection);
void chiaki_aia_trust_store_free(void *store);
bool chiaki_aia_verify_with_store(
	const uint8_t *leaf_der, size_t leaf_len,
	const uint8_t *const *candidates, const size_t *candidate_lens, size_t candidate_count,
	const uint8_t *const *roots, const size_t *root_lens, size_t root_count,
	ChiakiLog *log, char **pem_out, size_t *pem_len_out, void *trust_store);

bool chiaki_aia_should_stop(const ChiakiAiaControl *control);
bool chiaki_aia_configure_transfer(CURL *curl, const ChiakiAiaControl *control);
bool chiaki_aia_public_ipv4(uint32_t address);

bool chiaki_aia_peek_leaf(const char *host, uint8_t **der_out, size_t *der_len_out, ChiakiLog *log,
	const ChiakiAiaControl *control);

ChiakiErrorCode chiaki_aia_recover(
	const uint8_t *leaf_der, size_t leaf_len,
	char **pem_out, size_t *pem_len_out, ChiakiLog *log, const ChiakiAiaControl *control);

bool chiaki_aia_blob_add_pem(const char *pem, size_t pem_len);

char *chiaki_aia_blob_take(size_t *len_out);

// Combine the configured CA file with cached issuers; NULL means no CA file.
char *chiaki_aia_blob_with_ca(FILE *ca_file, size_t *len_out);

size_t chiaki_aia_blob_len(void);
uint32_t chiaki_aia_blob_generation(void);
void chiaki_aia_blob_reset(void);

#ifdef __cplusplus
}
#endif

#endif
