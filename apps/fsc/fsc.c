/*
 * fsc.c
 *
 * Generic freespace consolidation built using Evfs API (forward pointers only)
 *
 */

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <evfs.h>
#include "set.h"

static int 
atomic_inode_map(evfs_t * evfs, long ino_nr, struct evfs_imap * imap,
                 struct evfs_timeval * mtime);

#define eprintf(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

static int 
usage(char * prog)
{
    eprintf("usage: %s DEV [NUM]\n", prog);
    eprintf("  DEV: device of the file system.\n");
    eprintf("  NUM: inode number.\n");
    return 1;
}

// type 0 -> data, any length
// type 1 -> metadata, full length
static int
galloc(evfs_t * evfs, struct set * blkgrp, int * lptr, int type)
{
    int i, c = set_count(blkgrp);
    int len = *lptr;
    int ret;
    
    for (i = 0; i < c; i++) {
        struct evfs_group group;
        struct evfs_extent_attr attr;
        
        group.group_nr = set_item(blkgrp, i);
        if ((ret = group_info(evfs, &group)) < 0) {
            return ret;
        }
        
        attr.flags = (type == 0) ? EVFS_ANY : EVFS_ALL;
        attr.metadata = type;
        attr.type = 0;
        attr.range = *group_to_extent(&group);
        
        // TODO: should pass &len (by pointer)
        if ((ret = extent_alloc(evfs, 0, len, &attr)) > 0) {
            *lptr = len;
            return ret;
        }
        else if (ret < 0) {
            return ret;
        }
    }
    
    return -ENOSPC;
}

// return 0 on success
// return 1 to indicate retry
// return negative value on error
static int
consolidate(evfs_t * evfs, const struct evfs_super_block * sb, 
            unsigned long ino_nr)
{
    struct evfs_imap * imap = imap_info(evfs, ino_nr);
    evfs_iter_t * it;
    struct evfs_inode inode;
    struct evfs_metadata * mdp, md;
    struct set * blkgrp;
    int ret = 0;
    unsigned i;
    double ratio;
    
    if (!imap) {
        eprintf("warning: imap_info failed on inode %lu\n", ino_nr);
        return -ENOMEM;
    }
    
    blkgrp = set_new();
    if (!blkgrp) {
        imap_free(imap);
        return -ENOMEM;
    }
    
    // calculate how many block groups this file belongs to
    for (i = 0; i < imap->count; i++) {
        struct evfs_imentry * e = &imap->entry[i];
        struct evfs_block_info binfo;
        
        // do not consolidate any file with inlined data
        if (e->inlined) {
            ret = 0;
            goto done;
        }
        
        if ((ret = block_info(evfs, e->phy_addr, &binfo)) < 0) {
            goto done;
        }
        
        if ((ret = set_add(blkgrp, binfo.group_nr)) < 0) {
            goto done;
        }
    }
    
    inode.ino_nr = ino_nr;
    if ((ret = inode_info(evfs, &inode)) < 0) {
        goto done;
    }
    
    // if ratio is less than the number of block groups, 
    // we don't need to consolidate
    ratio = (double)inode.bytesize / sb->max_extent_size;
    if (ratio <= (double)set_count(blkgrp)) {
        ret = 0;
        goto done;
    }

    // at this point, we are committed to performing consolidation
    for (i = 0; i < imap->count; i++) {
        struct evfs_imentry * e = &imap->entry[i];      
        int pa = e->phy_addr;
        int la = e->log_addr;
        int remain = e->len;
        
        while (remain > 0) {
            struct evfs_imap * nmap = imap_new(evfs);
            int len = remain;
            int ex = galloc(evfs, blkgrp, &len, 0);
            
            if ((ret = extent_copy(evfs, ex, pa, len)) < 0) {
                goto done;
            }
            
            ret = imap_append(nmap, la, ex, len);
            if (ret < 0) {
                imap_free(nmap);
                goto done;
            }   
            
            pa += len;
            la += len;
            remain -= len;
            
            ret = atomic_inode_map(evfs, ino_nr, nmap, &inode.mtime);
            imap_free(nmap);
            
            if (ret > 0) {
                ret = 1;
                goto done;
            }
            else if (ret < 0) {
                goto done;
            }
        }
    }

    it = metadata_iter(evfs, ino_nr);
    while ((mdp = metadata_next(it)) != NULL) {
        int len = mdp->len;
        int pa = galloc(evfs, blkgrp, &len, 1);
        md = *mdp;
        if ((ret = metadata_move(evfs, pa, &md)) < 0) {
            goto done;
        }
    }
    
    ret = 0;
done:
    set_free(blkgrp);
    imap_free(imap);
    return ret;
}

static int 
consolidate_all(evfs_t * evfs, const struct evfs_super_block * sb)
{
    evfs_iter_t * it = inode_iter(evfs, 0);
    int ret = 0, cnt = 0;
    unsigned long ino_nr;
    
    while ((ino_nr = inode_next(it)) > 0) {
        // retry consolidation until either success or error
        while ((ret = consolidate(evfs, sb, ino_nr)) == 1);
        if (ret < 0)
            break;
        if (ret == 0)
            cnt++;
    }
    
    printf("%d inode(s) were processed for freespace consolidation.\n", cnt);
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

    ret = consolidate_all(evfs, &sb);
    
done:    
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

static int 
atomic_inode_map(evfs_t * evfs, long ino_nr, struct evfs_imap * imap,
                 struct evfs_timeval * mtime)
{
    int ret, id;
    struct evfs_atomic * aa = atomic_begin(evfs);
    struct evfs_inode inode; 
    
    inode.ino_nr = ino_nr;
    id = inode_info(aa, &inode);
    if (id < 0) {
        goto fail;
    }
    
    ret = atomic_const_equal(aa, id, EVFS_INODE_MTIME_TV_SEC, mtime->tv_sec);
    if (ret < 0) {
        goto fail;
    }
    
    ret = atomic_const_equal(aa, id, EVFS_INODE_MTIME_TV_USEC, mtime->tv_usec);
    if (ret < 0) {
        goto fail;
    }
    
    ret = inode_map(aa, ino_nr, imap);
    if (ret < 0) {
        goto fail;
    }
    
    ret = atomic_execute(aa);
fail:
    atomic_end(aa);
    return ret;
}


