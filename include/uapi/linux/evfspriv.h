#ifndef EVFSPRIV_H_
#define EVFSPRIV_H_

#include "evfspub.h"

enum evfs_type {
    EVFS_TYPE_INVALID,
    EVFS_TYPE_INODE,
    EVFS_TYPE_EXTENT,
    EVFS_TYPE_SUPER,
    EVFS_TYPE_DIRENT,
    EVFS_TYPE_METADATA,
};

#define EVFS_BUFSIZE (1024 * sizeof(char))
#define EVFS_MAX_NAME_LEN 256

// TODO(tbrindus): might want to switch f2fs to use this, maybe
#define EVFS_EXTENT_ALLOC_FIXED 0x1
#define EVFS_EXTENT_ALLOC_MASK (EVFS_EXTENT_ALLOC_FIXED)

#define EVFS_META_DYNAMIC 0x1
#define EVFS_META_STATIC  0x2

#define EVFS_META_FILE 		0x1
#define EVFS_META_DIRECTORY 	0x2
#define EVFS_META_INDIR 	0x3
#define EVFS_META_UNKNOWN 	0xff

#define EVFS_IMAP_UNMAP_ONLY 0x1 /* Only unmap rather than free as well */
struct __evfs_imap {
    unsigned long ino_nr;
    u32 log_blkoff;
    u32 phy_blkoff;
    unsigned long length;
    unsigned long flag;
};

struct evfs_extent_alloc_op {
    unsigned long flags;
    struct evfs_extent extent;
};

enum evfs_query {
    EVFS_ANY = 1,
    EVFS_ALL = 2,
};

struct evfs_extent_query {
    struct evfs_extent extent;
    enum evfs_query query;
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

struct evfs_meta_mv_ops {
    struct evfs_metadata md;
    unsigned long to_blkaddr;
};

struct evfs_inode_read_op {
    unsigned long ino_nr;
    unsigned long ofs;
    char *data;
    unsigned long length;
};

/*
 * For iterating
 */

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

typedef struct evfs_iter_s {
    evfs_t * evfs;
    int type;
    int flags;
    u64 count;
    u64 next_req;
    struct evfs_iter_ops op;
} evfs_iter_t;

/*
 * TODO (kyokeun): evfs_inode no longer required, since
                   we only require the ino_nr according to
                   new doc. (?)
 */
struct __evfs_ino_iter_param {
    unsigned long ino_nr;
    struct evfs_inode i;
};

struct __evfs_meta_iter {
    unsigned int id;
    struct evfs_metadata md;
};


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
#define FS_IOC_INODE_ITERATE _IOR('f', 81, struct evfs_iter_ops)
#define FS_IOC_SUPER_GET _IOR('f', 83, struct evfs_super_block)
#define FS_IOC_SUPER_SET _IOWR('f', 84, struct evfs_super_block)
#define FS_IOC_META_MOVE _IOR('f', 85, struct evfs_meta_mv_ops)
#define FS_IOC_META_ITER _IOR('f', 86, struct evfs_iter_ops)
#define FS_IOC_ATOMIC_ACTION _IOWR('f', 96, struct evfs_atomic_action_param)

#endif // EVFSPRIV_H_
