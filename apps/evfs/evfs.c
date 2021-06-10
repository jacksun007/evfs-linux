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
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include "evfs.h"
 
evfs_t * evfs_open(const char * dev)
{
    evfs_t * evfs = malloc(sizeof(evfs_t));
    
    if (!evfs) 
        return NULL;
        
    evfs->fd = open(dev, O_RDONLY);
	if (evfs->fd < 0) {
		perror("open device");
		free(evfs);
		evfs = NULL;
	}
	
    return evfs;
}

void evfs_close(evfs_t * evfs)
{
    if (evfs != NULL) {
        close(evfs->fd);
        free(evfs);
    }
}

evfs_iter_t * inode_iter(evfs_t * evfs, int flags)
{
    (void)evfs;
    (void)flags;
    return NULL;
}

evfs_iter_t * extent_iter(evfs_t * evfs, int flags)
{
    (void)evfs;
    (void)flags;
    return NULL;
}

u64 inode_next(evfs_iter_t * it)
{
    (void)it;
    return 0;
}

void iter_end(evfs_iter_t * it)
{
    (void)it;
}

int super_info(evfs_t * evfs, struct evfs_super_block * sb)
{
    (void)evfs;
    (void)sb;
    return -EINVAL;
}

int extent_alloc(evfs_t * evfs, u64 pa, u64 len, int flags)
{
    (void)evfs;
    (void)pa;
    (void)len;
    (void)flags;
    return -EINVAL;
}

int extent_free(evfs_t * evfs, u64 pa)
{
    (void)evfs;
    (void)pa;
    return -EINVAL;
}

int extent_write(evfs_t * evfs, u64 pa, u64 off, char * buf, u64 len)
{
    (void)evfs;
    (void)pa;
    (void)off;
    (void)buf;
    (void)len;
    return -EINVAL;
}

int inode_info(evfs_t * evfs, struct evfs_inode * inode)
{
    (void)evfs;
    (void)inode;
    return -EINVAL;
}

int inode_read(evfs_t * evfs, u64 ino_nr, u64 off, char * buf, u64 len)
{
    (void)evfs;
    (void)ino_nr;
    (void)off;
    (void)buf;
    (void)len;
    return -EINVAL;
}

int inode_map(evfs_t * evfs, u64 ino_nr, struct evfs_imap * imap)
{
    (void)evfs;
    (void)ino_nr;
    (void)imap;
    return -EINVAL;
}

struct evfs_imap * imap_new(evfs_t * evfs)
{
    const int default_capacity = 64;
    struct evfs_imap * imap = malloc(sizeof(struct evfs_imap) + 
                              sizeof(struct evfs_imentry)*default_capacity);
    
    if (imap != NULL) {
        imap->count = 0;                          
        imap->capacity = default_capacity;
    }
    
    (void)evfs;
    return imap;
}

void imap_free(evfs_t * evfs, struct evfs_imap * imap, int nofree)
{
    unsigned i;
    
    if (imap != NULL && !nofree) {
        // free all physical extents
        for (i = 0; i < imap->count; i++) {
            if (imap->entry[i].phy_addr > 0) {
                extent_free(evfs, imap->entry[i].phy_addr);
            }   
        }

        free(imap);
    }
}

struct evfs_imap * imap_info(evfs_t * evfs, u64 ino_nr)
{
    struct evfs_imap * imap = imap_new(evfs);
    
    // TODO: implement when we need it
    
    (void)ino_nr;
    return imap;
}

int imap_append(struct evfs_imap * imap, u64 la, u64 pa, u64 len)
{
    if (!imap) {
        return -EINVAL;
    }
    
    // here, we want to check that the new entry cannot overlap or
    // come positionally before the last entry
    if (imap->count > 0) {
        struct evfs_imentry * last = &imap->entry[imap->count-1];
        if (la < last->log_addr + last->len) {
            return -EINVAL;
        }
    }
    
    // grow the imap structure if it is full
    assert(imap->count <= imap->capacity);
    if (imap->count >= imap->capacity) {
        const unsigned f = 2;
        imap = realloc(imap, sizeof(struct evfs_imap) + 
                             sizeof(struct evfs_imentry)*imap->capacity*f);
        if (imap == NULL)
            return -ENOMEM;
        
        imap->capacity *= f;
    }
    
    imap->entry[imap->count].inlined = 0;
    imap->entry[imap->count].log_addr = la;
    imap->entry[imap->count].phy_addr = pa;
    imap->entry[imap->count].len = len;
    imap->count += 1;
    
    return 0;
}

// TODO (jsun): finalize
// remove an entry at log_addr
// the shift flag determines whether to shift all subsequent entries forward
// by default will create a hole
int imap_remove(struct evfs_imap * imap, u64 log_addr, int shift)
{
    (void)imap;
    (void)log_addr;
    (void)shift;
    return -EINVAL;
}





