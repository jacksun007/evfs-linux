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
#include <time.h>
#include <evfs.h>

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV PATH\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, " PATH: path for new file on evfs device.\n");
    return 1;
}

// use vfs to create the new file first
static int create_data_file(const char * path)
{
    FILE * fs = fopen("data/input.txt", "r");
    FILE * fd;
    int c;
    
    if (!fs) {
        return -1;
    }
    
    fd = fopen(path, "w");
    if (!fd) {
        fclose(fs);
        return -1;
    }
    
    while ((c = fgetc(fs)) != EOF) {
        if (fputc(c, fd) == EOF) {
            // error during fputc
            break;
        }
    }
    
    
    fclose(fd);
    fclose(fs);
    
    return 0;
}

int main(int argc, char * argv[])
{
    int ret;

    if (argc != 3) {
        goto error;
    }

    ret = create_data_file(argv[2]);
    if (ret < 0) {
        printf("%s: could not %s\n", argv[0], argv[2]);
        return 1;
    }
    
    // TODO: use evfs functions to remap input data file
    
    return 0;
error:
    return usage(argv[0]);
}

