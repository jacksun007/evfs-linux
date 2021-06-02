/*
 * evfs.c
 *
 * Just a bunch of scaffolds right now. Need to merge with refactored
 * Evfs kernel API
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include "evfs.h"
 
evfs_t * evfs_open(const char * dev)
{
    evfs_t * evfs = malloc(sizeof(evfs_t));
    
    if (!evfs) 
        return NULL;
        
    evfs->fd = open(dev, O_RDONLY);
	if (evfs->fd < 0) {
		perror("open device");
		free(evfs);
		evfs = NULL;
	}
	
    return evfs;
}

void evfs_close(evfs_t * evfs)
{
    if (evfs != NULL) {
        close(evfs->fd);
        free(evfs);
    }
}

evfs_iter_t * inode_iter(evfs_t * evfs, int flags)
{
    (void)evfs;
    (void)flags;
    return NULL;
}

evfs_iter_t * extent_iter(evfs_t * evfs, int flags)
{
    (void)evfs;
    (void)flags;
    return NULL;
}

u64 inode_next(evfs_iter_t * it)
{
    (void)it;
    return 0;
}

void iter_end(evfs_iter_t * it)
{
    (void)it;
}

int super_info(evfs_t * evfs, struct evfs_super_block * sb)
{
    (void)evfs;
    (void)sb;
    return -EINVAL;
}

int inode_info(evfs_t * evfs, struct evfs_inode * inode)
{
    (void)evfs;
    (void)inode;
    return -EINVAL;
}


