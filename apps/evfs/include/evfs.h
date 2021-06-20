/*
 * evfs.h
 *
 * Userspace Evfs Library API
 *
 */ 

#ifndef EVFS_H_
#define EVFS_H_

typedef unsigned long u64;  // needed because userspace doesn't define this

#include "linux/evfs.h"

// (jsun):
// use the name evfs_t for non-atomic usage
// use the name struct evfs_atomic for atomic usage

struct evfs_atomic;
typedef struct evfs_atomic evfs_t;

// generic iterator type (opaque to user)
struct evfs_iter_s;
typedef struct evfs_iter_s evfs_iter_t;

// basic open/close for evfs device
evfs_t * evfs_open(const char * dev);
void evfs_close(evfs_t * evfs);

// iterators
evfs_iter_t * inode_iter(evfs_t * evfs, int flags);
evfs_iter_t * extent_iter(evfs_t * evfs, int flags);    // free space
u64 inode_next(evfs_iter_t * it);
struct evfs_extent extent_next(evfs_iter_t * it);
void iter_end(evfs_iter_t * it);

// Extent
// Note: You may only free/write to extents that you allocated yourself,
//       and it hasn't been mapped to another inode.
int extent_alloc(evfs_t * evfs, u64 pa, u64 len, int flags);
int extent_active(evfs_t * evfs, u64 pa, u64 len, int flags);
int extent_free(evfs_t * evfs, u64 pa, u64 len, int flags);
int extent_write(evfs_t * evfs, u64 pa, u64 off, const char * buf, u64 len);
int extent_read(evfs_t * evfs, u64 pa, u64 off, char * buf, u64 len);

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
long atomic_result(struct evfs_atomic * aa, int id);

// debug functions
void debug_my_extents(evfs_t * evfs);

#endif // EVFS_H_

