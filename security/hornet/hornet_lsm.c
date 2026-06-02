// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hornet Linux Security Module
 *
 * Author: Blaise Boscaccy <bboscaccy@linux.microsoft.com>
 *
 * Copyright (C) 2026 Microsoft Corporation
 */

#include <linux/lsm_hooks.h>
#include <uapi/linux/lsm.h>
#include <linux/bpf.h>
#include <linux/verification.h>
#include <crypto/public_key.h>
#include <linux/module_signature.h>
#include <crypto/pkcs7.h>
#include <linux/sort.h>
#include <linux/asn1_decoder.h>
#include <linux/oid_registry.h>
#include "hornet.asn1.h"

#define MAX_USED_MAPS 64

/* The only hashing algorithm available is SHA256 due to it be hardcoded
 * in the bpf subsystem.
 */
struct hornet_prog_security_struct {
	int signed_hash_count;
	unsigned char signed_hashes[SHA256_DIGEST_SIZE * MAX_USED_MAPS];
};

struct hornet_parse_context {
	struct hornet_prog_security_struct *security;
};

struct lsm_blob_sizes hornet_blob_sizes __ro_after_init = {
	.lbs_bpf_prog = sizeof(struct hornet_prog_security_struct),
};

static inline struct hornet_prog_security_struct *
hornet_bpf_prog_security(struct bpf_prog *prog)
{
	return prog->aux->security + hornet_blob_sizes.lbs_bpf_prog;
}

int hornet_next_map(void *context, size_t hdrlen,
		     unsigned char tag,
		     const void *value, size_t vlen)
{
	struct hornet_parse_context *ctx = (struct hornet_parse_context *)context;

	ctx->security->signed_hash_count++;
	return 0;
}

int hornet_map_hash(void *context, size_t hdrlen,
		    unsigned char tag,
		    const void *value, size_t vlen)

{
	struct hornet_parse_context *ctx = (struct hornet_parse_context *)context;

	if (vlen != SHA256_DIGEST_SIZE && vlen != 0)
		return -EINVAL;
	if (ctx->security->signed_hash_count >= MAX_USED_MAPS)
		return -EINVAL;

	memcpy(&ctx->security->signed_hashes[ctx->security->signed_hash_count * SHA256_DIGEST_SIZE],
	       value, vlen);

	return 0;
}

static int hornet_check_program(struct bpf_prog *prog, union bpf_attr *attr,
				struct bpf_token *token, bool is_kernel,
				enum lsm_integrity_verdict *verdict)
{
	bpfptr_t usig = make_bpfptr(attr->signature, is_kernel);
	struct pkcs7_message *msg;
	struct hornet_parse_context *ctx;
	void *sig;
	int err;
	const void *authattrs;
	size_t authattrs_len;
	struct key *key;
	key_ref_t user_key = ERR_PTR(-ENOKEY);

	if (!attr->signature) {
		*verdict = LSM_INT_VERDICT_UNSIGNED;
		return 0;
	}

	if (!attr->signature_size) {
		*verdict = LSM_INT_VERDICT_BADSIG;
		return 0;
	}

	ctx = kzalloc(sizeof(struct hornet_parse_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->security = hornet_bpf_prog_security(prog);

	sig = kzalloc(attr->signature_size, GFP_KERNEL);
	if (!sig) {
		err = -ENOMEM;
		goto out;
	}
	err = copy_from_bpfptr(sig, usig, attr->signature_size);
	if (err != 0) {
		err = -EFAULT;
		goto cleanup_sig;
	}

	msg = pkcs7_parse_message(sig, attr->signature_size);
	if (IS_ERR(msg)) {
		*verdict = LSM_INT_VERDICT_BADSIG;
		err = 0;
		goto cleanup_sig;
	}

	if (system_keyring_id_check(attr->keyring_id) == 0)
		key = (struct key *)(unsigned long)attr->keyring_id;
	else {
		user_key = lookup_user_key(attr->keyring_id, 0, KEY_DEFER_PERM_CHECK);
		if (IS_ERR(user_key)) {
			*verdict = LSM_INT_VERDICT_UNKNOWNKEY;
			goto cleanup_msg;
		}
		key = key_ref_to_ptr(user_key);
	}

	if (verify_pkcs7_message_sig(prog->insnsi, prog->len * sizeof(struct bpf_insn), msg,
				     key,
				     VERIFYING_BPF_SIGNATURE,
				     NULL, NULL)) {
		*verdict = LSM_INT_VERDICT_BADSIG;
		err = 0;
		goto cleanup_msg;
	}

	if (pkcs7_get_authattr(msg, OID_hornet_data,
			       &authattrs, &authattrs_len) == -ENODATA) {
		*verdict = LSM_INT_VERDICT_PARTIALSIG;
		err = 0;
		goto cleanup_msg;
	}

	err = asn1_ber_decoder(&hornet_decoder, ctx, authattrs, authattrs_len);
	if (err < 0 || authattrs == NULL) {
		*verdict = LSM_INT_VERDICT_BADSIG;
		err = 0;
		goto cleanup_msg;
	}

	*verdict = LSM_INT_VERDICT_OK;
	err = 0;

cleanup_msg:
	pkcs7_free_message(msg);
	if (!IS_ERR(user_key))
		key_put(key);
cleanup_sig:
	kfree(sig);
out:
	kfree(ctx);
	return err;
}

static const struct lsm_id hornet_lsmid = {
	.name = "hornet",
	.id = LSM_ID_HORNET,
};

static int hornet_bpf_prog_load_integrity(struct bpf_prog *prog, union bpf_attr *attr,
					  struct bpf_token *token, bool is_kernel)
{
	enum lsm_integrity_verdict verdict;
	int result = hornet_check_program(prog, attr, token, is_kernel, &verdict);

	if (result < 0)
		return result;

	return security_bpf_prog_load_post_integrity(prog, attr, token, is_kernel,
						     &hornet_lsmid, verdict);
}

static int hornet_check_prog_maps(struct bpf_prog *prog)
{
	struct hornet_prog_security_struct *security;
	unsigned char hash[SHA256_DIGEST_SIZE];
	struct bpf_map *map;
	int i, j;
	bool found;

	security = hornet_bpf_prog_security(prog);

	if (!security->signed_hash_count)
		return 0;

	mutex_lock(&prog->aux->used_maps_mutex);

	/* Verify every signed map exists in used_maps */
	for (i = 0; i < security->signed_hash_count; i++) {
		found = false;
		for (j = 0; j < prog->aux->used_map_cnt; j++) {
			map = prog->aux->used_maps[j];

			if (!READ_ONCE(map->frozen) || !map->ops->map_get_hash)
				continue;

			if (map->ops->map_get_hash(map, SHA256_DIGEST_SIZE, hash))
				continue;

			if (memcmp(hash,
				   &security->signed_hashes[i * SHA256_DIGEST_SIZE],
				   SHA256_DIGEST_SIZE) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			mutex_unlock(&prog->aux->used_maps_mutex);
			return -EPERM;
		}
	}

	mutex_unlock(&prog->aux->used_maps_mutex);

	return 0;
}

static int hornet_bpf_prog(struct bpf_prog *prog)
{
	return hornet_check_prog_maps(prog);
}

static struct security_hook_list hornet_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(bpf_prog_load_integrity, hornet_bpf_prog_load_integrity),
	LSM_HOOK_INIT(bpf_prog, hornet_bpf_prog),
};

static int __init hornet_init(void)
{
	pr_info("Hornet: eBPF signature verification enabled\n");
	security_add_hooks(hornet_hooks, ARRAY_SIZE(hornet_hooks), &hornet_lsmid);
	return 0;
}

DEFINE_LSM(hornet) = {
	.id = &hornet_lsmid,
	.blobs = &hornet_blob_sizes,
	.init = hornet_init,
};
