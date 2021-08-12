/*
 * exup.c
 *
 * Tests extent_active
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
    fprintf(stderr, "usage: %s DEV ADDR LEN\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, " ADDR: starting block address\n");
    fprintf(stderr, "  LEN: length of extent\n");

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

    if (argc < 4) {
        goto error;
    }
    else {
        evfs = evfs_open(argv[1]);
    }
    
    if (evfs == NULL) {
        goto error;
    }
    
    extent.addr = (u64)atoi(argv[2]);
    extent.len  = (u64)atoi(argv[3]);
    
    ret = extent_active(evfs, extent.addr, extent.len, EVFS_ALL);
    if (ret < 0) {
        fprintf(stderr, "error during extent_active, errno = %s\n", 
            strerror(-ret));
        goto done;
    }
    else if (ret == 0) {
        printf("extent %lu of length %lu is not fully active.\n", 
            extent.addr, extent.len);
    }
    else {
        printf("extent %lu of length %lu is fully active.\n", 
            extent.addr, extent.len);
    }
    
done:
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

