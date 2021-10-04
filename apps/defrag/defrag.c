/*
 * defrag.c
 *
 * Generic defragmentation tool built using Evfs API
 *
 */

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <evfs.h>

// turn on for debugging
// #define ALWAYS_DEFRAG

#define VERBOSE

#define eprintf(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

// return value of defragment function
#define NOT_FRAGMENTED 1
#define INODE_BUSY 2
#define NOT_REGULAR 3

static int 
atomic_inode_map(evfs_t * evfs, long ino_nr, struct evfs_imap * imap,
                 struct evfs_timeval * mtime);

int usage(char * prog)
{
    eprintf("usage: %s DEV [NUM]\n", prog);
    eprintf("  DEV: device of the file system.\n");
    eprintf("  NUM: Inode number of file to defragment.\n");
    eprintf("       When not specified, defragment all files.\n");
    return 1;
}

// 1 for yes, 0 for no
int should_defragment(evfs_t * evfs, struct evfs_super_block * sb, unsigned long ino_nr)
{
    struct evfs_imap * imap = imap_info(evfs, ino_nr);
    int ret = 0;
    u64 end = 0;
    int num_out_of_order = 0;
    unsigned i;
    
    if (!imap) {
        eprintf("warning: imap_info failed on inode %lu\n", ino_nr);
        return 0;
    } 
    
    // current algorithm just checks if there are any extents that are not
    // in monotonically increasing order
    for (i = 0; i < imap->count; i++) {
        struct evfs_imentry * e = &imap->entry[i];     
        
        // do not defrag any file with inlined data
        if (e->inlined) {
            ret = 0;
            goto done;
        }
        
        if (end > e->phy_addr)
            num_out_of_order++;
        
        end = e->phy_addr + e->len;
    }

#ifdef ALWAYS_DEFRAG
    ret = 1;
#else    
    if (num_out_of_order > 0)
        ret = 1;
#endif
    
    // TODO: may need to consult fields of super block in the future
    (void)sb;
done:
    imap_free(imap);
    return ret;
}

#define CEILING(x,y) (((x) + (y) - 1) / (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

int defragment(evfs_t * evfs, struct evfs_super_block * sb, unsigned long ino_nr)
{
    struct evfs_inode inode;
    char * data = NULL;
    int ret;
    u64 poff, loff = 0;
    u64 nr_blocks;
    struct evfs_imap * imap = NULL;
    u64 extent_size;
    u64 byte_size;

    inode.ino_nr = ino_nr;
    if ((ret = inode_info(evfs, &inode)) < 0)
        return ret;

    // TODO: need this for now because f2fs does not support directory fiemap
    if (!S_ISREG(inode.mode)) {
        return NOT_REGULAR;
    }

    if (!should_defragment(evfs, sb, ino_nr)) {
        return NOT_FRAGMENTED;
    }
          
#ifdef VERBOSE
    printf("Defragmenting inode %lu\n", ino_nr); 
#endif   
    
    // calculate how many blocks need to be allocated
    imap = imap_new(evfs);
    nr_blocks = CEILING(inode.bytesize, sb->block_size);       
    extent_size = MIN(nr_blocks, sb->max_extent_size);
    byte_size = extent_size * sb->block_size;   
    
    // set up buffer for copying data to new extents   
    data = malloc(byte_size);
    if (!data) {
        ret = -ENOMEM;
        goto done;
    }      
          
    // start allocating new contiguous extents. we have to deal with the
    // possibility that the file system has a maximum contiguous extent limit
    while (nr_blocks > 0) {
        poff = extent_alloc(evfs, 0, extent_size, 0 /* no hint */);
        if (!poff) {
            ret = -ENOSPC;
            goto done;
        }

        ret = imap_append(&imap, loff, poff, extent_size);
        if (ret < 0) {
            goto done;
        }   
           
        ret = inode_read(evfs, ino_nr, loff * sb->block_size, data, byte_size);
        if (ret < 0) {
            goto done;
        }
        
        ret = extent_write(evfs, poff, 0, data, byte_size);
        if (ret < 0) {
            goto done;     
        }
        
        // prepare for next iteration
        nr_blocks -= extent_size;
        loff += extent_size;
        extent_size = MIN(nr_blocks, sb->max_extent_size);
        byte_size = extent_size * sb->block_size;    
    }
    
    // atomic_execute returns positive value if atomic action is cancelled 
    // due to failed comparison
    ret = atomic_inode_map(evfs, ino_nr, imap, &inode.mtime);
    if (ret > 0)
        ret = INODE_BUSY;

done:    
    free(data);
    imap_free(imap);
    return ret;
}

int defragment_all(evfs_t * evfs, struct evfs_super_block * sb)
{
    evfs_iter_t * it = inode_iter(evfs, 0);
    int ret = 0, cnt = 0, nf = 0, ib = 0, total = 0;
    unsigned long ino_nr;
    
    while ((ino_nr = inode_next(it)) > 0) {
        ret = defragment(evfs, sb, ino_nr);
        total++;

        if (ret < 0) {
            eprintf("error while defragmenting inode %ld\n", ino_nr);
            break;
        }
        else if (ret == 0)
            cnt++;
        else if (ret == NOT_FRAGMENTED)
            nf++;
        else if (ret == INODE_BUSY)
            ib++;
        else if (ret == NOT_REGULAR)
            total--;
        else {
            eprintf("error: unknown return value from defragment()\n");
            ret = -EINVAL;
            break;
        }
    }
    
    iter_end(it);
    printf("%d inode(s) scanned. %d defragmented, %d not fragmented, %d busy\n", 
        total, cnt, nf, ib);
    
    return ret;
}

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    struct evfs_super_block sb;
    int ret;

    if (argc > 3) {
        goto error;
    }

    if (argc > 1) {
        evfs = evfs_open(argv[1]);
    }
    
    if (evfs == NULL) {
        goto error;
    }
    
    if ((ret = super_info(evfs, &sb)) < 0) {
        fprintf(stderr, "Error: could not retrieve super block info: %s\n",
		strerror(-ret));
        goto done;
    }

    if (argc == 2) {
        ret = defragment_all(evfs, &sb);
    }
    else if (argc == 3) {
        int ino_nr = atoi(argv[2]);
        if (ino_nr <= 0) {
            fprintf(stderr, "Invalid inode number: %s\n", argv[2]);
            goto error; 
        }
        else {
            ret = defragment(evfs, &sb, ino_nr);
        }
    }
    
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

#if 0
    (void)evfs;
    (void)ino_nr;
    (void)imap;
    (void)mtime;
    return 0;
#endif
}


