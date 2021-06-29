#ifndef COMMON_H_
#define COMMON_H_

#include <time.h>
#include <stdio.h>

// NOTE: not thread-safe!
static inline
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
static inline
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

#endif

