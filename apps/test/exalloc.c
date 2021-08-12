/*
 * exalloc.c
 *
 * Tests extent_alloc
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
    fprintf(stderr, "usage: %s DEV LEN [START]\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  LEN: length of extent\n");
    fprintf(stderr, "START: starting block address\n");
    return 1;
}

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    struct evfs_extent extent;
    int ret;

    if (argc > 4) {
        goto error;
    }

    if (argc < 3) {
        goto error;
    }
    else {
        evfs = evfs_open(argv[1]);
    }
    
    if (evfs == NULL) {
        goto error;
    }
    
    extent.len  = (u64)atoi(argv[2]);
    if (argc > 3)
        extent.addr = (u64)atoi(argv[3]);
    else
        extent.addr = 0;
    
    ret = extent_alloc(evfs, extent.addr, extent.len, NULL);
    if (ret < 0) {
        fprintf(stderr, "error: cannot allocate extent, errno = %s\n", 
            strerror(-ret));
        goto done;
    }

    printf("success: extent of length %lu is allocated at block address %d.\n", 
        extent.len, ret); 
done:
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

