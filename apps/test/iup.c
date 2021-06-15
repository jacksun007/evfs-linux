/*
 * iup.c
 *
 * Tests inode_update
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <evfs.h>

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV NUM VAL\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  NUM: inode number\n");
    fprintf(stderr, "  VAL: new size of file\n");
    return 1;
}

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    struct evfs_inode inode;
    int ret;

    if (argc > 4) {
        goto error;
    }

    if (argc > 1) {
        evfs = evfs_open(argv[1]);
    }
    
    if (evfs == NULL) {
        goto error;
    }
    
    inode.ino_nr = (u64)atoi(argv[2]);
    ret = inode_info(evfs, &inode);
    if (ret < 0) {
        fprintf(stderr, "error: cannot read inode %lu, errno = %s\n", 
            inode.ino_nr, strerror(-ret));
        goto done;
    }
    
    inode.bytesize = (u64)atoi(argv[3]);
    ret = inode_update(evfs, &inode);
    if (ret < 0) {
        fprintf(stderr, "error: cannot update inode %lu, errno = %s\n", 
            inode.ino_nr, strerror(-ret));
    }
    
    printf("success: inode %lu's size is now set to %lu bytes.\n", 
        inode.ino_nr, inode.bytesize); 
done:
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

