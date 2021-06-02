/*
 * defrag.c
 *
 * Generic defragmentation tool built using Evfs API
 *
 */

#include <evfs.h>
#include <stdio.h>
#include <stdlib.h>

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV [NUM]\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  NUM: Inode number of file to defragment.\n");
    fprintf(stderr, "       When not specified, defragment all files.\n");
    return 1;
}

int defragment(evfs_t * evfs, struct evfs_super_block * sb, long ino_nr)
{
    struct evfs_inode inode;
    int ret;
    
    inode.ino_nr = ino_nr;
    ret = inode_info(evfs, &inode);
    
    if (!ret) {
        return ret;
    }
    
    /* refactor defragmentation logic here */
    
    (void)sb;
    return ret;    
}

int defragment_all(evfs_t * evfs, struct evfs_super_block * sb)
{
    evfs_iter_t * it = inode_iter(evfs, 0);
    int ret = 0;
    long ino_nr;
    
    while ((ino_nr = inode_next(it)) > 0) {
        ret = defragment(evfs, sb, ino_nr);
        if (ret != 0) {
            break;
        }
    }
    
    iter_end(it);
    return ret;
}

int main(int argc, char * argv[])
{
    evfs_t * evfs = NULL;
    struct evfs_super_block sb;
    int ret;

    if (argc > 3) {
        goto error;
    }

    if (argc > 1) {
        evfs = evfs_open(argv[1]);
    }
    
    if (evfs == NULL) {
        goto error;
    }
    
    if ((ret = super_info(evfs, &sb)) < 0) {
        fprintf(stderr, "Error: could not retrieve super block info.\n");
        goto done;
    }

    if (argc == 2) {
        ret = defragment_all(evfs, &sb);
    }
    else if (argc == 3) {
        int ino_nr = atoi(argv[2]);
        if (ino_nr <= 0) {
            fprintf(stderr, "Invalid inode number: %s\n", argv[2]);
            goto error; 
        }
        else {
            ret = defragment(evfs, &sb, ino_nr);
        }
    }
    
done:    
    evfs_close(evfs);
    return ret;
error:
    return usage(argv[0]);
}

