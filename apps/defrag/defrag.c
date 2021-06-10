/*
 * defrag.c
 *
 * Generic defragmentation tool built using Evfs API
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <evfs.h>

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV [NUM]\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  NUM: Inode number of file to defragment.\n");
    fprintf(stderr, "       When not specified, defragment all files.\n");
    return 1;
}

// 1 for yes, 0 for no
int should_defragment(evfs_t * evfs, struct evfs_super_block * sb, long ino_nr)
{
    // for now, always defragment
    (void)evfs;
    (void)sb;
    (void)ino_nr;
    return 1;
}

#define CEILING(x,y) (((x) + (y) - 1) / (y))

int defragment(evfs_t * evfs, struct evfs_super_block * sb, long ino_nr)
{
    struct evfs_inode inode;
    char * data = NULL;
    int ret;
    u64 poff, loff = 0;
    u64 nr_blocks;
    struct evfs_imap * imap = NULL;
    struct evfs_timeval mtime;  // to avoid race condition
    u64 extent_size;
    u64 byte_size;

    if (!should_defragment(evfs, sb, ino_nr)) {
        return 0;
    }
    
    inode.ino_nr = ino_nr;
    ret = inode_info(evfs, &inode);
    
    if (!ret) {
        return ret;
    }
          
    // calculate how many blocks need to be allocated
    imap = imap_new(evfs);
    nr_blocks = CEILING(inode.bytesize, sb->block_size);       
    extent_size = (nr_blocks > sb->max_extent_size) ? sb->max_extent_size : nr_blocks;
    byte_size = extent_size * sb->block_size;   
    
    // set up buffer for copying data to new extents   
    data = malloc(byte_size);
    if (!data) {
        ret = -ENOMEM;
        goto fail;
    }      
          
    // start allocating new contiguous extents. we have to deal with the
    // possibility that the file system has a maximum contiguous extent limit
    while (nr_blocks > 0) {
        poff = extent_alloc(evfs, 0, extent_size, 0 /* no hint */);
        if (!poff) {
	        return -ENOSPC;	// error
        }

        ret = imap_append(imap, loff, poff, extent_size);
        if (ret < 0) {
            goto fail;
        }   
           
        ret = inode_read(evfs, ino_nr, loff * sb->block_size, data, byte_size);
        if (ret < 0) {
            goto fail;
        }
        
        ret = extent_write(evfs, poff, 0, data, byte_size);
        if (ret < 0) {
            goto fail;            
        }
        
        // prepare for next iteration
        nr_blocks -= extent_size;
        loff += extent_size;
        extent_size = (nr_blocks > sb->max_extent_size) ? sb->max_extent_size : nr_blocks;
        byte_size = extent_size * sb->block_size; 
    }
    
    ret = inode_map(evfs, ino_nr, imap);
    if (ret < 0) {
        goto fail;
    }
    
    (void)mtime;
    free(data);
    imap_free(evfs, imap, 1 /* do not free extents on success */);
    return 0;
    
fail:
    free(data);
    imap_free(evfs, imap, 0 /* free all extents upon error */);
    return ret;
}

int defragment_all(evfs_t * evfs, struct evfs_super_block * sb)
{
    evfs_iter_t * it = inode_iter(evfs, 0);
    int ret = 0;
    long ino_nr;
    
    while ((ino_nr = inode_next(it)) > 0) {
        ret = defragment(evfs, sb, ino_nr);
        if (ret != 0) {
            break;
        }
    }
    
    iter_end(it);
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
        fprintf(stderr, "Error: could not retrieve super block info.\n");
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

