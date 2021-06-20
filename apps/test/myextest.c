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
    fprintf(stderr, "usage: %s DEV\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    return 1;
}

#define NUM_EXTENTS 5

struct evfs_extent extent[NUM_EXTENTS];

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    int ret;
    unsigned len = 6;
    int i;

    if (argc != 2) {
        goto error;
    }

    evfs = evfs_open(argv[1]);
    if (evfs == NULL) {
        goto error;
    }
    
    for (i = 0; i < NUM_EXTENTS; i++) {
        ret = extent_alloc(evfs, 0, len, 0);
        if (ret < 0) {
            fprintf(stderr, "error: cannot allocate extent, errno = %s\n", 
                strerror(-ret));
            goto done;
        }

        printf("success: extent of length %u is allocated at address %d.\n", 
            len, ret);
            
        extent[i].addr = ret;
        extent[i].len = len;    
        len++;
    }
    
    debug_my_extents(evfs);
    
    for (i = 1; i <= NUM_EXTENTS/2; i++) {
        ret = extent_free(evfs, extent[i].addr, extent[i].len, 0);
        if (ret < 0) {
            fprintf(stderr, "error: cannot free owned extent, errno = %s\n", 
                strerror(-ret));
            goto done;
        }
        
        printf("success: extent (%lu, %lu) has been freed.\n", 
               extent[i].addr, extent[i].len);
    }
    
    debug_my_extents(evfs);
     
done:
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

