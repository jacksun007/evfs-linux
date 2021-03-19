#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "kernevfs.h"

#define PAGE_SIZE 4096

int main(int argc, char **argv)
{
	struct evfs_inode_read_op read_op;
	struct evfs_ext_write_op write_op;
	int fd, err;

	if (argc != 6) {
		fprintf(stderr, "usage: evfs_copy <mnt> <inode_from>"
				" <offset_from> <length> <addr_to>\n");
		return 1;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open device");
		return 1;
	}

	char data[strtoll(argv[4], NULL, 10)];

	read_op.ino_nr = strtoll(argv[2], NULL, 10);
	read_op.ofs = strtoll(argv[3], NULL, 10);
	read_op.length = strtoll(argv[4], NULL, 10);
	read_op.data = data;

	err = ioctl(fd, FS_IOC_INODE_READ, &read_op);
	if (err) {
		perror("ioctl read");
		return 1;
	}

	printf("%s", data);

	write_op.addr = strtoll(argv[5], NULL, 10);
	write_op.length = read_op.length;
	write_op.data = data;
	err = ioctl(fd, FS_IOC_EXTENT_WRITE, &write_op);
	if (err) {
		perror("ioctl write");
		return 1;
	}

	return 0;
}
