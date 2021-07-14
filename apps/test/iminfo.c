/*
 * iminfo.c
 *
 * Tests inode_info
 *
 */

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <evfs.h>
#include "common.h"

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV NAME\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, " NAME: name for new file on evfs device.\n");
    return 1;
}

char pathname[1024]; 

// note: unsafe because dst length is not specified
void path_join(char * dst, const char * first, const char * second)
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
static int create_data_file(const char * dir, const char * name)
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
        fprintf(stderr, "cannot stat: %s\n", strerror(errno));
        ret = -errno;
    }
    else {
        ret = stats.st_ino;
        printf("created %s, ino_nr = %lu\n", pathname, stats.st_ino);
    }

    return ret;
}

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    u64 ino_nr;
    struct evfs_imap * imap;
    int ret = 0;
    unsigned i;
    
    if (argc != 3) {
        goto error;
    }

    ret = create_data_file(argv[1], argv[2]);
    if (ret < 0) {
        printf("%s: cannot create %s\n", argv[0], argv[2]);
        return 1;
    }

    evfs = evfs_open(argv[1]);
    if (evfs == NULL) {
        goto error;
    }
    
    ino_nr = ret;
    imap = imap_info(evfs, ino_nr);
    
    if (imap == NULL) {
        fprintf(stderr, "error: cannot read mapping of inode %lu\n", ino_nr);
        goto done;
    }
    
    printf("file has %u extent(s):\n", imap->count);
    for (i = 0; i < imap->count; i++) {
        struct evfs_imentry * e = &imap->entry[i];
        printf("%u: log: %lu, phy: %lu, len: %lu ", e->index, e->log_addr,
            e->phy_addr, e->len);
        
        if (e->inlined)
            printf("(inlined)");

        printf("\n");
    }
    
    imap_free(evfs, imap, 1);   
done:
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

