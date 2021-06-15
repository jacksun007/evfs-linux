/*
 * linux/evfs.h
 *
 * kernel EVFS API
 *
 */

#ifndef _LINUX_EVFS_H
#define _LINUX_EVFS_H

#include <uapi/linux/evfs.h>
#include <linux/fs.h>

/* fs/dcache.c */
extern void d_drop_entry_in_dir(struct inode *, struct qstr *);

/* fs/evfs.c */
long evfs_run_atomic_action(struct super_block * sb, struct evfs_atomic_op *fop,
                            void * arg);

extern ssize_t evfs_page_read_iter(struct inode *, loff_t *, struct iov_iter *,
		ssize_t, struct page *(*)(struct address_space *, pgoff_t));
extern ssize_t evfs_perform_write(struct super_block *,
		struct iov_iter *, pgoff_t);
extern int evfs_copy_param(struct evfs_iter_ops *, const void *, int);

static inline void evfs_timeval_to_timespec(struct evfs_timeval *in,
		struct timespec *out)
{
	out->tv_nsec = in->tv_usec * 1000000;
	out->tv_sec = in->tv_sec;
}

static inline void evfs_timespec_to_timeval(struct timespec *in,
		struct evfs_timeval *out)
{
	out->tv_usec = in->tv_nsec / 1000000;
	out->tv_sec = in->tv_sec;
}

static inline void
vfs_to_evfs_inode(struct inode *inode, struct evfs_inode *evfs_i)
{
	evfs_i->ino_nr = inode->i_ino;
	evfs_i->mode = inode->i_mode;
	evfs_i->flags = inode->i_flags;
	evfs_timespec_to_timeval(&inode->i_atime, &evfs_i->atime);
	evfs_timespec_to_timeval(&inode->i_ctime, &evfs_i->ctime);
	evfs_timespec_to_timeval(&inode->i_mtime, &evfs_i->mtime);
	evfs_i->gid = i_gid_read(inode);
	evfs_i->uid = i_uid_read(inode);
	evfs_i->bytesize = i_size_read(inode);
	evfs_i->_prop.blockcount = inode->i_blocks;
	evfs_i->_prop.refcount = atomic_read(&inode->i_count);
	evfs_i->_prop.inlined_bytes = 0; /* FS-specific code should take care of it */
}

static inline void
evfs_to_vfs_inode(struct evfs_inode *evfs_i, struct inode *inode)
{
	inode->i_mode = evfs_i->mode;
	inode->i_flags = evfs_i->flags;
	inode->i_size = evfs_i->bytesize;
	evfs_timeval_to_timespec(&evfs_i->atime, &inode->i_atime);
	evfs_timeval_to_timespec(&evfs_i->ctime, &inode->i_ctime);
	evfs_timeval_to_timespec(&evfs_i->mtime, &inode->i_mtime);
	i_uid_write(inode, evfs_i->uid);
	i_gid_write(inode, evfs_i->gid);
}


#endif

