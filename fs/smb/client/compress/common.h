/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026, SUSE LLC
 *
 * Authors: Enzo Matsumiya <ematsumiya@suse.de>
 *
 * Common helpers and definitions for compression/decompression.
 *
 * Naming convention:
 * - smb_compress_*:	MS-SMB2 related API
 * - compress_*:	Compression internals
 * - <alg>_*:		Compression algorithm specifics
 *
 * Upper case for macros and constants, lower case for everything else.
 */
#ifndef _COMPRESS_COMMON_H
#define _COMPRESS_COMMON_H

#include <linux/kernel.h>
#include <linux/count_zeros.h>
#include <linux/string.h>
#include <linux/sizes.h>
#include <linux/slab.h>

/*
 * Build time checks/asserts.
 * These are assumptions/expectations for all algorithms implemented.
 */
#ifndef __LITTLE_ENDIAN /* TODO */
# error "SMB3 compression is only supported on little endian architectures"
#endif /* !__LITTLE_ENDIAN */

#if BITS_PER_LONG < 64 /* TODO */
# error "SMB3 compression is only supported on 64-bit architectures"
#endif /* BITS_PER_LONG < 64 */

/* Build time double checks (probably unnecessary) */
static_assert(sizeof(u16) == 2);
static_assert(sizeof(u32) == 4);
static_assert(sizeof(u64) == 8);
static_assert(sizeof(size_t) == 8);

#ifdef CONFIG_CIFS_DEBUG2
# define COMPRESS_DEBUG
#endif /* CONFIG_CIFS_DEBUG2 */

#define COMPRESS_LOG_FMT	"CIFS: %s:%d %s(): "

#ifdef COMPRESS_DEBUG
# define compress_log(fmt, ...) \
	pr_err(COMPRESS_LOG_FMT fmt, __FILE__, __LINE__, __func__, ## __VA_ARGS__)

/* Make sure to handle assertion failures! */
# define COMPRESS_ASSERT(cond) \
	!WARN_ONCE(!(cond), COMPRESS_LOG_FMT "assertion failed: %s\n", \
		   __FILE__, __LINE__, __func__, #cond)
#else /* COMPRESS_DEBUG */
# define compress_log(...)
/* XXX: should not fail silently on non-debug? */
# define COMPRESS_ASSERT(cond)	likely(cond)
#endif /* !COMPRESS_DEBUG */

/*
 * Memory ops helpers.
 */
#undef MEM_UNALIGNED_READ
#undef MEM_UNALIGNED_WRITE

/* Read prefetch */
#define MEM_PREFETCH(ptr)		__builtin_prefetch((ptr), 0, 3)

#ifdef CONFIG_X86
# define MEM_UNALIGNED_READ(ptr, t)	(*(const t *)(ptr))
# define MEM_UNALIGNED_WRITE(ptr, v, t)	(*(t *)(ptr) = (t)(v))
#else
# include <linux/unaligned.h>
# define MEM_UNALIGNED_READ(ptr, t)	get_unaligned((const t *)(ptr))
# define MEM_UNALIGNED_WRITE(ptr, v, t)	put_unaligned((v), (t *)(ptr))
#endif /* !CONFIG_X86 */

#define mem_read8(ptr)			MEM_UNALIGNED_READ(ptr, u8)
#define mem_read16(ptr)			MEM_UNALIGNED_READ(ptr, u16)
#define mem_read32(ptr)			MEM_UNALIGNED_READ(ptr, u32)
#define mem_read64(ptr)			MEM_UNALIGNED_READ(ptr, u64)
#define mem_write8(ptr, v)		MEM_UNALIGNED_WRITE(ptr, v, u8)
#define mem_write16(ptr, v)		MEM_UNALIGNED_WRITE(ptr, v, u16)
#define mem_write32(ptr, v)		MEM_UNALIGNED_WRITE(ptr, v, u32)
/* mem_write64() not implemented because it's not used anywhere. */

/*
 * COMPRESS_RSTEP_SIZE:		Number of bytes to read from input buffer for hashing and initial
 *				match check (default 4 bytes).
 * COMPRESS_MSTEP_SIZE:		Number of bytes to extend-compare a found match (default 8 bytes).
 */
#define COMPRESS_RSTEP_SIZE	sizeof(u32)
#define COMPRESS_MSTEP_SIZE	sizeof(u64)

static __always_inline size_t mem_match_len(const void *match, const void *cur, const void *end)
{
	const void *start = cur;

	/* Callers must ensure @cur + COMPRESS_MSTEP_SIZE < @end. */
	do {
		const u64 diff = mem_read64(cur) ^ mem_read64(match);

		if (!diff) {
			cur += COMPRESS_MSTEP_SIZE;
			match += COMPRESS_MSTEP_SIZE;

			continue;
		}

		/* This computes the number of common bytes in @diff. */
		cur += count_trailing_zeros(diff) >> 3;

		return (cur - start);
	} while (likely(cur + COMPRESS_MSTEP_SIZE <= end));

	/* Fallback to byte-by-byte comparison for last bytes (< COMPRESS_MSTEP_SIZE). */
	while (cur < end && mem_read8(match) == mem_read8(cur)) {
		cur++;
		match++;
	}

	return (cur - start);
}

/*
 * Hashing
 *
 * Same for all algorithms.
 *
 * XXX: these are fixed for now, might make them tunables in the future.
 */

/*
 * COMPRESS_HASH_LOG:		ilog2 hash size (recommended to be 13 - 18, default 15)
 * COMPRESS_HASH_SIZE:		Hashtable size (default is 32k (1 << COMPRESS_HASH_LOG))).
 */
#define COMPRESS_HASH_LOG	15
#define COMPRESS_HASH_SIZE	(1U << COMPRESS_HASH_LOG)

static __always_inline u32 compress_hash(const u32 v)
{
	return ((v ^ 0x9E3779B9U) * 0x85EBCA6BU) >> (32 - COMPRESS_HASH_LOG);
}

static __always_inline u32 compress_hash_ptr(const void *ptr)
{
	return compress_hash(mem_read32(ptr));
}
#endif /* _COMPRESS_COMMON_H */
