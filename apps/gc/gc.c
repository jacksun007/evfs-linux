/*
 * gc.c
 *
 * Generic garbage collection tool built using Evfs API
 *
 */

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <evfs.h>

#define eprintf(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

static int 
usage(char * prog)
{
    eprintf("usage: %s DEV [NUM]\n", prog);
    eprintf("  DEV: device of the file system.\n");
    return 1;
}

// 1 on yes, 0 on no
static int
should_collect(evfs_t * evfs, const struct evfs_extent_group * group)
{
    evfs_iter_t * it;
    int num_holes;

    // entire extent group is empty
    if (group->block_count == group->len)
        return 0;
        
    it = extent_iter(evfs, group->addr, group->len, EVFS_FREE_SPACE);
    num_holes = iter_count(it);
    
    if (num_holes < 0) {
        eprintf("error during iter_count of extents\n");
        return 0;
    }
    
    // collect if there are 2 or more holes
    return (num_holes > 1) : 1 : 0;
}

static int
relocate_data(evfs_t * evfs, const struct evfs_extent_group * group,
                             struct evfs_rmap * rmap)
{
    struct evfs_inode inode;
    struct evfs_rmentry * entry;
    int ret;
    u64 paddr;
    
    // TODO: currently do support extent mapped to multiple inodes
    if (rmap->count != 1)
        return -ENOSYS;
    
    entry = &rmap->entry[0];
    
    inode.ino_nr = ino_nr;
    if ((ret = inode_info(evfs, &inode)) < 0)
        break;
    
    paddr = extent_alloc(evfs, 0, rmap->len, EVFS_NOT, group);
    if (!paddr)
        return -ENOSPC;
        
    ret = extent_copy(evfs, paddr, rmap->phy_addr, rmap->len);
    if (ret < 0)
        break;
    
    ret = atomic_inode_map(evfs, entry->ino_nr, imap, &inode.mtime);
    if (ret > 0)
        break;

    return ret;
}

static int
relocate_metadata(evfs_t * evfs, const struct evfs_extent_group * group,
                                 struct evfs_rmap * rmap)
{
    struct evfs_inode inode;
    struct evfs_rmentry * entry;
    int ret;
    u64 paddr;
    
    // TODO: this should always be true?
    assert(rmap->count == 1);   
    entry = &rmap->entry[0];
    
    paddr = extent_alloc(evfs, 0, rmap->len, EVFS_NOT | EVFS_METADATA, group);
    if (!paddr)
        return -ENOSPC;
        
    ret = metadata_move(evfs, paddr, rmap, rmap->len);
    if (ret < 0)
        break;

    return ret;
}


static int
relocate_extent(evfs_t * evfs, const struct evfs_extent_group * group,
                               const struct evfs_extent * extent)
{
    struct evfs_rmap * rmap;
    u64 block_nr, end = extent->addr + extent->len;
    int ret;

    while (block_nr < end) {
        ret = reverse_map(evfs, block_nr, &rmap);
        if (!rmap)
            break;
        
        // data mapping
        if (rmap->type == 0) {
            ret = relocate_data(evfs, group, rmap);
            
            // if inode changed, redo
            if (ret > 0)
                continue;
        }
        // metadata mapping 
        else {
            ret = relocate_metadata(evfs, group, rmap);
        }
        
        // move onto next extent/block
        block_nr += rmap->len;      
        rmap_free(rmap);
        if (ret < 0)
            break;   
    }

    return ret;
}

static int
garbage_collect(evfs_t * evfs, const struct evfs_extent_group * group)
{
    evfs_iter_t * it;
    const struct evfs_extent * extent;
    int ret = 0;

    if (!should_collect(evfs, group))
        return 1;

    it = extent_iter(evfs, group->addr, group->len, EVFS_USED_SPACE);
    while ((extent = extent_next(it)) != NULL) {
        ret = relocate_extent(evfs, group, extent);
        if (ret < 0)
            break;
    }

    iter_end(it);
    return ret;
}

static int 
garbage_collect_all(evfs_t * evfs)
{
    evfs_iter_t * it = extent_group_iter(evfs, 0);
    const struct evfs_extent_group * group;
    int ret = 0, cnt = 0;
    
    while ((group = extent_group_next(it)) != NULL) {
        ret = garbage_collect(evfs, group);
        if (ret < 0)
            break;
        if (ret == 0)
            cnt++;
    }
    
    printf("%d extent group(s) have been garbage collected.\n", cnt);
    iter_end(it);
    return ret;
}

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    struct evfs_super_block sb;
    int ret;

    if (argc > 2) {
        goto error;
    }

    if (argc > 1) {
        evfs = evfs_open(argv[1]);
    }
    
    if (evfs == NULL) {
        goto error;
    }
    
    if ((ret = super_info(evfs, &sb)) < 0) {
        fprintf(stderr, "Error: could not retrieve super block info.\n");
        goto done;
    }

    ret = garbage_collect_all(evfs, &sb);
    
done:    
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

