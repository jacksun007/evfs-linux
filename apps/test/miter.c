/*
 * miter.c
 *
 * Tests metadata_iter
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <evfs.h>

int usage(char * prog)
{
	fprintf(stderr, "usage: %s DEV NUM\n", prog);
	fprintf(stderr, "  DEV: device of the file system.\n");
	fprintf(stderr, "  NUM: inode number\n");
	return 1;
}

int main (int argc, char **argv) {
	evfs_t * evfs = NULL;
	evfs_iter_t * iter = NULL;
	// struct evfs_metadata meta = { 0 };
	u64 ino_nr;

	if (argc != 3)
		goto error;

	evfs = evfs_open(argv[1]);
	ino_nr = atol(argv[2]);

	if (evfs == NULL) {
		goto error;
	}

	iter = metadata_iter(evfs, ino_nr);
	if (!iter) {
		evfs_close(evfs);
		goto error;
	}

	/* meta = metadata_next(iter); */
	/* while (meta.blkaddr) { */
	/* 	printf("owner = %lu\n", meta.owner); */
	/* 	meta = metadata_next(iter); */
	/* } */

	iter_end(iter);

	evfs_close(evfs);
	return 0;
error:
	return usage(argv[0]);
}
