#ifndef STRUCT_H_
#define STRUCT_H_

#include <evfs.h>

/*
 * TODO: Probably want to define these in a separate header file, that is
 *       not exposed to the application developer? It seems like this file
 *       will quickly become cluttered otherwise.
 */

enum evfs_type {
    EVFS_TYPE_INODE,
    EVFS_TYPE_EXTENT,
    EVFS_TYPE_SUPER,
    EVFS_TYPE_DIRENT,
    EVFS_TYPE_METADATA,
};

enum evfs_opcode {
    EVFS_INVALID_OPCODE = 0,

    // TODO: merge with Shawn's work
    EVFS_CONST_EQUAL,   // compare field with a constant
    EVFS_FIELD_EQUAL,   // compare field with another field
};

struct evfs_read_op {
    int opcode;

    union {
        struct evfs_inode inode;    /* for inode_info */
        struct evfs_extent extent;  /* for extent-related operations */
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

typedef struct evfs_atomic_action {
    int nr_read;
    int nr_comp;

    /* set to null if absent (e.g. read-only atomic action would not have
       a write_op so write_op == NULL) */
    struct evfs_read_op * read_set;
    struct evfs_comp_op * comp_set;
    struct evfs_write_op * write_op;    /* ONLY ONE ALLOWED */

    int err;        /* error number */
    int errop;      /* error operation (index number) */
} atomic_action_t;

/*
 * For iterating
 */
struct __evfs_ext_iter_param {
    u32 log_blkoff;
    u32 phy_blkoff;
    unsigned long length;
};

struct __evfs_fsp_iter_param {
    u32 addr;
    unsigned long length;
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

#endif // STRUCT_H_
