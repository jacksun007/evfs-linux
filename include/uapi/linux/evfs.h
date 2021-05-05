#ifndef _UAPI_LINUX_EVFS_H
#define _UAPI_LINUX_EVFS_H

#include <linux/limits.h>
#include <linux/ioctl.h>
#include <linux/types.h>

enum evfs_type {
    EVFS_TYPE_INODE,
    EVFS_TYPE_EXTENT,
    EVFS_TYPE_SUPER,
    EVFS_TYPE_DIRENT,
    EVFS_TYPE_METADATA,
};

typedef unsigned int u32;

/*
 * This file has definitions for all evfs APIs and data structures.
 */

struct evfs_extent {
    unsigned long start;     // if set to zero, means allocate any of length
    unsigned long length;
    unsigned long ino_nr;
};

struct evfs_super_block {
    unsigned long max_extent; /* maximum allowed size of a given extent */
    unsigned long max_bytes; /* max file size */
    unsigned long page_size;
    unsigned long root_ino; /* root inode number */
};

enum evfs_query {
    EVFS_ANY = 1,
    EVFS_ALL = 2,
};

struct evfs_extent_query {
    struct evfs_extent extent;
    enum evfs_query query;
};

/* TODO: compact this structure */
struct evfs_inode_property {
    int inlined;        // does inode have inlined data
    int refcount;       // link count
    long blockcount;    // number of blocks used
    long bytesize;      // size of inode, in bytes
};

/* TODO: replace with kernel's timespec? */
struct evfs_timeval {
    long tv_sec;
    long tv_usec;
};

struct evfs_inode {
    unsigned long ino_nr;

    struct evfs_timeval atime;
    struct evfs_timeval ctime;
    struct evfs_timeval mtime;
    struct evfs_timeval otime;

    int /* kuid_t */ uid;
    int /* kgid_t */ gid;
    unsigned short /* umode_t */ mode;
    unsigned int flags;

    union {
        const struct evfs_inode_property prop;  /* users read this */
        struct evfs_inode_property _prop;       /* kernel modify this */
    };
};

struct evfs_imap {
    unsigned long ino_nr;
    u32 log_blkoff;
    u32 phy_blkoff;
    unsigned long length;
};

struct evfs_inode_read_op {
    unsigned long ino_nr;
    unsigned long ofs;
    char *data;
    unsigned long length;
};

#define EVFS_MAX_NAME_LEN 256

enum evfs_file_type {
    REGULAR_FILE,
    DIRECTORY
};

struct evfs_dirent_add_op {
    long dir_nr;
    long ino_nr;
    int name_len;
    int file_type;
    char name[EVFS_MAX_NAME_LEN];
};

struct evfs_dirent_del_op {
    long dir_nr;
    char name[EVFS_MAX_NAME_LEN];
};

struct evfs_extent_iter {
    long ino_nr;
    void *priv;
    long (*cb) (u32 log_blkoff, u32 phy_blkoff,
                unsigned long length, void *priv);
};

struct evfs_freesp_iter {
    void *priv;
    long (*cb) (unsigned long addr, unsigned long length, void *priv);
};

struct evfs_inode_iter {
    void *priv;
    long (*cb) (unsigned long ino_nr, struct evfs_inode *i, void *priv);
};

// TODO(tbrindus): might want to switch f2fs to use this, maybe
#define EVFS_EXTENT_ALLOC_FIXED 0x1
#define EVFS_EXTENT_ALLOC_MASK (EVFS_EXTENT_ALLOC_FIXED)

struct evfs_extent_alloc_op {
    unsigned long ino_nr;
    unsigned long flags;
    struct evfs_extent extent;
};


struct __evfs_ext_iter_param {
    u32 log_blkoff;
    u32 phy_blkoff;
    unsigned long length;
};

struct __evfs_fsp_iter_param {
    u32 addr;
    unsigned long length;
};

struct __evfs_ino_iter_param {
    unsigned long ino_nr;
    struct evfs_inode i;
};

struct evfs_ext_write_op {
    u32 addr;
    unsigned long length;
    char *data;
};

#define EVFS_BUFSIZE (1024 * sizeof(char))

/*
 * Struct to pass into the ioctl call for all iterate calls.
 * buffer will be used to hold <count> many parameters.
 *
 * Note that start_from represents different things for different
 * iterate calls:
 *     - Extent iterate: logical block offset
 *     - Freesp iterate: physical block offset
 *     - Inode iterate: inode number
 *
 * Furthermore, iterate calls should return:
 *     - 1, if there are more items left
 *     - 0, if there are no more items to iterate
 */
struct evfs_iter_ops {
    char buffer[EVFS_BUFSIZE];
    unsigned long count; /* Number of parameters that resides in the buffer */
    unsigned long start_from;
    unsigned long ino_nr; /* Used for extent iter, ignored by rest */
};

/*
 * Evfs operation naming conventions
 *
 * alloc  - allocate the object
 * free   - free the object
 * read   - read user data from the object (for extent and inode only)
 * write  - write user data to the object
 * info   - read metadata information from the object (replaces stat and get).
 *        - reason: 'get' is easily confused with its association with 'put'.
 * update - update metadata information for the object.
 *
 */
enum evfs_opcode {
    // read operations
    EVFS_SUPER_INFO,
    EVFS_INODE_INFO,
    EVFS_DIRENT_INFO,
    
    EVFS_EXTENT_READ,   // read raw data from extent
    EVFS_INODE_READ,    // same as posix read()
    
    // compare operations
    // TODO: merge with Shawn's work
    
    // write operations
    EVFS_EXTENT_ALLOC,
    EVFS_INODE_ALLOC,
    
    EVFS_EXTENT_WRITE,  
    EVFS_INODE_WRITE,
    
    // Note: the identifier for dirents is its filename + parent inode
    EVFS_DIRENT_ADD,
    EVFS_DIRENT_REMOVE,
    EVFS_DIRENT_UPDATE,
    EVFS_DIRENT_RENAME, // unlike update, this *keeps* content but changes id
    
    // inode-specific operations
    EVFS_INODE_MAP,     // also "remaps"
    EVFS_INODE_UNMAP,
    EVFS_INODE_UPDATE,
    
    EVFS_SUPER_UPDATE,
};

struct evfs_read_op {
    int opcode;
    
    union {
        struct evfs_inode inode;    /* for inode_info */
    };
};

struct evfs_comp_op {
    int opcode;  
};

struct evfs_write_op {
    int opcode;
    
    union {
        struct evfs_inode inode;    /* for inode_update */
    };
};

// note that for dirent operations, the parent directory is locked
struct evfs_lockable {
    unsigned type;
    unsigned long object_id;
    int exclusive;  // read or write lock?
};

struct evfs_atomic_action {
    int nr_read;
    int nr_comp;
    
    /* set to null if absent (e.g. read-only atomic action would not have
       a write_op so write_op == NULL) */
    struct evfs_read_op * read_set;
    struct evfs_comp_op * comp_set;
    struct evfs_write_op * write_op;    /* ONLY ONE ALLOWED */
    
    int err;        /* error number */
    int errop;      /* error operation (index number) */
};

/*
 * NOTE: EXPERIMENTAL
 */
#define FS_IOC_INODE_PREALLOC _IOR('f', 123, long)

/*
 * evfs ioctl commands
 *
 * double check that these command numbers don't overlap existing in
 * <uapi/linux/fs.h> before adding
 *
 * TODO (jsun): clean-up everything. only EVFS_ACTION should be kept.
 * 
 */
#define FS_IOC_INODE_LOCK _IOR('f', 64, long)
#define FS_IOC_INODE_UNLOCK _IOR('f', 65, long)
#define FS_IOC_EXTENT_ALLOC _IOWR('f', 66, struct evfs_extent_alloc_op)
#define FS_IOC_EXTENT_ACTIVE _IOWR('f', 67, struct evfs_extent_query)
#define FS_IOC_EXTENT_FREE _IOR('f', 68, struct evfs_extent)
#define FS_IOC_EXTENT_WRITE _IOR('f', 69, struct evfs_extent)
#define FS_IOC_INODE_ALLOC _IOWR('f', 70, struct evfs_inode)
#define FS_IOC_INODE_FREE _IOWR('f', 71, long)
#define FS_IOC_DIRENT_ADD _IOR('f', 72, struct evfs_dirent_add_op)
#define FS_IOC_DIRENT_REMOVE _IOR('f', 73, struct evfs_dirent_del_op)
#define FS_IOC_INODE_STAT _IOR('f', 74, long)
#define FS_IOC_INODE_GET _IOR('f', 75, struct evfs_inode)
#define FS_IOC_INODE_SET _IOWR('f', 76, struct evfs_inode)
#define FS_IOC_INODE_READ _IOWR('f', 77, struct evfs_inode_read_op)
#define FS_IOC_INODE_MAP _IOR('f', 78, struct evfs_imap)
#define FS_IOC_INODE_UNMAP _IOR('f', 79, struct evfs_imap)
#define FS_IOC_EXTENT_ITERATE _IOR('f', 80, struct evfs_iter_ops)
#define FS_IOC_FREESP_ITERATE _IOR('f', 81, struct evfs_iter_ops)
#define FS_IOC_INODE_ITERATE _IOR('f', 82, struct evfs_iter_ops)
#define FS_IOC_SUPER_GET _IOR('f', 83, struct evfs_super_block)
#define FS_IOC_EVFS_ACTION _IOWR('f', 96, struct evfs_atomic_action)

// fs/evfs.c
void evfs_destroy_atomic_action(struct evfs_atomic_action * aa);
long evfs_get_user_atomic_action(struct evfs_atomic_action * aout, void * arg);

#endif /* _UAPI_LINUX_EVFS_H */

