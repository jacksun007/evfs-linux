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
typedef unsigned short u16;
typedef unsigned char u8;
typedef long long i64;
typedef int i32;

// (jsun):
// use the name evfs_t for non-atomic usage
// use the name struct evfs_atomic for atomic usage

struct evfs_atomic;
typedef struct evfs_atomic evfs_t;

struct evfs_extent {
    u64 addr;   // block address
    u64 len;    // number of blocks
};

// generic iterator type (opaque to user)
struct evfs_iter_s;
typedef struct evfs_iter_s evfs_iter_t;

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

// basic open/close for evfs device
evfs_t * evfs_open(const char * dev);
void evfs_close(evfs_t * evfs);

// iterators
evfs_iter_t * inode_iter(evfs_t * evfs, int flags);
evfs_iter_t * extent_iter(evfs_t * evfs, int flags);    // free space
u64 inode_next(evfs_iter_t * it);
void iter_end(evfs_iter_t * it);

// extent
int extent_alloc(evfs_t * evfs, u64 pa, u64 len, int flags);
int extent_free(evfs_t * evfs, u64 pa);
int extent_write(evfs_t * evfs, u64 pa, u64 off, char * buf, u64 len);

// super block
int super_info(evfs_t * evfs, struct evfs_super_block * sb);

// inode
int inode_info(evfs_t * evfs, struct evfs_inode * inode);
int inode_update(evfs_t * evfs, struct evfs_inode * inode);
int inode_map(evfs_t * evfs, u64 ino_nr, struct evfs_imap * imap);
int inode_read(evfs_t * evfs, u64 ino_nr, u64 off, char * buf, u64 len);

// inode mapping
struct evfs_imap * imap_new(evfs_t * evfs);
struct evfs_imap * imap_info(evfs_t * evfs, u64 ino_nr);
int imap_append(struct evfs_imap * imap, u64 la, u64 pa, u64 len);
void imap_free(evfs_t * evfs, struct evfs_imap * imap, int nofree);

// atomic interface
struct evfs_atomic * atomic_begin(evfs_t * evfs);
int atomic_const_equal(struct evfs_atomic * aa, int id, int field, u64 rhs);
int atomic_execute(struct evfs_atomic * aa);
void atomic_end(struct evfs_atomic * aa);


#endif

