/*
 * iminfo.c
 *
 * Tests inode_info
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    u64 ino_nr;
    struct evfs_imap * imap;
    int ret = 0;

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
    
    // common.c 
    print_imap(imap);
    
    imap_free(evfs, imap, 1);   
done:
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

