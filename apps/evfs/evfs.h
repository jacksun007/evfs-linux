/*
 * evfs.h
 *
 * Userspace Evfs library API
 *
 */
  
#ifndef EVFS_H
#define EVFS_H

typedef unsigned long long u64;
typedef unsigned int u32;
typedef long long i64;
typedef int i32;

typedef struct {
    int fd;
} evfs_t;

typedef struct {
    evfs_t * evfs;
    int type;
} evfs_iter_t;

struct evfs_extent {
    u64 addr;   // block address
    u64 len;    // number of blocks
};

struct evfs_super_block {
    u64 max_extent_size;  /* maximum allowed size of a given extent */
    u64 max_bytes;        /* max file size */
    u64 block_count;      /* total number of data blocks available */
    u64 root_ino;         /* root inode number */
};

struct evfs_inode_property {
    int inlined;        // does inode have inlined data
    int refcount;       // link count
    long blockcount;    // number of blocks used
    long bytesize;      // size of inode, in bytes
};

struct evfs_timeval {
    u64 tv_sec;
    u64 tv_usec;
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

// basic open/close for evfs device
evfs_t * evfs_open(const char * dev);
void evfs_close(evfs_t * evfs);

// iterators
evfs_iter_t * inode_iter(evfs_t * evfs, int flags);
evfs_iter_t * extent_iter(evfs_t * evfs, int flags);    // free space
u64 inode_next(evfs_iter_t * it);
void iter_end(evfs_iter_t * it);

// super block
int super_info(evfs_t * evfs, struct evfs_super_block * sb);

// inode
int inode_info(evfs_t * evfs, struct evfs_inode * inode);

#endif

