/*
 * iinfo.c
 *
 * Tests inode_iter
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <evfs.h>

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    return 1;
}

int main(int argc, char **argv) {
	evfs_t * evfs = NULL;
	evfs_iter_t * iter = NULL;
	u64 ino_nr;

	if (argc > 3) {
		goto error;
	}

	if (argc > 1) {
		evfs = evfs_open(argv[1]);
	}

	if (evfs == NULL) {
		goto error;
	}

	iter = inode_iter(evfs, 0);
	if (!iter) {
		evfs_close(evfs);
		goto error;
	}

	while ((ino_nr = inode_next(iter)))
		printf("inode %lu\n", ino_nr);

	iter_end(iter);

	evfs_close(evfs);
	return 0;
error:
	return usage(argv[0]);
}
