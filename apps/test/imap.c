/*
 * imap.c
 *
 * Tests extent_alloc, extent_write, and inode_imap
 *
 */

#include <sys/stat.h>
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
    struct stat stats;
    int c;
    int ret;
    
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
    
    if (fstat(fileno(fd), &stats) < 0) {
        fprintf(stderr, "cannot fstat: %s\n", strerror(errno));
        ret = -errno;
    }
    else {
        ret = stats.st_ino;
    }
    
    fclose(fd);
    fclose(fs);
    return ret;
}

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

    ret = create_data_file(argv[2]);
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
    ret = extent_alloc(evfs, 0, 1, 0);
    if (ret < 0) {
        fprintf(stderr, "error: cannot allocate extent, errno = %s\n", 
            strerror(-ret));
        goto done;
    }
    
    pa = ret;
    ret = extent_write(evfs, pa, 0, NEW_MSG, sizeof(NEW_MSG));
    if (ret < 0) {
        fprintf(stderr, "error: could not write to owned extent %lu, "
            "errno = %s\n", pa, strerror(-ret));
        goto done;
    }
    
    /* the existing file uses 3 blocks. we will unmap the last 2 */
    imap = imap_new(evfs);
    imap_append(imap, 0, pa, 1);
    imap_append(imap, 1, 0, 2);
    
    ret = inode_map(evfs, inode.ino_nr, imap);
    if (ret < 0) {
        fprintf(stderr, "error: could not map to inode %lu, "
            "errno = %s\n", inode.ino_nr, strerror(-ret));
        goto done;
    }

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
 
    ret = 0;
done: 
    imap_free(evfs, imap, 1);
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

