/*
 * test.c
 *
 * Tests various Evfs operations
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <evfs.h>

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV NUM\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  NUM: inode number\n");
    return 1;
}

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    struct evfs_inode inode;
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
    
    inode.ino_nr = (u64)atoi(argv[2]);
    ret = inode_info(evfs, &inode);
    if (ret < 0) {
        fprintf(stderr, "error: cannot read inode %llu\n", inode.ino_nr);
    }
    else {
        printf("size of inode %llu is %llu\n", inode.ino_nr, inode.bytesize);
    }
       
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

