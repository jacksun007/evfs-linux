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
#include <getopt.h>
#include <ctype.h>

int usage(char * prog)
{
    fprintf(stderr, "usage: %s [-m] DEV LEN [START]\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  LEN: length of extent\n");
    fprintf(stderr, "START: starting block address\n");
    return 1;
}

struct evfs_extent extent = { 0 };
struct evfs_extent_attr attr = { 0 };

long get_arguments(int argc, char * argv[], char ** dnptr)
{
    int c;

    while ((c = getopt(argc, argv, "m")) != -1)
        switch (c)
        {
        case 'm':
            attr.metadata = 1;
        break;
        case '?':
            if (isprint(optopt))
                fprintf(stderr, "Unknown option `-%c'.\n", optopt);
            else
                fprintf(stderr,
                   "Unknown option character `\\x%x'.\n",
                   optopt);
            return -1;
        default:
            return -1;
        }
  
    // shift to non-option arguments
    argv = argv + optind;
    argc = argc - optind;
    
    if (argc > 3) {
        return -1;
    }

    if (argc < 2) {
        return -1;
    }
    
    extent.len  = (u64)atoi(argv[1]);
    if (argc > 2)
        extent.addr = (u64)atoi(argv[2]);
    else
        extent.addr = 0;
    
    *dnptr = argv[0];
    return 0;
}

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    char * devname;
    int ret;

    if (get_arguments(argc, argv, &devname) < 0) {
        goto error;
    }
    
    evfs = evfs_open(devname);
    if (evfs == NULL) {
        goto error;
    }
    
    if (attr.metadata) {
        printf("allocating metadata block(s)\n");   
    }
    
    ret = extent_alloc(evfs, extent.addr, extent.len, &attr);
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

