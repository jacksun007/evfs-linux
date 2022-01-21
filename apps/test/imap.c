/*
 * imap.c
 *
 * Tests extent_alloc, extent_write, and inode_imap
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <evfs.h>
#include "common.h"

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV NAME\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, " NAME: name for new file on evfs device.\n");
    return 1;
}

static
void 
print_imap_info(evfs_t * evfs, unsigned ino_nr)
{
    struct evfs_imap * imap = imap_info(evfs, ino_nr);
    print_imap(imap);
    imap_free(imap);   
}

static long 
atomic_inode_map(evfs_t * evfs, long ino_nr, struct evfs_imap * imap,
                 struct evfs_timeval * mtime);

#define NEW_MSG "hello world"

int main(int argc, char * argv[])
{
    int ret = 0;
    unsigned long pa;
    struct evfs_imap * imap = NULL;
    struct evfs_inode inode;
    evfs_t * evfs = NULL;

    if (argc != 3) {
        goto error;
    }

    ret = create_data_file(argv[1], argv[2]);
    if (ret < 0) {
        printf("%s: could not %s\n", argv[0], argv[2]);
        return 1;
    }

    inode.ino_nr = ret;
    evfs = evfs_open(argv[1]);
    if (evfs == NULL) {
        fprintf(stderr, "error: cannot open evfs device, errno = %s\n",
            strerror(-ret));
        goto error;
    }

    // TODO: use evfs functions to remap input data file
    ret = extent_alloc(evfs, 0, 1, NULL);
    if (ret < 0) {
        fprintf(stderr, "error: cannot allocate extent, errno = %s\n",
            strerror(-ret));
        goto done;
    }
    else {
        printf("allocated physical extent %d\n", ret);
    }

    pa = (u64)ret;
    ret = extent_write(evfs, pa, 0, NEW_MSG, sizeof(NEW_MSG));
    if (ret < 0) {
        fprintf(stderr, "error: could not write to owned extent %lu, "
            "errno = %s\n", pa, strerror(-ret));
        goto done;
    }

    /* the existing file uses 3 blocks. we will unmap the last 2 */
    imap = imap_new(evfs);
    ret = imap_append(&imap, 0, pa, 1);
    if (ret < 0) {
        fprintf(stderr, "error during imap errno = %d\n", ret);
        goto done;
    }
    
    ret = imap_append(&imap, 1, 0, 2);
    if (ret < 0) {
        fprintf(stderr, "error during imap errno = %d\n", ret);
        goto done;
    }

    printf("BEFORE INODE_MAP:\n");
    print_imap_info(evfs, inode.ino_nr);

    ret = atomic_inode_map(evfs, inode.ino_nr, imap, &inode.mtime);
    if (ret < 0) {
        fprintf(stderr, "error: could not map to inode %lu, "
            "errno = %s\n", inode.ino_nr, strerror(-ret));
        goto done;
    }

    printf("\nAFTER INODE_MAP:\n");
    print_imap_info(evfs, inode.ino_nr);

    ret = inode_info(evfs, &inode);
    if (ret < 0) {
        fprintf(stderr, "error: cannot read inode %lu, errno = %s\n",
            inode.ino_nr, strerror(-ret));
        goto done;
    }
    
    inode.bytesize = sizeof(NEW_MSG);
    ret = inode_update(evfs, &inode);
    if (ret < 0) {
        fprintf(stderr, "error: cannot update inode %lu, errno = %s\n",
            inode.ino_nr, strerror(-ret));
        goto done;
    }

    printf("\n");
    print_inode(&inode);
    ret = 0;
done:
    printf("FREE IMAP\n");
    imap_print(imap);
    imap_free(imap);
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

static long 
atomic_inode_map(evfs_t * evfs, long ino_nr, struct evfs_imap * imap,
                 struct evfs_timeval * mtime)
{
    long ret, id;
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
    
    // for diagnostics
    imap_print(imap);
    
    ret = inode_map(aa, ino_nr, imap);
    if (ret < 0) {
        goto fail;
    }
    
    ret = atomic_execute(aa);
fail:
    atomic_end(aa);
    return ret;
}

