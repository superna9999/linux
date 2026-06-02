// SPDX-License-Identifier: GPL-2.0-or-later
/* Testing module to load key from trusted PKCS#7 message
 *
 * Copyright (C) 2014 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) "PKCS7key: "fmt
#include <linux/key.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/verification.h>
#include <linux/key-type.h>
#include <keys/user-type.h>
#include <crypto/pkcs7.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PKCS#7 testing key type");
MODULE_AUTHOR("Red Hat, Inc.");

static unsigned pkcs7_usage;
module_param_named(usage, pkcs7_usage, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(pkcs7_usage,
		 "Usage to specify when verifying the PKCS#7 message");

/*
 * Retrieve the PKCS#7 message content.
 */
static int pkcs7_view_content(void *ctx, const void *data, size_t len,
			      size_t asn1hdrlen)
{
	struct key_preparsed_payload *prep = ctx;
	const void *saved_prep_data;
	size_t saved_prep_datalen;
	int ret;

	saved_prep_data = prep->data;
	saved_prep_datalen = prep->datalen;
	prep->data = data;
	prep->datalen = len;

	ret = user_preparse(prep);

	prep->data = saved_prep_data;
	prep->datalen = saved_prep_datalen;
	return ret;
}

/*
 * Preparse a PKCS#7 wrapped and validated data blob.
 */
static int pkcs7_preparse(struct key_preparsed_payload *prep)
{
	enum key_being_used_for usage = pkcs7_usage;
	int ret;
	struct pkcs7_message *pkcs7;
	const void *data;
	size_t len;

	if (usage >= NR__KEY_BEING_USED_FOR) {
		pr_err("Invalid usage type %d\n", usage);
		return -EINVAL;
	}

	ret = verify_pkcs7_signature(NULL, 0,
				      prep->data, prep->datalen,
				      VERIFY_USE_SECONDARY_KEYRING, usage,
				      pkcs7_view_content, prep);
	if (ret)
		return ret;

	pkcs7 = pkcs7_parse_message(prep->data, prep->datalen);
	if (IS_ERR(pkcs7)) {
		pr_err("pkcs7 parse error\n");
		return PTR_ERR(pkcs7);
	}

	/*
	 * the parsed message has no trusted signer, so nothing should
	 * be returned here
	 */
	ret = pkcs7_get_authattr(pkcs7, OID_messageDigest, &data, &len);
	if (ret == 0) {
		pr_err("OID returned when no trust in signer\n");
		goto out;
	}
	/* add trust and check again */
	ret = verify_pkcs7_message_sig(NULL, 0, pkcs7,
				       VERIFY_USE_SECONDARY_KEYRING, usage,
				       NULL, NULL);
	if (ret) {
		pr_err("verify_pkcs7_message_sig failed!!\n");
		goto out;
	}
	/* now we should find the OID */
	ret = pkcs7_get_authattr(pkcs7, OID_messageDigest, &data, &len);
	if (ret) {
		pr_err("Failed to get message digest\n");
		goto out;
	}
	pr_info("Correctly Got message hash, size=%zu\n", len);

 out:
	pkcs7_free_message(pkcs7);
	return 0;
}

/*
 * user defined keys take an arbitrary string as the description and an
 * arbitrary blob of data as the payload
 */
static struct key_type key_type_pkcs7 = {
	.name			= "pkcs7_test",
	.preparse		= pkcs7_preparse,
	.free_preparse		= user_free_preparse,
	.instantiate		= generic_key_instantiate,
	.revoke			= user_revoke,
	.destroy		= user_destroy,
	.describe		= user_describe,
	.read			= user_read,
};

/*
 * Module stuff
 */
static int __init pkcs7_key_init(void)
{
	return register_key_type(&key_type_pkcs7);
}

static void __exit pkcs7_key_cleanup(void)
{
	unregister_key_type(&key_type_pkcs7);
}

module_init(pkcs7_key_init);
module_exit(pkcs7_key_cleanup);
