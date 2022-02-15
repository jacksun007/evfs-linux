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

// userspace imap object
struct evfs_imap;

// basic open/close for evfs device
evfs_t * evfs_open(const char * dev);
void evfs_close(evfs_t * evfs);

// iterators
evfs_iter_t * inode_iter(evfs_t * evfs, int flags);
evfs_iter_t * block_iter(evfs_t * evfs, int flags);     // used space
evfs_iter_t * extent_iter(evfs_t * evfs, int flags);    // free space
evfs_iter_t * group_iter(evfs_t * evfs, int flags);     // block group
evfs_iter_t * metadata_iter(evfs_t * evfs, u64 ino_nr);

u64 inode_next(evfs_iter_t * it);
u64 block_next(evfs_iter_t * it);
struct evfs_extent extent_next(evfs_iter_t * it);
struct evfs_group * group_next(evfs_iter_t * it);
struct evfs_metadata metadata_next(evfs_iter_t * it);

// all iterators can be ended using this
void iter_end(evfs_iter_t * it);

// consume iterator and return count
long iter_count(evfs_iter_t * it);

// Extent
// Note: You may only free/write to extents that you allocated yourself,
//       and it hasn't been mapped to another inode.
long extent_alloc(evfs_t * evfs, u64 pa, u64 len, struct evfs_extent_attr * at);
long extent_active(evfs_t * evfs, u64 pa, u64 len, int flags);
long extent_free(evfs_t * evfs, u64 pa, u64 len, int flags);
long extent_write(evfs_t * evfs, u64 pa, u64 off, const char * buf, u64 len);
long extent_read(evfs_t * evfs, u64 pa, u64 off, char * buf, u64 len);
long extent_copy(evfs_t * evfs, u64 dst, u64 src, u64 len);

long block_info(evfs_t * evfs, u64 pa, struct evfs_block_info * bi);
long group_info(evfs_t * evfs, struct evfs_group * group);

// allows overwriting ANY part of the device
long extent_write_unsafe(evfs_t * evfs, u64 pa, u64 off, const char * buf, u64 len);

// super block
long super_info(evfs_t * evfs, struct evfs_super_block * sb);

// inode
long inode_info(evfs_t * evfs, struct evfs_inode * inode);
long inode_update(evfs_t * evfs, struct evfs_inode * inode);
long inode_map(evfs_t * evfs, u64 ino_nr, struct evfs_imap * imap);
long inode_read(evfs_t * evfs, u64 ino_nr, u64 off, char * buf, u64 len);
long inode_write(evfs_t * evfs, u64 ino_nr, u64 off, char * buf, u64 len);

long extent_write(evfs_t * evfs, u64 pa, u64 off, const char * buf, u64 len);

// inode mapping
struct evfs_imap * imap_new(evfs_t * evfs);
struct evfs_imap * imap_info(evfs_t * evfs, u64 ino_nr);
long imap_append(struct evfs_imap ** imptr, u64 la, u64 pa, u64 len);
void imap_free(struct evfs_imap * imap);
void imap_print(const struct evfs_imap * imap);

// reverse mapping
long reverse_map(evfs_t * evfs, u64 pa, struct evfs_rmap ** rmptr);
void rmap_free(struct evfs_rmap * rmap);
long metadata_move(evfs_t * evfs, u64 pa, struct evfs_metadata * md);

// atomic interface
struct evfs_atomic * atomic_begin(evfs_t * evfs);
long atomic_const_equal(struct evfs_atomic * aa, int id, int field, u64 rhs);
long atomic_execute(struct evfs_atomic * aa);
void atomic_end(struct evfs_atomic * aa);
long atomic_result(struct evfs_atomic * aa, int id);

// debug functions
void debug_my_extents(evfs_t * evfs);

#endif // EVFS_H_

