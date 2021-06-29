/*
 * exwrite.c
 *
 * Tests extent_write
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <evfs.h>

#define PAGE_SIZE 4096

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV FROM FROMOFF TOADDR LEN\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  FROM: inode number to read from\n");
    fprintf(stderr, "  FROMOFF: logical offset to read from\n");
    fprintf(stderr, "  TOADDR: block address to write to\n");
    fprintf(stderr, "  LEN: length in blocks\n");

    return 1;
}

int main(int argc, char * argv[])
{
	evfs_t * evfs = NULL;
	char * buf;
	u64 from_ino, from_off, to_addr, len;
	int ret;

	if (argc != 6) {
		goto error;
	} else if (!(evfs = evfs_open(argv[1]))) {
		goto error;
	}


	from_ino = (u64)atoi(argv[2]);
	from_off = (u64)atoi(argv[3]);
	to_addr = (u64)atoi(argv[4]);
	len = (u64)atoi(argv[5]);

	buf = malloc(len * PAGE_SIZE + 1);

	memset(buf, 0, len * PAGE_SIZE + 1);
	ret = inode_read(evfs, from_ino, from_off, buf, len * PAGE_SIZE);
	if (ret < 0) {
		fprintf(stderr, "error: cannot read inode %lu, errno = %s\n",
			from_ino, strerror(-ret));
	} else {
		ret = extent_write(evfs, to_addr, 0, buf, len);
		if (ret < 0)
			fprintf(stderr, "error: cannot write to address %lu, errno = %s\n",
				to_addr, strerror(-ret));
	}

	evfs_close(evfs);
	return ret;
error:
	return usage(argv[0]);
}
