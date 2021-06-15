/*
 * evfs.h
 *
 * Linux Evfs kernel API (public -- also used by application developer)
 *
 */ 

#ifndef UAPI_EVFS_H_
#define UAPI_EVFS_H_

// defined by int-ll64.h which is included by <linux/fs.h>
// typedef unsigned long u64; 

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long i64;
typedef int i32;

struct evfs_extent {
    u64 addr;   // block address
    u64 len;    // number of blocks
};

struct evfs_super_block {
    u64 max_extent_size;  /* maximum allowed size of a given extent */
    u64 max_bytes;        /* max file size */
    u64 block_count;      /* total number of data blocks available */
    u64 root_ino;         /* root inode number */
    u64 block_size;
};

struct evfs_inode_property {
    u32 refcount;       // link count
    u64 blockcount;     // number of blocks used
    u64 inlined_bytes;  // number of bytes inlined in the file system
};

struct evfs_timeval {
    u64 tv_sec;
    u64 tv_usec;
};

struct evfs_inode {
    u64 ino_nr;

    struct evfs_timeval atime;
    struct evfs_timeval ctime;
    struct evfs_timeval mtime;
    struct evfs_timeval otime;

    u32 /* kuid_t */ uid;
    u32 /* kgid_t */ gid;
    u16 /* umode_t */ mode;
    u32 flags;
    u64 bytesize;

    union {
        const struct evfs_inode_property prop;  /* users read this */
        struct evfs_inode_property _prop;       /* kernel modify this */
    };
};

struct evfs_metadata {
    u64 blkaddr;
    u64 size;   // Block-level granularity
    u64 owner;  // TODO: Does it make sense for this to be just inode? Maybe optional?
    u32 loc_type;
    u32 type;

    /* TODO: In order to be able to move metadata, the user needs
     *       some idea of where they can move the data to. Returning
     *       this "meta region" should help with that, but not entirely
     *       sure if it makes sense to be in this struct.
     */
    u64 region_start;
    u64 region_len;
};

// mapping entry
struct evfs_imentry {
    u32 inlined;    // this map entry is inlined (e.g., tail-packed)
    u64 log_addr;
    u64 phy_addr;
    u64 len;
};

struct evfs_imap {
    u32 count;      // number of active entries
    u32 capacity;   // number of entries that fits (i.e. preallocated)
    struct evfs_imentry entry[];
};

// TODO: put ALL the possible comparable fields here
enum evfs_field {
    EVFS_FIELD_INVALID = 0,

    EVFS_INODE_FIELD_BEGIN,

    EVFS_INODE_MTIME_SEC = EVFS_INODE_FIELD_BEGIN,
    EVFS_INODE_MTIME_USEC,

    EVFS_INODE_FIELD_END,

    // TODO: add fields of other objects
};

#endif // UAPI_EVFS_H_

