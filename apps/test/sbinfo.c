/*
 * sbinfo.c
 *
 * Tests super_info
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
    fprintf(stderr, "usage: %s DEV\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    return 1;
}

void print_super(struct evfs_super_block * sb)
{
    printf("max_extent_size: %lu\n", sb->max_extent_size);
    printf("max_bytes: %lu\n", sb->max_bytes);
    printf("block_count: %lu\n", sb->block_count);
    printf("root_ino: %lu\n", sb->root_ino);
    printf("block_size: %lu\n", sb->block_size);
}

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    struct evfs_super_block super;
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
    
    ret = super_info(evfs, &super);
    if (ret < 0) {
        fprintf(stderr, "error: cannot read super block, errno = %s\n", 
            strerror(-ret));
    }
    else {
        print_super(&super);
    }
       
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

