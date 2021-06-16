/*
 * iinfo.c
 *
 * Tests inode_info
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
    fprintf(stderr, "usage: %s DEV NUM\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  NUM: inode number\n");
    return 1;
}

// NOTE: not thread-safe!
const char * timevalstr(struct evfs_timeval * tv)
{
    static char buf[256];
    struct tm * loctime;
    unsigned len;
    
    loctime = localtime((const time_t *)&tv->tv_sec);
    len = strftime(buf, sizeof(buf), "%F %T.", loctime);
    snprintf(buf + len, sizeof(buf) - len, "%06lu", tv->tv_usec);
    return buf;
}

// TODO: complete me
void print_inode(struct evfs_inode * inode)
{
    printf("ino_nr: %lu\n", inode->ino_nr);
    printf("atime: %s\n", timevalstr(&inode->atime));
    printf("ctime: %s\n", timevalstr(&inode->ctime));
    printf("mtime: %s\n", timevalstr(&inode->mtime));
    printf("otime: %s\n", timevalstr(&inode->otime));
    printf("uid: %u\n", inode->uid);
    printf("gid: %u\n", inode->gid);
    printf("mode: 0x%X\n", inode->mode);
    printf("flags: 0x%X\n", inode->flags);
    printf("bytesize: %lu\n", inode->bytesize);
    printf("refcount: %u\n", inode->prop.refcount);
    printf("blockcount: %lu\n", inode->prop.blockcount);
    printf("inlined_bytes: %lu\n", inode->prop.inlined_bytes);
}

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    struct evfs_inode inode;
    int ret;

    if (argc > 3) {
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
    
    inode.ino_nr = (u64)atoi(argv[2]);
    ret = inode_info(evfs, &inode);
    if (ret < 0) {
        fprintf(stderr, "error: cannot read inode %lu, errno = %s\n", 
            inode.ino_nr, strerror(-ret));
    }
    else {
        print_inode(&inode);
    }
       
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

