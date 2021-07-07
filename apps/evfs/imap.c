/*
 * imap.c
 *
 * Implements imap related functionalities, including imap_info
 *
 */

#include <linux/fiemap.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stropts.h>
#include "evfslib.h"

static struct evfs_imap * imap_alloc(int capacity)
{
    struct evfs_imap * imap = malloc(sizeof(struct evfs_imap) + 
                              sizeof(struct evfs_imentry)*capacity);
    
    if (imap != NULL) {
        imap->count = 0;                          
        imap->capacity = capacity;
    }
    
    return imap;
}

struct evfs_imap * imap_new(evfs_t * evfs)
{
    const int default_capacity = 64;
    (void)evfs;
    return imap_alloc(default_capacity);
}

void imap_free(evfs_t * evfs, struct evfs_imap * imap, int nofree)
{
    unsigned i;
    
    if (imap != NULL && !nofree) {
        // free all physical extents
        for (i = 0; i < imap->count; i++) {
            struct evfs_imentry * entry = &imap->entry[i];
            if (entry->phy_addr > 0) {
                extent_free(evfs, entry->phy_addr, entry->len, 0);
            }   
        }   
    }
    
    free(imap);
}

// TODO: allow for ranges
struct evfs_imap * imap_info(evfs_t * evfs, u64 ino_nr)
{
    struct fiemap probe = {
        .fm_start = 0,
        .fm_length = ~0,        // basically end-of-file
        .fm_flags = 0,
        .fm_extent_count = 0,
        .fm_mapped_extents = 0,
    }, * fiemap;
    struct evfs_imap_param param = { .ino_nr = ino_nr, .fiemap = &probe };
    
    if (ioctl(evfs->fd, FS_IOC_IMAP_INFO, &param) < 0) {
		fprintf(stderr, "fiemap ioctl() failed\n");
		return NULL;
	}
	
	printf("fm_mapped_extents = %u, fm_extent_count = %u\n", 
	    probe.fm_mapped_extents, probe.fm_extent_count);
	
	if (probe.fm_extent_count == 0) {
	    return imap_alloc(0);
	}
    
    (void)fiemap;
    return NULL;
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

