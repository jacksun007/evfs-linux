#ifndef STRUCT_H_
#define STRUCT_H_

#include <evfs.h>

enum evfs_type {
    EVFS_TYPE_INVALID,
    EVFS_TYPE_INODE,
    EVFS_TYPE_EXTENT,
    EVFS_TYPE_SUPER,
    EVFS_TYPE_DIRENT,
    EVFS_TYPE_METADATA,
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

typedef struct evfs_iter_s {
    evfs_t * evfs;
    int type;
    int flags;
    u64 count;
    u64 next_req;
    struct evfs_iter_ops op;
} evfs_iter_t;

/*
 * For iterating
 */

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

