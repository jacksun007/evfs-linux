/*
 * common.c
 *
 * shared testing functions
 *
 */

#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <evfs.h>

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


char pathname[1024];

// note: unsafe because dst length is not specified
static void path_join(char * dst, const char * first, const char * second)
{
    int i, c;

    for (i = 0; first[i] != '\0'; i++) {
        dst[i] = first[i];
    }

    dst[i++] = '/';

    for (c = 0; second[c] != '\0'; c++) {
        dst[i++] = second[c];
    }

    dst[i] = '\0';
}

// use vfs to create the new file first
int create_data_file(const char * dir, const char * name)
{
    FILE * fs = fopen("data/input.txt", "r");
    FILE * fd;
    struct stat stats;
    int c;
    int ret;

    if (!fs) {
        return -1;
    }

    // create new path name
    path_join(pathname, dir, name);

    fd = fopen(pathname, "w");
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

    if (stat(pathname, &stats) < 0) {
        fprintf(stderr, "cannot fstat: %s\n", strerror(errno));
        ret = -errno;
    }
    else {
        ret = stats.st_ino;
        printf("created %s, ino_nr = %lu\n", pathname, stats.st_ino);
    }

    return ret;
}

void print_imap(struct evfs_imap *imap)
{  
    unsigned i;
  
    printf("file has %u extent(s):\n", imap->count);
    
    for (i = 0; i < imap->count; i++) {
        struct evfs_imentry * e = &imap->entry[i];
        printf("%u: log: %lu, phy: %lu, len: %lu ", e->index, e->log_addr,
            e->phy_addr, e->len);
        
        if (e->inlined)
            printf("(inlined)");

        printf("\n");
    }
}       

