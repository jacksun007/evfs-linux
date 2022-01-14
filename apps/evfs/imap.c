/*
 * imap.c
 *
 * Implements imap related functionalities, including imap_info
 *
 */

#include <linux/fiemap.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stropts.h>
#include "evfslib.h"

static struct evfs_imap * imap_alloc(evfs_t * evfs, int capacity)
{
    struct evfs_imap * imap = malloc(sizeof(struct evfs_imap) + 
                              sizeof(struct evfs_imentry)*capacity);
    
    if (imap != NULL) {
        imap->handle = evfs;
        imap->count = 0;                          
        imap->capacity = capacity;
    }
    
    return imap;
}

struct evfs_imap * imap_new(evfs_t * evfs)
{
    const int default_capacity = 64;
    return imap_alloc(evfs, default_capacity);
}

void imap_free(struct evfs_imap * imap)
{
    unsigned i;
    
    if (imap != NULL) {
        for (i = 0; i < imap->count; i++) {
            struct evfs_imentry * entry = &imap->entry[i];
            
            // TODO: free all unassigned physical extents
            if (!entry->assigned) {
                evfs_t * evfs = (evfs_t *)imap->handle;
                extent_free(evfs, entry->phy_addr, entry->len, 0);
            } 
        }
        
        free(imap);
    } 
}

static 
void
fiemap_to_imentry(const struct evfs_super_block * sb,
                  const struct fiemap_extent * extent, 
                  struct evfs_imentry * entry, unsigned i)
{
    entry->index = i;
    entry->inlined = (extent->fe_flags & FIEMAP_EXTENT_NOT_ALIGNED) ? 1 : 0;
    entry->assigned = 1;

    if (!entry->inlined) {
        // just a sanity check to make sure all are block-aligned
        assert(extent->fe_logical % sb->block_size == 0);
        assert(extent->fe_physical % sb->block_size == 0);
        assert(extent->fe_length % sb->block_size == 0);
        
        entry->log_addr = extent->fe_logical / sb->block_size;
        entry->phy_addr = extent->fe_physical / sb->block_size;
        entry->len = extent->fe_length / sb->block_size;
    }
    else {
        /* TODO: this seems very ugly */
        entry->log_addr = extent->fe_logical;
        entry->phy_addr = extent->fe_physical;
        entry->len = extent->fe_length;
    }
}

#define NUM_RETRIES 10

// TODO: allow for ranges
struct evfs_imap * imap_info(evfs_t * evfs, u64 ino_nr)
{
    struct evfs_super_block sb;
    struct evfs_imap * ret = NULL;
    struct fiemap * fiemap;
    struct evfs_imap_param param = { .ino_nr = ino_nr };
    unsigned i, r;  // number of retries
    
    if (super_info(evfs, &sb) < 0)
        return 0; 
    
    fiemap = malloc(sizeof(struct fiemap));
    if (fiemap == NULL)
        return NULL;

    fiemap->fm_start = 0;
    fiemap->fm_length = ~0;         // basically end-of-file
    fiemap->fm_flags = 0;
    fiemap->fm_extent_count = 0;
    fiemap->fm_mapped_extents = 0;
    param.fiemap = fiemap;
    
    for (r = 0; r < NUM_RETRIES; r++) 
    {
        unsigned size;
    
        // f2fs currently does not support fiemap on directory inodes
        if (ioctl(evfs->fd, FS_IOC_IMAP_INFO, &param) < 0)
		    goto fail;

	    if (fiemap->fm_extent_count >= fiemap->fm_mapped_extents) {
	        ret = imap_alloc(evfs, fiemap->fm_mapped_extents);
	        
	        for (i = 0; i < fiemap->fm_mapped_extents; i++) {
	            struct fiemap_extent * extent = &fiemap->fm_extents[i];
	            struct evfs_imentry * entry = &ret->entry[i];
                fiemap_to_imentry(&sb, extent, entry, i);
	        }
	        
	        ret->count = fiemap->fm_mapped_extents;
            break;
	    }
	    
	    size = fiemap->fm_mapped_extents;
	    fiemap = realloc(fiemap, sizeof(struct fiemap) + 
	                     sizeof(struct fiemap_extent) * size);
	    param.fiemap = fiemap;
            
	    if (fiemap == NULL)
            return NULL;

        fiemap->fm_extent_count = size;
	}
    
fail:
    free(fiemap);
    return ret;
}

long imap_append(struct evfs_imap ** imptr, u64 la, u64 pa, u64 len)
{
    struct evfs_imap * imap;
    
    if (!imptr) {
        return -EINVAL;
    }
    
    imap = *imptr;
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
        *imptr = imap;
    }
    
    assert(imap->count <= imap->capacity);
    imap->entry[imap->count].inlined = 0;
    imap->entry[imap->count].assigned = 0;
    imap->entry[imap->count].index = imap->count;
    imap->entry[imap->count].log_addr = la;
    imap->entry[imap->count].phy_addr = pa;
    imap->entry[imap->count].len = len;
    imap->count += 1;

    return 0;
}

void imap_print(const struct evfs_imap * imap)
{
    unsigned i;
    for (i = 0; i < imap->count; i++) {
        const struct evfs_imentry * e = &imap->entry[i];
        printf("%u: la = %lu, pa = %lu, len = %lu. %s %s\n",
            e->index, e->log_addr, e->phy_addr, e->len,
            e->inlined ? "inlined" : "", e->assigned ? "assigned" : "");
    }       
}

// TODO (jsun): finalize
// remove an entry at log_addr
// the shift flag determines whether to shift all subsequent entries forward
// by default will create a hole
long imap_remove(struct evfs_imap * imap, u64 log_addr, int shift)
{
    (void)imap;
    (void)log_addr;
    (void)shift;
    return -EINVAL;
}

