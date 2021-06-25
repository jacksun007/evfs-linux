/*
 * iread.c
 *
 * Tests inode_read
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
    fprintf(stderr, "usage: %s DEV NUM OFFSET LEN\n", prog);
    fprintf(stderr, "  DEV: device of the file system.\n");
    fprintf(stderr, "  NUM: inode number\n");
    fprintf(stderr, "  OFFSET: logical offset\n");
    fprintf(stderr, "  LEN: length in bytes\n");

    return 1;
}

int main(int argc, char * argv[])
{
	evfs_t * evfs = NULL;
	char buf[BUFLEN + 1];
	u64 ino_nr, off, len;
	int ret;

	if (argc != 5) {
		goto error;
	} else if (!(evfs = evfs_open(argv[1]))) {
		goto error;
	}

	ino_nr = (u64)atoi(argv[2]);
	off = (u64)atoi(argv[3]);
	len = (u64)atoi(argv[4]);

	memset(buf, 0, BUFLEN + 1);
	ret = inode_read(evfs, ino_nr, off, buf, len);
	if (ret < 0) {
		fprintf(stderr, "error: cannot read inode %lu, errno = %s\n",
			ino_nr, strerror(-ret));
	} else {
		printf("%s\n", buf);
	}

	evfs_close(evfs);
	return ret;
error:
	return usage(argv[0]);
}
