// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "aia.h"

#include <curl/curl.h>
#include <stdio.h>

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

static X509 *aia_der_to_x509(const uint8_t *der, size_t der_len)
{
	if(!der || der_len == 0)
		return NULL;
	const unsigned char *p = der;
	return d2i_X509(NULL, &p, (long)der_len);
}

char *chiaki_aia_issuer_url(const uint8_t *cert_der, size_t cert_len)
{
	X509 *cert = aia_der_to_x509(cert_der, cert_len);
	if(!cert)
		return NULL;

	char *url = NULL;
	AUTHORITY_INFO_ACCESS *aia =
		(AUTHORITY_INFO_ACCESS *)X509_get_ext_d2i(cert, NID_info_access, NULL, NULL);
	if(aia)
	{
		for(int i = 0; i < sk_ACCESS_DESCRIPTION_num(aia) && !url; i++)
		{
			ACCESS_DESCRIPTION *ad = sk_ACCESS_DESCRIPTION_value(aia, i);
			if(!ad || OBJ_obj2nid(ad->method) != NID_ad_ca_issuers)
				continue;
			if(!ad->location || ad->location->type != GEN_URI)
				continue;

			const ASN1_IA5STRING *uri = ad->location->d.uniformResourceIdentifier;
			int len = ASN1_STRING_length(uri);
			const unsigned char *data = ASN1_STRING_get0_data(uri);
			if(len <= 0 || !data)
				continue;
			if(memchr(data, 0, (size_t)len))
				continue;

			url = (char *)malloc((size_t)len + 1);
			if(url)
			{
				memcpy(url, data, (size_t)len);
				url[len] = 0;
			}
		}
		AUTHORITY_INFO_ACCESS_free(aia);
	}

	X509_free(cert);
	return url;
}

void *chiaki_aia_trust_store(CURL *connection)
{
	if(!connection)
		return NULL;
	X509_STORE *store = X509_STORE_new();
	if(!store)
		return NULL;
	const char *file = NULL, *path = NULL;
	if(curl_easy_getinfo(connection, CURLINFO_CAINFO, &file) != CURLE_OK ||
		curl_easy_getinfo(connection, CURLINFO_CAPATH, &path) != CURLE_OK)
		goto error;
#ifdef _WIN32
	const curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
	if(version->ssl_version && strstr(version->ssl_version, "Schannel") && !file) {
		HCERTSTORE native = CertOpenSystemStoreA(0, "ROOT");
		if(!native)
			goto error;
		PCCERT_CONTEXT cert = NULL;
		size_t added = 0;
		while((cert = CertEnumCertificatesInStore(native, cert))) {
			X509 *root = aia_der_to_x509(cert->pbCertEncoded, cert->cbCertEncoded);
			if(root) {
				if(X509_STORE_add_cert(store, root) == 1)
					added++;
				X509_free(root);
			}
		}
		CertCloseStore(native, 0);
		if(!added)
			goto error;
		return store;
	}
#endif
	if((!file && !path) || X509_STORE_load_locations(store, file, path) != 1)
		goto error;
	return store;
error:
	X509_STORE_free(store);
	return NULL;
}

void chiaki_aia_trust_store_free(void *store)
{
	X509_STORE_free(store);
}

bool chiaki_aia_verify_with_store(
	const uint8_t *leaf_der, size_t leaf_len,
	const uint8_t *const *candidates, const size_t *candidate_lens, size_t candidate_count,
	const uint8_t *const *roots, const size_t *root_lens, size_t root_count,
	ChiakiLog *log, char **pem_out, size_t *pem_len_out, void *trust_store)
{
	if(pem_out)
		*pem_out = NULL;
	if(pem_len_out)
		*pem_len_out = 0;
	X509 *leaf = aia_der_to_x509(leaf_der, leaf_len);
	if(!leaf)
		return false;

	STACK_OF(X509) *untrusted = sk_X509_new_null();
	X509_STORE *store = trust_store ? trust_store : X509_STORE_new();
	X509_STORE_CTX *ctx = X509_STORE_CTX_new();
	bool ok = false;

	if(!untrusted || !store || !ctx)
		goto out;

	for(size_t i = 0; i < candidate_count; i++)
	{
		X509 *cand = aia_der_to_x509(candidates[i], candidate_lens[i]);
		if(!cand)
			continue;
		if(!sk_X509_push(untrusted, cand))
		{
			X509_free(cand);
			goto out;
		}
	}

	if(trust_store) {
		// The caller supplies the trust source used by the connection.
	}
	else if(roots && root_lens && root_count > 0)
	{
		size_t added = 0;
		for(size_t i = 0; i < root_count; i++)
		{
			X509 *root = aia_der_to_x509(roots[i], root_lens[i]);
			if(!root)
				continue;
			if(X509_STORE_add_cert(store, root) == 1)
				added++;
			X509_free(root);
		}
		if(added == 0)
			goto out;
	}
	else if(X509_STORE_set_default_paths(store) != 1)
	{
		CHIAKI_LOGE(log, "aia: could not load the system trust store");
		goto out;
	}

	if(X509_STORE_CTX_init(ctx, store, leaf, untrusted) != 1)
		goto out;

	if(X509_verify_cert(ctx) == 1)
	{
		ok = true;
		if(pem_out) {
			// Export only the verified intermediates, excluding leaf and trust anchor.
			STACK_OF(X509) *chain = X509_STORE_CTX_get0_chain(ctx);
			BIO *bio = BIO_new(BIO_s_mem());
			if(!bio) {
				ok = false;
				goto out;
			}
			for(int i = 1; i < sk_X509_num(chain) - 1; i++) {
				if(PEM_write_bio_X509(bio, sk_X509_value(chain, i)) != 1) {
					ok = false;
					break;
				}
			}
			char *data = NULL;
			long len = BIO_get_mem_data(bio, &data);
			if(ok && len > 0) {
				*pem_out = malloc((size_t)len + 1);
				if(*pem_out) {
					memcpy(*pem_out, data, (size_t)len);
					(*pem_out)[len] = 0;
					*pem_len_out = (size_t)len;
				} else
					ok = false;
			}
			BIO_free(bio);
		}
	}
	else
	{
		int err = X509_STORE_CTX_get_error(ctx);
		CHIAKI_LOGI(log, "aia: path incomplete (x509 err %d): %s",
			err, X509_verify_cert_error_string(err));
	}

out:
	if(ctx)
		X509_STORE_CTX_free(ctx);
	if(store && !trust_store)
		X509_STORE_free(store);
	if(untrusted)
		sk_X509_pop_free(untrusted, X509_free);
	X509_free(leaf);
	return ok;
}

bool chiaki_aia_path_completes(
	const uint8_t *leaf_der, size_t leaf_len,
	const uint8_t *const *candidates, const size_t *candidate_lens, size_t candidate_count,
	const uint8_t *const *roots, const size_t *root_lens, size_t root_count, ChiakiLog *log)
{
	return chiaki_aia_verify_with_store(leaf_der, leaf_len, candidates, candidate_lens, candidate_count,
		roots, root_lens, root_count, log, NULL, NULL, NULL);
}

bool chiaki_aia_verified_pem(
	const uint8_t *leaf_der, size_t leaf_len,
	const uint8_t *const *candidates, const size_t *candidate_lens, size_t candidate_count,
	const uint8_t *const *roots, const size_t *root_lens, size_t root_count,
	char **pem_out, size_t *pem_len_out, ChiakiLog *log)
{
	return chiaki_aia_verify_with_store(leaf_der, leaf_len, candidates, candidate_lens, candidate_count,
		roots, root_lens, root_count, log, pem_out, pem_len_out, NULL);
}

char *chiaki_aia_der_to_pem(const uint8_t *der, size_t der_len, size_t *pem_len_out)
{
	X509 *cert = aia_der_to_x509(der, der_len);
	if(!cert)
		return NULL;

	BIO *bio = BIO_new(BIO_s_mem());
	if(!bio)
	{
		X509_free(cert);
		return NULL;
	}

	char *out = NULL;
	if(PEM_write_bio_X509(bio, cert) == 1)
	{
		char *data = NULL;
		long n = BIO_get_mem_data(bio, &data);
		if(n > 0)
		{
			out = (char *)malloc((size_t)n + 1);
			if(out)
			{
				memcpy(out, data, (size_t)n);
				out[n] = 0;
				if(pem_len_out)
					*pem_len_out = (size_t)n;
			}
		}
	}

	BIO_free(bio);
	X509_free(cert);
	return out;
}

bool chiaki_aia_pem_to_der(const char *pem, size_t pem_len, uint8_t **der_out, size_t *der_len_out)
{
	if(!pem || pem_len == 0 || !der_out || !der_len_out)
		return false;

	BIO *bio = BIO_new_mem_buf(pem, (int)pem_len);
	if(!bio)
		return false;

	X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	BIO_free(bio);
	if(!cert)
		return false;

	unsigned char *der = NULL;
	int der_len = i2d_X509(cert, &der);
	X509_free(cert);
	if(der_len <= 0 || !der) {
		OPENSSL_free(der);
		return false;
	}

	// Callers own the result through malloc/free, not OpenSSL's allocator.
	uint8_t *copy = malloc((size_t)der_len);
	if(copy)
		memcpy(copy, der, (size_t)der_len);
	OPENSSL_free(der);
	if(!copy)
		return false;
	*der_out = copy;
	*der_len_out = (size_t)der_len;
	return true;
}

bool chiaki_aia_peek_leaf(const char *host, uint8_t **der_out, size_t *der_len_out, ChiakiLog *log,
	const ChiakiAiaControl *control)
{
	if(!host || !der_out || !der_len_out)
		return false;

	char url[256];
	snprintf(url, sizeof(url), "https://%s/", host);

	CURL *curl = curl_easy_init();
	if(!curl)
		return false;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
	if(!chiaki_aia_configure_transfer(curl, control)) {
		curl_easy_cleanup(curl);
		return false;
	}
	curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

	CURLcode res = curl_easy_perform(curl);
	struct curl_certinfo *ci = NULL;
	bool ok = false;

	if(!chiaki_aia_should_stop(control) && res == CURLE_OK &&
		curl_easy_getinfo(curl, CURLINFO_CERTINFO, &ci) == CURLE_OK && ci && ci->num_of_certs > 0)
	{
		for(struct curl_slist *e = ci->certinfo[0]; e && !ok; e = e->next)
		{
			if(strncmp(e->data, "Cert:", 5) == 0)
				ok = chiaki_aia_pem_to_der(e->data + 5, strlen(e->data + 5), der_out, der_len_out);
		}
	}

	if(!ok)
		CHIAKI_LOGW(log, "aia: could not read the certificate served by %s: %s",
			host, curl_easy_strerror(res));

	curl_easy_cleanup(curl);
	return ok;
}

char *chiaki_aia_cert_describe(const uint8_t *der, size_t der_len)
{
	X509 *cert = aia_der_to_x509(der, der_len);
	if(!cert)
		return NULL;

	char subject[256] = { 0 };
	char issuer[256] = { 0 };
	X509_NAME_oneline(X509_get_subject_name(cert), subject, (int)sizeof(subject) - 1);
	X509_NAME_oneline(X509_get_issuer_name(cert), issuer, (int)sizeof(issuer) - 1);
	X509_free(cert);

	size_t len = strlen(subject) + strlen(issuer) + 32;
	char *out = (char *)malloc(len);
	if(out)
		snprintf(out, len, "%s (issued by %s)", subject, issuer);
	return out;
}
