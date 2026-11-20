// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <munit.h>

#include "../lib/src/aia.h"

#include <stdlib.h>
#include <string.h>

#include "aia_fixtures.inl"

#define AIA_URL "http://aia.test.invalid/intermediate.crt"
#define PEM_BEGIN "-----BEGIN CERTIFICATE-----"

static const uint8_t *const roots[1] = { fx_root_der };
static const size_t root_lens[1] = { sizeof(fx_root_der) };

static bool fetch_real(const char *url, uint8_t **out, size_t *len, void *user)
{
	(void)user;
	munit_assert_string_equal(url, AIA_URL);
	*out = (uint8_t *)malloc(sizeof(fx_inter_der));
	memcpy(*out, fx_inter_der, sizeof(fx_inter_der));
	*len = sizeof(fx_inter_der);
	return true;
}

static bool fetch_forged(const char *url, uint8_t **out, size_t *len, void *user)
{
	(void)url; (void)user;
	*out = (uint8_t *)malloc(sizeof(fx_decoy_der));
	memcpy(*out, fx_decoy_der, sizeof(fx_decoy_der));
	*len = sizeof(fx_decoy_der);
	return true;
}

static bool fetch_fails(const char *url, uint8_t **out, size_t *len, void *user)
{
	(void)url; (void)out; (void)len; (void)user;
	return false;
}

static MunitResult test_aia_issuer_url(const MunitParameter params[], void *user)
{
	(void)params; (void)user;

	char *url = chiaki_aia_issuer_url(fx_leaf_der, sizeof(fx_leaf_der));
	munit_assert_not_null(url);
	munit_assert_string_equal(url, AIA_URL);
	free(url);

	munit_assert_null(chiaki_aia_issuer_url(fx_root_der, sizeof(fx_root_der)));
	munit_assert_null(chiaki_aia_issuer_url(NULL, 0));
	munit_assert_null(chiaki_aia_issuer_url(fx_leaf_der, 4));

	return MUNIT_OK;
}

static MunitResult test_aia_path_completes(const MunitParameter params[], void *user)
{
	(void)params; (void)user;

	const uint8_t *good[1] = { fx_inter_der };
	size_t good_lens[1] = { sizeof(fx_inter_der) };
	const uint8_t *forged[1] = { fx_decoy_der };
	size_t forged_lens[1] = { sizeof(fx_decoy_der) };

	munit_assert_false(chiaki_aia_path_completes(fx_leaf_der, sizeof(fx_leaf_der),
		NULL, NULL, 0, roots, root_lens, 1, NULL));

	munit_assert_true(chiaki_aia_path_completes(fx_leaf_der, sizeof(fx_leaf_der),
		good, good_lens, 1, roots, root_lens, 1, NULL));

	munit_assert_false(chiaki_aia_path_completes(fx_leaf_der, sizeof(fx_leaf_der),
		forged, forged_lens, 1, roots, root_lens, 1, NULL));

	return MUNIT_OK;
}

static MunitResult test_aia_recover_walks(const MunitParameter params[], void *user)
{
	(void)params; (void)user;

	char *pem = NULL;
	size_t pem_len = 0;
	munit_assert_int(chiaki_aia_recover_with(fx_leaf_der, sizeof(fx_leaf_der),
		fetch_real, NULL, roots, root_lens, 1, &pem, &pem_len, NULL), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_not_null(pem);
	munit_assert_size(pem_len, >, 0);
	munit_assert_memory_equal(strlen(PEM_BEGIN), pem, PEM_BEGIN);
	munit_assert_size(strlen(pem), ==, pem_len);
	free(pem);

	return MUNIT_OK;
}

static MunitResult test_aia_recover_rejects_forged(const MunitParameter params[], void *user)
{
	(void)params; (void)user;

	char *pem = (char *)0x1;
	size_t pem_len = 99;
	munit_assert_int(chiaki_aia_recover_with(fx_leaf_der, sizeof(fx_leaf_der),
		fetch_forged, NULL, roots, root_lens, 1, &pem, &pem_len, NULL), !=, CHIAKI_ERR_SUCCESS);
	munit_assert_null(pem);
	munit_assert_size(pem_len, ==, 0);

	pem = (char *)0x1;
	munit_assert_int(chiaki_aia_recover_with(fx_leaf_der, sizeof(fx_leaf_der),
		fetch_fails, NULL, roots, root_lens, 1, &pem, &pem_len, NULL), !=, CHIAKI_ERR_SUCCESS);
	munit_assert_null(pem);

	return MUNIT_OK;
}

static MunitResult test_aia_recover_noop_when_complete(const MunitParameter params[], void *user)
{
	(void)params; (void)user;

	char *pem = (char *)0x1;
	size_t pem_len = 99;
	munit_assert_int(chiaki_aia_recover_with(fx_inter_der, sizeof(fx_inter_der),
		fetch_fails, NULL, roots, root_lens, 1, &pem, &pem_len, NULL), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_null(pem);
	munit_assert_size(pem_len, ==, 0);

	return MUNIT_OK;
}

static MunitResult test_aia_blob_rejects_non_pem(const MunitParameter params[], void *user)
{
	(void)params; (void)user;
	chiaki_aia_blob_reset();

	munit_assert_false(chiaki_aia_blob_add_pem(NULL, 0));
	munit_assert_false(chiaki_aia_blob_add_pem("hello", 5));
	munit_assert_size(chiaki_aia_blob_len(), ==, 0);
	return MUNIT_OK;
}

static MunitResult test_aia_blob_accumulates(const MunitParameter params[], void *user)
{
	(void)params; (void)user;
	chiaki_aia_blob_reset();

	size_t one_len = 0;
	char *one = chiaki_aia_der_to_pem(fx_inter_der, sizeof(fx_inter_der), &one_len);
	munit_assert_not_null(one);
	munit_assert_memory_equal(strlen(PEM_BEGIN), one, PEM_BEGIN);

	munit_assert_true(chiaki_aia_blob_add_pem(one, one_len));
	munit_assert_size(chiaki_aia_blob_len(), ==, one_len);

	size_t taken_len = 0;
	char *taken = chiaki_aia_blob_take(&taken_len);
	munit_assert_not_null(taken);
	munit_assert_size(taken_len, ==, one_len);
	munit_assert_size(strlen(taken), ==, taken_len);

	free(taken);
	free(one);
	chiaki_aia_blob_reset();
	return MUNIT_OK;
}

struct DelayedIssuer {
	unsigned calls;
	unsigned available_after;
};

static bool fetch_delayed_issuer(const char *url, uint8_t **out, size_t *len, void *user)
{
	struct DelayedIssuer *state = user;
	munit_assert_string_equal(url, AIA_URL);
	bool available = ++state->calls >= state->available_after;
	const uint8_t *der = available ? fx_inter_der : fx_leaf_der;
	*len = available ? sizeof(fx_inter_der) : sizeof(fx_leaf_der);
	*out = malloc(*len);
	munit_assert_not_null(*out);
	memcpy(*out, der, *len);
	return true;
}

static MunitResult test_aia_final_fetch(const MunitParameter params[], void *user)
{
	(void)params; (void)user;
	// Repeated leaf responses keep the path incomplete until the boundary fetch.
	for(unsigned available_after = 4; available_after <= 5; available_after++) {
		struct DelayedIssuer state = { 0, available_after };
		char *pem = NULL;
		size_t len = 0;
		ChiakiErrorCode err = chiaki_aia_recover_with(fx_leaf_der, sizeof(fx_leaf_der),
			fetch_delayed_issuer, &state, roots, root_lens, 1, &pem, &len, NULL);
		munit_assert_uint(state.calls, ==, 4);
		if(available_after == 4) {
			munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
			munit_assert_not_null(pem);
		} else {
			munit_assert_int(err, !=, CHIAKI_ERR_SUCCESS);
			munit_assert_null(pem);
		}
		free(pem);
	}
	return MUNIT_OK;
}

static MunitResult test_aia_preserves_ca_bundle(const MunitParameter params[], void *user)
{
	(void)params; (void)user;
	chiaki_aia_blob_reset();
	size_t root_len, issuer_len, bundle_len;
	char *root = chiaki_aia_der_to_pem(fx_root_der, sizeof(fx_root_der), &root_len);
	char *issuer = chiaki_aia_der_to_pem(fx_inter_der, sizeof(fx_inter_der), &issuer_len);
	munit_assert_not_null(root);
	munit_assert_not_null(issuer);
	/* MinGW's tmpfile() may use an unavailable system temporary directory.
	 * The test working directory is writable, so use a local scratch file. */
	const char *ca_bundle_path = ".chiaki-aia-ca-bundle-test.pem";
	FILE *file = fopen(ca_bundle_path, "wb+");
	munit_assert_not_null(file);
	// Repeat to exercise reads across chunk boundaries and preserve all roots.
	for(int i = 0; i < 8; i++)
		munit_assert_size(fwrite(root, 1, root_len, file), ==, root_len);
	rewind(file);
	munit_assert_true(chiaki_aia_blob_add_pem(issuer, issuer_len));
	char *bundle = chiaki_aia_blob_with_ca(file, &bundle_len);
	munit_assert_not_null(bundle);
	munit_assert_size(bundle_len, ==, 8 * root_len + 1 + issuer_len);
	for(int i = 0; i < 8; i++)
		munit_assert_memory_equal(root_len, bundle + i * root_len, root);
	munit_assert_memory_equal(issuer_len, bundle + 8 * root_len + 1, issuer);
	munit_assert_size(strlen(bundle), ==, bundle_len);
	free(bundle);
	fclose(file);
	remove(ca_bundle_path);
	free(root);
	free(issuer);
	chiaki_aia_blob_reset();
	return MUNIT_OK;
}

static MunitResult test_aia_exports_verified_chain(const MunitParameter params[], void *user)
{
	(void)params; (void)user;
	const uint8_t *candidates[] = { fx_decoy_der, fx_leaf_der, fx_inter_der };
	size_t lengths[] = { sizeof(fx_decoy_der), sizeof(fx_leaf_der), sizeof(fx_inter_der) };
	char *bundle = NULL;
	size_t bundle_len = 0, expected_len = 0;
	munit_assert_true(chiaki_aia_verified_pem(fx_leaf_der, sizeof(fx_leaf_der),
		candidates, lengths, 3, roots, root_lens, 1, &bundle, &bundle_len, NULL));
	char *expected = chiaki_aia_der_to_pem(fx_inter_der, sizeof(fx_inter_der), &expected_len);
	munit_assert_not_null(bundle);
	munit_assert_not_null(expected);
	munit_assert_size(bundle_len, ==, expected_len);
	munit_assert_memory_equal(expected_len, bundle, expected);
	free(bundle);
	free(expected);
	return MUNIT_OK;
}

static bool cancel_recovery(void *user)
{
	return *(bool *)user;
}

static MunitResult test_aia_network_policy(const MunitParameter params[], void *user)
{
	(void)params; (void)user;
	const uint32_t blocked[] = {
		0, 0x0a000001, 0x7f000001, 0xa9fea9fe, 0xac100001, 0xac1fffff,
		0xc0a80101, 0x64400001, 0x647fffff, 0xc0000201, 0xc6120001,
		0xc6336401, 0xcb007101, 0xe0000001, 0xffffffff,
	};
	for(size_t i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++)
		munit_assert_false(chiaki_aia_public_ipv4(blocked[i]));
	munit_assert_true(chiaki_aia_public_ipv4(0x08080808));
	munit_assert_true(chiaki_aia_public_ipv4(0x01010101));
	bool canceled = true;
	ChiakiAiaControl control = { cancel_recovery, &canceled, 0 };
	munit_assert_true(chiaki_aia_should_stop(&control));
	CURL *curl = curl_easy_init();
	munit_assert_not_null(curl);
	munit_assert_false(chiaki_aia_configure_transfer(curl, &control));
	canceled = false;
	control.deadline_ms = 1;
	munit_assert_false(chiaki_aia_configure_transfer(curl, &control));
	control.deadline_ms = 0;
	munit_assert_true(chiaki_aia_configure_transfer(curl, &control));
	curl_easy_cleanup(curl);
	return MUNIT_OK;
}

static void *aia_blob_setup(const MunitParameter params[], void *user)
{
	(void)params; (void)user;
	munit_assert_int(chiaki_aia_init(), ==, CHIAKI_ERR_SUCCESS);
	return NULL;
}

static void aia_blob_teardown(void *fixture)
{
	(void)fixture;
	chiaki_aia_fini();
}

static MunitResult test_aia_pem_roundtrip(const MunitParameter params[], void *user)
{
	(void)params; (void)user;
	size_t pem_len = 0, der_len = 0;
	char *pem = chiaki_aia_der_to_pem(fx_leaf_der, sizeof(fx_leaf_der), &pem_len);
	munit_assert_not_null(pem);
	uint8_t *der = NULL;
	munit_assert_true(chiaki_aia_pem_to_der(pem, pem_len, &der, &der_len));
	munit_assert_not_null(der);
	munit_assert_size(der_len, ==, sizeof(fx_leaf_der));
	munit_assert_memory_equal(der_len, der, fx_leaf_der);
	free(der);
	free(pem);
	return MUNIT_OK;
}

MunitTest tests_aia[] = {
	{ "/pem_roundtrip", test_aia_pem_roundtrip, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/network_policy", test_aia_network_policy, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/exports_verified_chain", test_aia_exports_verified_chain, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/final_fetch", test_aia_final_fetch, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/preserves_ca_bundle", test_aia_preserves_ca_bundle, aia_blob_setup, aia_blob_teardown, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/issuer_url", test_aia_issuer_url, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/path_completes", test_aia_path_completes, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/recover_walks", test_aia_recover_walks, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/recover_rejects_forged", test_aia_recover_rejects_forged, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/recover_noop_when_complete", test_aia_recover_noop_when_complete, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/blob_rejects_non_pem", test_aia_blob_rejects_non_pem, aia_blob_setup, aia_blob_teardown, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/blob_accumulates", test_aia_blob_accumulates, aia_blob_setup, aia_blob_teardown, MUNIT_TEST_OPTION_NONE, NULL },
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
