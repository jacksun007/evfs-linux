/*
 * evfs.c
 *
 * Just a bunch of scaffolds right now. Need to merge with refactored
 * Evfs kernel API
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stropts.h>
#include "evfslib.h"


evfs_t * evfs_open(const char * dev)
{
    evfs_t * evfs = malloc(sizeof(evfs_t));
    long ret;

    if (!evfs)
        return NULL;

    evfs->atomic = 0;
    evfs->magic = 0;   /* no magic, not atomic! */

    evfs->fd = open(dev, O_RDONLY);
	if (evfs->fd < 0) {
		perror("open device");
		goto fail;
	}

	ret = ioctl(evfs->fd, FS_IOC_EVFS_OPEN, 0);
	if (ret < 0) {
	    fprintf(stderr, "error: cannot open '%s' as evfs device. %s\n",
	            dev, strerror(-ret));
	    goto fail;
	}

    return evfs;
fail:
    free(evfs);
    return NULL;
}

void evfs_close(evfs_t * evfs)
{
    if (evfs != NULL) {
        close(evfs->fd);
        free(evfs);
    }
}

/*
 * TODO (kyokeun): Need to respect user-provided flag
 */
evfs_iter_t * inode_iter(evfs_t * evfs, int flags)
{
    evfs_iter_t *iter = malloc(sizeof(evfs_iter_t));
    iter->evfs = evfs;
    iter->type = EVFS_TYPE_INODE;
    iter->flags = flags;
    iter->count = 0;
    iter->next_req = 0;

    if (ioctl(evfs->fd, FS_IOC_INODE_ITERATE, &iter->op) < 0) {
        free(iter);
        return NULL;
    }

    return iter;
}

evfs_iter_t * metadata_iter(evfs_t * evfs, u64 ino_nr)
{
    evfs_iter_t *iter = malloc(sizeof(evfs_iter_t));
    iter->evfs = evfs;
    iter->type = EVFS_TYPE_METADATA;
    iter->flags = ino_nr;
    iter->count = 0;
    iter->next_req = 0;

    if (ioctl(evfs->fd, FS_IOC_METADATA_ITERATE, &iter->op) < 0) {
        free(iter);
        return NULL;
    }

    return iter;
}

evfs_iter_t * extent_iter(evfs_t * evfs, int flags)
{
    evfs_iter_t *iter = malloc(sizeof(evfs_iter_t));
    iter->evfs = evfs;
    iter->type = EVFS_TYPE_EXTENT;
    iter->flags = flags;
    iter->count = 0;
    iter->next_req = 0;

    if (ioctl(evfs->fd, FS_IOC_EXTENT_ITERATE, &iter->op) < 0) {
        free(iter);
        return NULL;
    }

    return iter;
}

u64 inode_next(evfs_iter_t * it)
{
    u64 *param;
    int ret = 0;

    if (it->type != EVFS_TYPE_INODE)
        return -1;

    /* If we have iterated through everything in the buffer, get more */
    if (it->op.count <= it->count) {
        it->op.start_from = it->next_req;
	ret = ioctl(it->evfs->fd, FS_IOC_INODE_ITERATE, &it->op);
        if (ret <= 0)
            return 0;
        it->count = 0;
    }

    param = ((u64 *) it->op.buffer) + it->count;
    ++it->count;
    it->next_req = *param + 1;

    return *param;
}

struct evfs_metadata metadata_next(evfs_iter_t * it)
{
    struct evfs_metadata *param, ret = { 0 };

    if (it->type != EVFS_TYPE_METADATA)
        return ret;

    if (it->op.count <= it->count) {
        it->op.start_from = it->next_req;
        if (ioctl(it->evfs->fd, FS_IOC_METADATA_ITERATE, &it->op) <= 0)
            return ret;
        it->count = 0;
    }

    param = ((struct evfs_metadata *) it->op.buffer + it->count);
    ++it->count;
    it->next_req = param->region_start + param->len;
    ret = *param;

    return ret;
}

struct evfs_extent extent_next(evfs_iter_t * it)
{
    struct evfs_extent *param, ret = { 0 };
    if (it->type != EVFS_TYPE_EXTENT)
        return ret;

    if (it->op.count <= it->count) {
        it->op.start_from = it->next_req;
        if (ioctl(it->evfs->fd, FS_IOC_EXTENT_ITERATE, &it->op) <= 0)
            return ret;
        it->count = 0;
    }

    param = ((struct evfs_extent *) it->op.buffer) + it->count;
    ++it->count;
    it->next_req = param->addr + param->len;
    ret = *param;

    return ret;
}

void iter_end(evfs_iter_t * it)
{
    free(it);
}

long super_info(evfs_t * evfs, struct evfs_super_block * sb)
{
    return _evfs_operation(evfs, EVFS_SUPER_INFO, sb,
                           sizeof(struct evfs_super_block));
}

static
long
extent_operation(evfs_t * evfs, int type, u64 pa, u64 len, int flags)
{
    struct evfs_extent_op ext_op;

    ext_op.extent.addr = pa;
    ext_op.extent.len = len;
    ext_op.flags = flags;

    return evfs_operation(evfs, type, ext_op);
}

long extent_alloc(evfs_t * evfs, u64 pa, u64 len, struct evfs_extent_attr * at)
{
    struct evfs_extent_alloc_op op;

    op.extent.addr = pa;
    op.extent.len = len;
    op.attr = at;

    return evfs_operation(evfs, EVFS_EXTENT_ALLOC, op);
}

long extent_active(evfs_t * evfs, u64 pa, u64 len, int flags)
{
    return extent_operation(evfs, EVFS_EXTENT_ACTIVE, pa, len, flags);
}

long extent_free(evfs_t * evfs, u64 pa, u64 len, int flags)
{
    return extent_operation(evfs, EVFS_EXTENT_FREE, pa, len, flags);
}

static inline void
set_extent_op(struct evfs_ext_rw_op * args,
              u64 pa, u64 off, const char * buf, u64 len, u64 flags)
{
    args->addr = pa;
    args->offset = off;
    args->wdata = buf;
    args->len = len;
    args->flags = flags;
}

long extent_write(evfs_t * evfs, u64 pa, u64 off, const char * buf, u64 len)
{
    struct evfs_ext_rw_op args;
    set_extent_op(&args, pa, off, buf, len, 0);
    return evfs_operation(evfs, EVFS_EXTENT_WRITE, args);
}

long
extent_write_unsafe(evfs_t * evfs, u64 pa, u64 off, const char * buf, u64 len)
{
    struct evfs_ext_rw_op args;
    set_extent_op(&args, pa, off, buf, len, EVFS_FORCED);
    return evfs_operation(evfs, EVFS_EXTENT_WRITE, args);
}

long extent_read(evfs_t * evfs, u64 pa, u64 off, char * buf, u64 len)
{
    struct evfs_ext_rw_op args;

    args.addr = pa;
    args.offset = off;
    args.rdata = buf;
    args.len = len;
    return evfs_operation(evfs, EVFS_EXTENT_READ, args);
}

#define inode_operation(evfs, opcode, inode) \
    _evfs_operation((evfs), (opcode), (inode), sizeof(struct evfs_inode))

long inode_info(evfs_t * evfs, struct evfs_inode * inode)
{
    return inode_operation(evfs, EVFS_INODE_INFO, inode);
}

long inode_update(evfs_t * evfs, struct evfs_inode * inode)
{
    return inode_operation(evfs, EVFS_INODE_UPDATE, inode);
}

long inode_read(evfs_t * evfs, u64 ino_nr, u64 off, char * buf, u64 len)
{
    struct evfs_inode_read_op op;
    op.data = buf;
    op.ino_nr = ino_nr;
    op.length = len;
    op.ofs = off;
    return evfs_operation(evfs, EVFS_INODE_READ, op);
}

long inode_write(evfs_t * evfs, u64 ino_nr, u64 off, char * buf, u64 len)
{
    struct evfs_inode_read_op op;
    op.data = buf;
    op.ino_nr = ino_nr;
    op.length = len;
    op.ofs = off;
    return evfs_operation(evfs, EVFS_INODE_WRITE, op);
}

long inode_map(evfs_t * evfs, u64 ino_nr, struct evfs_imap * imap)
{
    struct evfs_imap_op op;
    op.ino_nr = ino_nr;
    op.flags = 0;
    op.imap = imap;
    return evfs_operation(evfs, EVFS_INODE_MAP, op);
}

void debug_my_extents(evfs_t * evfs)
{
    ioctl(evfs->fd, FS_IOC_LIST_MY_EXTENTS, 0);
}

