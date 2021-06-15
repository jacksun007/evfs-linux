/*
 * linux/evfs.h
 *
 * kernel EVFS API
 *
 */

#ifndef _LINUX_EVFS_H
#define _LINUX_EVFS_H

#include <uapi/linux/evfspriv.h>
#include <linux/fs.h>

/*
 * Evfs operation naming conventions
 *
 * alloc  - allocate the object
 * free   - free the object
 * read   - read user data from the object (for extent and inode only) write  - write user data to the object info   - read metadata information from the object (replaces stat and get).
 *        - reason: 'get' is easily confused with its association with 'put'.
 * update - update metadata information for the object.
 *
 */
enum evfs_opcode {
    EVFS_OPCODE_INVALID = 0,

    // compare operations
    EVFS_COMP_OP_BEGIN,

    EVFS_CONST_EQUAL = EVFS_COMP_OP_BEGIN, // compare field with a constant
    EVFS_FIELD_EQUAL,                      // compare field with another field

    EVFS_COMP_OP_END,

    // read operations
    EVFS_READ_OP_BEGIN = EVFS_COMP_OP_END,

    EVFS_INODE_INFO = EVFS_READ_OP_BEGIN,
    EVFS_SUPER_INFO,
    EVFS_DIRENT_INFO,

    EVFS_EXTENT_READ,   // read raw data from extent
    EVFS_INODE_READ,    // same as posix read()

    EVFS_READ_OP_END,

    // write operations
    EVFS_WRITE_OP_BEGIN = EVFS_READ_OP_END,

    EVFS_INODE_UPDATE = EVFS_WRITE_OP_BEGIN,
    EVFS_SUPER_UPDATE,
    EVFS_DIRENT_UPDATE,

    EVFS_EXTENT_ALLOC,
    EVFS_INODE_ALLOC,

    EVFS_EXTENT_WRITE,
    EVFS_INODE_WRITE,

    // Note: the identifier for dirents is its filename + parent inode
    EVFS_DIRENT_ADD,
    EVFS_DIRENT_REMOVE,
    EVFS_DIRENT_RENAME, // unlike update, this *keeps* content but changes id

    // inode-specific operations
    EVFS_INODE_MAP,

    EVFS_WRITE_OP_END,
};

struct evfs_ext_write_op {
    u32 addr;
    unsigned long length;
    char *data;
};

// note that for dirent operations, the parent directory is locked
struct evfs_lockable {
    unsigned type;
    int exclusive;  // read or write lock?
    unsigned long object_id;
};

struct evfs_opentry {
    int code;
    int id;
    void * data;
};

struct evfs_atomic_action_param {
    /* userspace skips 'header' when passing struct to kernel */
    int count;
    int capacity;
    int errop;
    struct evfs_opentry item[];
};

struct evfs_atomic_action {
    int nr_read;
    int nr_comp;

    /* used by fs to execute the atomic action */
    struct super_block * sb;

    /* set to null if absent (e.g. read-only atomic action would not have
       a write_op so write_op == NULL) */
    struct evfs_opentry ** read_set;
    struct evfs_opentry ** comp_set;
    struct evfs_opentry * write_op;    /* ONLY ONE ALLOWED */

    /* data copied from userspace */
    struct evfs_atomic_action_param param;
};

struct evfs_atomic_op {
    long (* lock)(struct evfs_atomic_action * aa, struct evfs_lockable * lkb);
    void (* unlock)(struct evfs_atomic_action * aa, struct evfs_lockable * lkb);
    long (* execute)(struct evfs_atomic_action * aa, struct evfs_opentry * op);
};

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

