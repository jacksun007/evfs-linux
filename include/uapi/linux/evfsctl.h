/*
 * evfsctl.h
 *
 * Linux Evfs kernel API (private -- only used by library developer)
 *
 * Add more definition here for enums and structures shared between
 * evfs library and kernel, BUT NOT seen by evfs application developers.
 *
 * IMPORTANT: you MUST include uapi/linux/evfs.h before this file. i.e.
 *
 * #include <uapi/linux/evfs.h>
 * #include <uapi/linux/evfsctl.h>
 * 
 */ 

#ifndef UAPI_EVFSCTL_H_
#define UAPI_EVFSCTL_H_

enum evfs_type {
    EVFS_TYPE_INVALID,
    EVFS_TYPE_INODE,
    EVFS_TYPE_EXTENT,
    EVFS_TYPE_SUPER,
    EVFS_TYPE_DIRENT,
    EVFS_TYPE_METADATA,
    EVFS_TYPE_EXTENT_GROUP,     // e.g. segment, block group
    EVFS_TYPE_INODE_GROUP,      // e.g. block group
};

#define EVFS_BUFSIZE (512 * sizeof(char))
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

struct evfs_extent_op {
    struct evfs_extent extent;
    u64 flags;
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

// all iteration operations 

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

// atomic compare

struct evfs_const_comp {
    int id;
    int field;
    u64 rhs;
};

// atomic action

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
    
    EVFS_INODE_ACTIVE,
    EVFS_DIRENT_ACTIVE,
    EVFS_EXTENT_ACTIVE,

    EVFS_INODE_READ,    // same as posix read()
    EVFS_EXTENT_READ,   // read raw data from extent

    EVFS_READ_OP_END,

    // write operations
    EVFS_WRITE_OP_BEGIN = EVFS_READ_OP_END,
    
    EVFS_INODE_UPDATE = EVFS_WRITE_OP_BEGIN,
    EVFS_SUPER_UPDATE,
    EVFS_DIRENT_UPDATE,
    
    EVFS_INODE_ALLOC,
    EVFS_EXTENT_ALLOC,
        
    EVFS_INODE_WRITE,
    EVFS_EXTENT_WRITE,
    
    EVFS_INODE_FREE,
    EVFS_EXTENT_FREE,  

    // Note: the identifier for dirents is its filename + parent inode
    EVFS_DIRENT_ADD,
    EVFS_DIRENT_REMOVE,
    EVFS_DIRENT_RENAME, // unlike update, this *keeps* content but changes id

    // inode-specific operations
    EVFS_INODE_MAP,

    EVFS_WRITE_OP_END,
};

struct evfs_opentry {
    int code;
    int id;
    void * data;
    u64 result;
};

struct evfs_atomic_action_param {
    int count;
    int capacity;
    int errop;
    struct evfs_opentry item[];
};

// not sure where this goes

struct evfs_ext_write_op {
    u32 addr;
    unsigned long length;
    char *data;
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
#define FS_IOC_EVFS_OPEN _IOR('f', 97, long)
#define FS_IOC_LIST_MY_EXTENTS _IOR('f', 98, long)

#endif // UAPI_EVFSCTL_H_

