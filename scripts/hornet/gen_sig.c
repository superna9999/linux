// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 *
 * Generate a signature for an eBPF program along with appending
 * map hashes as signed attributes
 *
 * Copyright © 2025      Microsoft Corporation.
 *
 * Authors: Blaise Boscaccy <bboscaccy@linux.microsoft.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the licence, or (at your option) any later version.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <err.h>
#include <getopt.h>

#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pkcs7.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/objects.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/opensslv.h>
#include <openssl/bio.h>
#include <openssl/stack.h>

#if OPENSSL_VERSION_MAJOR >= 3
# define USE_PKCS11_PROVIDER
# include <openssl/provider.h>
# include <openssl/store.h>
#else
# if !defined(OPENSSL_NO_ENGINE) && !defined(OPENSSL_NO_DEPRECATED_3_0)
#  define USE_PKCS11_ENGINE
#  include <openssl/engine.h>
# endif
#endif
#include "../ssl-common.h"

#define SHA256_LEN 32
#define BUF_SIZE   (1 << 15) // 32 KiB
#define MAX_HASHES 64

struct hash_spec {
	char *file;
	int index;
};

typedef struct {
	ASN1_INTEGER *index;
	ASN1_OCTET_STRING *hash;

} HORNET_MAP;

DECLARE_ASN1_FUNCTIONS(HORNET_MAP)
ASN1_SEQUENCE(HORNET_MAP) = {
	ASN1_SIMPLE(HORNET_MAP, index, ASN1_INTEGER),
	ASN1_SIMPLE(HORNET_MAP, hash, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(HORNET_MAP);

IMPLEMENT_ASN1_FUNCTIONS(HORNET_MAP)

DEFINE_STACK_OF(HORNET_MAP)

typedef struct {
	STACK_OF(HORNET_MAP) * maps;
} MAP_SET;

DECLARE_ASN1_FUNCTIONS(MAP_SET)
ASN1_SEQUENCE(MAP_SET) = {
	ASN1_SET_OF(MAP_SET, maps, HORNET_MAP)
} ASN1_SEQUENCE_END(MAP_SET);

IMPLEMENT_ASN1_FUNCTIONS(MAP_SET)

#define DIE(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); \
		exit(EXIT_FAILURE); } while (0)

static BIO *bio_open_wr(const char *path)
{
	BIO *b = BIO_new_file(path, "wb");

	if (!b) {
		perror(path);
		ERR_print_errors_fp(stderr);
		exit(EXIT_FAILURE);
	}
	return b;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s --data content.bin --cert signer.crt --key signer.key [-pass pass]\n"
		"     --out newsig.p7b \n"
		"     --add FILE:index [--add FILE:index ...]\n",
		prog);
}

static const char *key_pass;

static int pem_pw_cb(char *buf, int len, int w, void *v)
{
	int pwlen;

	if (!key_pass)
		return -1;

	pwlen = strlen(key_pass);
	if (pwlen >= len)
		return -1;

	strcpy(buf, key_pass);

	key_pass = NULL;

	return pwlen;
}

static EVP_PKEY *read_private_key(const char *private_key_name)
{
	EVP_PKEY *private_key;
	BIO *b;

	b = BIO_new_file(private_key_name, "rb");
	ERR(!b, "%s", private_key_name);
	private_key = PEM_read_bio_PrivateKey(b, NULL, pem_pw_cb,
					      NULL);
	ERR(!private_key, "%s", private_key_name);
	BIO_free(b);

	return private_key;
}

static X509 *read_x509(const char *x509_name)
{
	unsigned char buf[2];
	X509 *x509;
	BIO *b;
	int n;

	b = BIO_new_file(x509_name, "rb");
	ERR(!b, "%s", x509_name);

	/* Look at the first two bytes of the file to determine the encoding */
	n = BIO_read(b, buf, 2);
	if (n != 2) {
		if (BIO_should_retry(b)) {
			fprintf(stderr, "%s: Read wanted retry\n", x509_name);
			exit(1);
		}
		if (n >= 0) {
			fprintf(stderr, "%s: Short read\n", x509_name);
			exit(1);
		}
		ERR(1, "%s", x509_name);
	}

	ERR(BIO_reset(b) != 0, "%s", x509_name);

	if (buf[0] == 0x30 && buf[1] >= 0x81 && buf[1] <= 0x84)
		/* Assume raw DER encoded X.509 */
		x509 = d2i_X509_bio(b, NULL);
	else
		/* Assume PEM encoded X.509 */
		x509 = PEM_read_bio_X509(b, NULL, NULL, NULL);

	BIO_free(b);
	ERR(!x509, "%s", x509_name);

	return x509;
}

static int sha256(const char *path, unsigned char out[SHA256_LEN], unsigned int *out_len)
{
	FILE *f;
	int rc;
	EVP_MD_CTX *ctx;
	unsigned char buf[BUF_SIZE];
	size_t n;
	unsigned int mdlen = 0;

	if (!path || !out)
		return -1;

	f = fopen(path, "rb");
	if (!f) {
		perror("fopen");
		return -2;
	}

	ERR_load_crypto_strings();

	rc = -3;
	ctx = EVP_MD_CTX_new();
	if (!ctx) {
		rc = -4;
		goto done;
	}

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	if (EVP_DigestInit_ex2(ctx, EVP_sha256(), NULL) != 1) {
		rc = -5;
		goto done;
	}
#else
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
		rc = -5;
		goto done;
	}
#endif
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		if (EVP_DigestUpdate(ctx, buf, n) != 1) {
			rc = -6;
			goto done;
		}
	}
	if (ferror(f)) {
		rc = -7;
		goto done;
	}

	if (EVP_DigestFinal_ex(ctx, out, &mdlen) != 1) {
		rc = -8;
		goto done;
	}
	if (mdlen != SHA256_LEN) {
		rc = -9;
		goto done;
	}

	if (out_len)
		*out_len = mdlen;
	rc = 0;

done:
	EVP_MD_CTX_free(ctx);
	fclose(f);
	ERR_free_strings();
	return rc;
}

static void add_hash(MAP_SET *set, unsigned char *buffer, int buffer_len, int index)
{
	HORNET_MAP *map = NULL;

	map = HORNET_MAP_new();
	ASN1_INTEGER_set(map->index, index);
	ASN1_OCTET_STRING_set(map->hash, buffer, buffer_len);
	sk_HORNET_MAP_push(set->maps, map);
}

int main(int argc, char **argv)
{
	const char *cert_path = NULL;
	const char *key_path = NULL;
	const char *data_path = NULL;
	const char *out_path = NULL;

	X509 *signer;
	EVP_PKEY *pkey;
	BIO *data_in;
	CMS_ContentInfo *cms_out;
	struct hash_spec hashes[MAX_HASHES];
	int hash_count = 0;
	int flags;
	CMS_SignerInfo *si;
	MAP_SET *set;
	unsigned char hash_buffer[SHA256_LEN];
	unsigned int hash_len;
	ASN1_OBJECT *oid;
	unsigned char *der = NULL;
	int der_len;
	int err;
	BIO *b_out;
	int i;
	int opt;

	const char *short_opts = "C:K:P:O:A:Sh";

	static const struct option long_opts[] = {
		{"cert", required_argument, 0, 'C'},
		{"key",  required_argument, 0, 'K'},
		{"pass",  required_argument, 0, 'P'},
		{"out",  required_argument, 0, 'O'},
		{"data",  required_argument, 0, 'D'},
		{"add",  required_argument, 0, 'A'},
		{"help",    no_argument,       0, 'h'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long_only(argc, argv, short_opts, long_opts, NULL)) != -1) {
		switch (opt) {
		case 'C':
			cert_path = optarg;
			break;
		case 'K':
			key_path = optarg;
			break;
		case 'P':
			key_pass = optarg;
			break;
		case 'O':
			out_path = optarg;
			break;
		case 'D':
			data_path = optarg;
			break;
		case 'A':
			if (strchr(optarg, ':')) {
				hashes[hash_count].file = strsep(&optarg, ":");
				hashes[hash_count].index = atoi(optarg);
				if (++hash_count >= MAX_HASHES) {
					usage(argv[0]);
					return EXIT_FAILURE;
				}
			} else {
				usage(argv[0]);
				return EXIT_FAILURE;
			}
			break;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (!cert_path || !key_path || !out_path || !data_path) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();

	signer = read_x509(cert_path);
	ERR(!signer, "Load cert failed");

	pkey = read_private_key(key_path);
	ERR(!pkey, "Load key failed");

	data_in = BIO_new_file(data_path, "rb");
	ERR(!data_in, "Load data failed");

	cms_out = CMS_sign(NULL, NULL, NULL, NULL,
			   CMS_NOCERTS | CMS_PARTIAL | CMS_BINARY | CMS_DETACHED);
	ERR(!cms_out, "create cms failed");

	flags = CMS_NOCERTS | CMS_PARTIAL | CMS_BINARY | CMS_NOSMIMECAP | CMS_DETACHED;

	si = CMS_add1_signer(cms_out, signer, pkey, EVP_sha256(), flags);
	ERR(!si, "add signer failed");

	set = MAP_SET_new();
	set->maps = sk_HORNET_MAP_new_null();

	for (i = 0; i < hash_count; i++) {
		if (sha256(hashes[i].file, hash_buffer, &hash_len) != 0) {
			DIE("failed to hash input");
		}
		add_hash(set, hash_buffer, hash_len, hashes[i].index);
	}

	oid = OBJ_txt2obj("2.25.316487325684022475439036912669789383960", 1);
	if (!oid) {
		ERR_print_errors_fp(stderr);
		DIE("create oid failed");
	}

	der_len = ASN1_item_i2d((ASN1_VALUE *)set, &der, ASN1_ITEM_rptr(MAP_SET));
	CMS_signed_add1_attr_by_OBJ(si, oid, V_ASN1_SEQUENCE, der, der_len);

	err = CMS_final(cms_out, data_in, NULL, CMS_NOCERTS | CMS_BINARY);
	ERR(!err, "cms final failed");

	OPENSSL_free(der);
	MAP_SET_free(set);

	b_out = bio_open_wr(out_path);
	ERR(!b_out, "opening output path failed");

	i2d_CMS_bio_stream(b_out, cms_out, NULL, 0);

	BIO_free(data_in);
	BIO_free(b_out);
	EVP_cleanup();
	ERR_free_strings();
	return 0;
}
