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
    u64 addr;       // block address
    u64 len;        // number of blocks
};

struct evfs_group {
    u64 addr;       
    u64 len;
    u64 block_count;    // number of blocks used
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

struct evfs_rmentry {
    u64 ino_nr;     // if 0, does not belong to any inode
    u64 log_addr;   // if type is data, refers to logical address
};

struct evfs_rmap {
    u64 phy_addr;
    u64 len;
    u32 type;       // if 0, refers to data mapping
    u16 count;
    u16 capacity;
    struct evfs_rmentry entry[];
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
    
    /*
    u64 log_addr;
    u64 phy_addr;
    u64 len;
    u64 ino_nr;
    u32 type;
    */
};

struct evfs_extent_attr {
    u32 flags;
    u16 type;
    unsigned metadata : 1;
    struct evfs_extent range;
};

// mapping entry
struct evfs_imentry {
    u64 log_addr;
    u64 phy_addr;
    u64 len;
    u32 index;
    unsigned inlined  : 1;  // this map entry is inlined (e.g., tail-packed)
    unsigned assigned : 1;  // this map entry is assigned to an inode
};

struct evfs_imap {
    u32 count;      // number of active entries
    u32 capacity;   // number of entries that fits (i.e. preallocated)
    struct evfs_imentry entry[];
};

// TODO: put ALL the possible comparable fields here
enum evfs_field {
    EVFS_FIELD_INVALID = 0,
    EVFS_RETURN_VALUE,

    EVFS_INODE_FIELD_BEGIN,

    EVFS_INODE_MTIME_TV_SEC = EVFS_INODE_FIELD_BEGIN,
    EVFS_INODE_MTIME_TV_USEC,

    EVFS_INODE_FIELD_END,

    // TODO: add fields of other objects
};

// TODO: some of these flags should be "hidden"
enum evfs_flag {
    EVFS_ANY = 1,
    EVFS_ALL = 2,
    EVFS_NOT = 3,
    EVFS_EXACT = 4,
    EVFS_FORCED = 5,
    
    EVFS_FREE_SPACE = 6,
    EVFS_USED_SPACE = 7,
};


static inline int
rmap_to_metadata(struct evfs_metadata * md, const struct evfs_rmap * rm, int i)
{
    if (md == NULL || rm == NULL)
        return -EINVAL;
    
    if (i < 0 || i >= rm->count)
        return -EINVAL;
        
    // TODO: the actual copy
}

#endif // UAPI_EVFS_H_

