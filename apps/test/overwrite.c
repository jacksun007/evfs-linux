/*
 * iminfo.c
 *
 * Tests inode_info
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <evfs.h>
#include <errno.h>
#include "common.h"

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV NUM OFF\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  NUM: inode number.\n");
    fprintf(stderr, "  OFF: file offset to overwrite.\n");
    return 1;
}

static int print_imap_info(const char * root, u64 ino_nr)
{
    int ret = 0;
    struct evfs_imap * imap;
    evfs_t * evfs = evfs_open(root);
    
    if (evfs == NULL) {
        return -ENOENT;
    }
    
    imap = imap_info(evfs, ino_nr);
    if (imap == NULL) {
        ret = -EINVAL;
        fprintf(stderr, "error: cannot read mapping of inode %lu\n", ino_nr);
        goto done;
    }
    
    print_imap(imap);
    imap_free(imap); 
      
done:
    evfs_close(evfs);
    return ret;
}

static int write_zero(int fd, off_t curoff, off_t lastoff, size_t blksize)
{
    ssize_t rv;
    off_t ptr;
    
    // printf("write_zero: curoff = %ld, lastoff = %ld, blksize = %lu\n", 
    //    curoff, lastoff, blksize);

    for (ptr = curoff ; ptr < lastoff ; ptr += blksize) {
        if (lseek(fd, ptr, SEEK_SET) < 0)
            return(errno);
        rv = write(fd, "", 1);    /* writes a null */
        if (rv < 0)
            return(errno);
        if (rv == 0)
            return(EIO);
    }
    
    return 0;
}

static int issueWrite(const char *path, off_t offset, size_t len, size_t blksize) {
    int fd, rv;

    if (len <= 0)
        return -1;  

    // open file as write-only, file must exist
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        printf("Could not open file: errno %d\n", errno);
        return fd;
    }

    // write zeros to file
    rv = write_zero(fd, offset, offset + len, blksize);

    if(rv != 0) {
        fprintf(stderr, "issueWrite: write failed: %s\n", strerror(rv));
        abort();
    }

    rv = close(fd);
    return rv;
}

static void writeFile(int arg)
{
    size_t bsz = 4096;
    off_t bkoff = (off_t)arg;
    size_t nblks = 1;
    
    off_t off = bsz * bkoff;
    size_t size = bsz * nblks;
    
    issueWrite("/home/sunk/test-disk/largefile.tgz", off, size, bsz);
}

int main(int argc, char * argv[])
{   
    u64 ino_nr;
    int offset;
    int ret = 0;

    if (argc != 4) {
        goto error;
    }

    ret = atoi(argv[2]);
    if (ret == 0) {
        printf("%s: '%s' is an invalid inode number\n", argv[0], argv[2]);
        return 1;
    }
    
    offset = atoi(argv[3]);
    ino_nr = ret;
    
    printf("Before:\n");
    if ((ret = print_imap_info(argv[1], ino_nr)) < 0) {
        goto done;
    }
    
    writeFile(offset);
    
    printf("After:\n");
    ret = print_imap_info(argv[1], ino_nr);
    
done:
    return ret;
error:
    return usage(argv[0]);
}

