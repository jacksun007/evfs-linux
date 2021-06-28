/*
 * linux/evfs.h
 *
 * Internal kernel EVFS API -- not exported to userspace
 *
 */

#ifndef _LINUX_EVFS_H
#define _LINUX_EVFS_H

// must come before evfs.h for definition of u64 type
#include <linux/fs.h>
#include <linux/rbtree.h>
#include <uapi/linux/evfs.h>
#include <uapi/linux/evfsctl.h>

struct evfs_atomic_action;

struct evfs_op {
    // this should NOT be the execute function since it may require locking
    long (* free_extent)(struct super_block *sb, const struct evfs_extent * ext);
    long (* free_inode)(struct super_block *sb, u64 ino_nr);
};

// note that for dirent operations, the parent directory is locked
struct evfs_lockable {
    unsigned type;
    int exclusive;              // read or write lock?
    unsigned long object_id;
    unsigned long data;
};

struct evfs_atomic_op {
    long (* prepare)(struct evfs_atomic_action * aa, struct evfs_opentry * op);
    long (* lock)(struct evfs_atomic_action * aa, struct evfs_lockable * lkb);
    void (* unlock)(struct evfs_atomic_action * aa, struct evfs_lockable * lkb);
    long (* execute)(struct evfs_atomic_action * aa, struct evfs_opentry * op);
};

struct evfs_atomic_action {
    int nr_read;
    int nr_comp;

    /* used by fs to execute the atomic action */
    struct super_block * sb;
    struct file * filp;
    struct evfs_atomic_op * fsop;

    /* set to null if absent (e.g. read-only atomic action would not have
       a write_op so write_op == NULL) */
    struct evfs_opentry ** read_set;
    struct evfs_opentry ** comp_set;
    struct evfs_opentry * write_op;    /* ONLY ONE ALLOWED */

    /* data copied from userspace */
    struct evfs_atomic_action_param param;
};

/* fs/dcache.c */
extern void d_drop_entry_in_dir(struct inode *, struct qstr *);

/* fs/evfs.c */
long evfs_run_atomic_action(struct file * filp,
                            struct evfs_atomic_op *fop, void * arg);              

long evfs_open(struct file * filp, struct evfs_op * ops);
int evfs_release(struct inode * inode, struct file * filp);
       
const struct evfs_extent * evfs_find_my_extent(struct file * filp, u64 addr); 
long evfs_extent_in_range(struct file * filp, const struct evfs_extent * ext);       
long evfs_add_my_extent(struct file * filp, const struct evfs_extent * ext);
long evfs_remove_my_extent(struct file * filp, const struct evfs_extent * ext);
long evfs_list_my_extents(struct file * filp);

// note: need to kfree the structure at end of function
long evfs_imap_from_user(struct evfs_imap ** imptr, void __user * arg);
long evfs_prepare_inode_map(struct file * filp, void __user * arg);
                            
extern ssize_t evfs_page_read_iter(struct inode *, loff_t *, struct iov_iter *,
		ssize_t, struct page *(*)(struct address_space *, pgoff_t));
extern ssize_t evfs_page_write_iter(struct inode *, loff_t *, struct iov_iter *,
		ssize_t, struct page *(*)(struct address_space *, pgoff_t));
extern long evfs_inode_read(struct super_block * sb, void __user * arg,
		struct page * (*page_cb) (struct address_space *, pgoff_t));
extern long evfs_inode_write (struct super_block * sb, void __user * arg,
		struct page * (*page_cb) (struct address_space *, pgoff_t));

extern long evfs_extent_write(struct super_block * sb, void __user * arg);

extern ssize_t evfs_perform_write(struct super_block *,
		struct iov_iter *, pgoff_t);
extern int evfs_copy_param(struct evfs_iter_ops *, const void *, int);

static 
inline void 
evfs_imap_to_extent(struct evfs_extent * ex, struct evfs_imentry * im)
{
    ex->addr = im->phy_addr;
    ex->len = im->len;
}

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

