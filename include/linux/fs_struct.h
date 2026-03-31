/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_STRUCT_H
#define _LINUX_FS_STRUCT_H

#include <linux/sched.h>
#include <linux/path.h>
#include <linux/spinlock.h>
#include <linux/seqlock.h>

struct fs_struct {
	seqlock_t seq;
	int users;
	int umask;
	int in_exec;
	int pwd_refs;	/* A pool of extra pwd references */
	struct path root, pwd;
} __randomize_layout;

extern struct kmem_cache *fs_cachep;

extern void exit_fs(struct task_struct *);
extern void set_fs_root(struct fs_struct *, const struct path *);
extern void set_fs_pwd(struct fs_struct *, const struct path *);
extern struct fs_struct *copy_fs_struct(struct fs_struct *);
extern void free_fs_struct(struct fs_struct *);
extern void drain_fs_pwd_pool(struct fs_struct *);
extern int unshare_fs_struct(void);

static inline void get_fs_root(struct fs_struct *fs, struct path *root)
{
	read_seqlock_excl(&fs->seq);
	*root = fs->root;
	path_get(root);
	read_sequnlock_excl(&fs->seq);
}

static inline void get_fs_pwd(struct fs_struct *fs, struct path *pwd)
{
	read_seqlock_excl(&fs->seq);
	*pwd = fs->pwd;
	path_get(pwd);
	read_sequnlock_excl(&fs->seq);
}

/* Borrow a pwd reference from the pool. Caller must hold fs->seq. */
static inline void get_fs_pwd_pool_locked(struct fs_struct *fs, struct path *pwd)
{
	*pwd = fs->pwd;
	if (fs->pwd_refs)
		fs->pwd_refs--;
	else
		path_get(pwd);
}

/*
 * Acquire a pwd reference from the pwd_refs pool, if available.
 *
 * Uses read_seqlock_excl() (writer spinlock without sequence bump) rather
 * than write_seqlock() because modifying pwd_refs does not change the path
 * values that lockless seq readers care about. Bumping the sequence counter
 * would force unnecessary retries in concurrent get_fs_pwd()/get_fs_root()
 * callers.
 */
static inline void get_fs_pwd_pool(struct fs_struct *fs, struct path *pwd)
{
	read_seqlock_excl(&fs->seq);
	get_fs_pwd_pool_locked(fs, pwd);
	read_sequnlock_excl(&fs->seq);
}

/* Release a pwd reference back to the pwd_refs pool, if appropriate. */
static inline void put_fs_pwd_pool(struct fs_struct *fs, struct path *pwd)
{
	read_seqlock_excl(&fs->seq);
	if (path_equal(&fs->pwd, pwd)) {
		fs->pwd_refs++;
		pwd = NULL;
	}
	read_sequnlock_excl(&fs->seq);
	if (pwd)
		path_put(pwd);
}

extern bool current_chrooted(void);

static inline int current_umask(void)
{
	return current->fs->umask;
}

#endif /* _LINUX_FS_STRUCT_H */
