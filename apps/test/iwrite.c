/*
 * iwrite.c
 *
 * Tests inode_write, but requires that inode_read is functional as well
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <evfs.h>

#define BUFLEN 4096

int usage(char * prog)
{
    fprintf(stderr, "usage: %s DEV FROM FROMOFF TO TOOFF OFFSET LEN\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  FROM: inode number to read from\n");
    fprintf(stderr, "  FROMOFF: logical offset to read from\n");
    fprintf(stderr, "  TO: inode number to write to\n");
    fprintf(stderr, "  TOOFF: logical offset to write to\n");
    fprintf(stderr, "  LEN: length in bytes\n");

    return 1;
}

int main(int argc, char * argv[])
{
	evfs_t * evfs = NULL;
	char buf[BUFLEN + 1];
	u64 from_ino, from_off, to_ino, to_off, len;
	int ret;

	if (argc != 7) {
		goto error;
	} else if (!(evfs = evfs_open(argv[1]))) {
		goto error;
	}

	from_ino = (u64)atoi(argv[2]);
	from_off = (u64)atoi(argv[3]);
	to_ino = (u64)atoi(argv[4]);
	to_off = (u64)atoi(argv[5]);
	len = (u64)atoi(argv[6]);

	memset(buf, 0, BUFLEN + 1);
	ret = inode_read(evfs, from_ino, from_off, buf, len);
	if (ret < 0) {
		fprintf(stderr, "error: cannot read inode %lu, errno = %s\n",
			from_ino, strerror(-ret));
	} else {
		ret = inode_write(evfs, to_ino, to_off, buf, len);
		if (ret < 0)
			fprintf(stderr, "error: cannot write to inode %lu, errno = %s\n",
				to_ino, strerror(-ret));
	}

	evfs_close(evfs);
	return ret;
error:
	return usage(argv[0]);
}
