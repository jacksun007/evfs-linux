#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "kernevfs.h"

int evfs_ealloc(int fd, int argc, char **argv)
{
	struct evfs_extent_alloc_op ext_op;
	struct evfs_extent ext;
	int ret = 0;

	if (argc != 3) {
		fprintf(stderr, "usage: evfs_runner ealloc <ino_nr> <blkaddr> <length>\n");
		return 1;
	}

	ext_op.ino_nr = strtoll(argv[0], NULL, 10);
	ext_op.flags = EVFS_EXTENT_ALLOC_FIXED;
	ext.start = strtoll(argv[1], NULL, 10);
	ext.length = strtoll(argv[2], NULL, 10);
	ext_op.extent = ext;

	ret = ioctl(fd, FS_IOC_EXTENT_ALLOC, &ext_op);
	if (ret < 0) {
		perror("extent alloc");
		return 1;
	} else if (ret != ext.start) {
		printf("Hint failed. Created extent starting %d with "
				"length of %d\n", ext.start, ext.length);
	} else {
		printf("Hint successful. Created extent starting %d with "
				"length of %d\n", ext.start, ext.length);
	}

	return 0;
}

int evfs_efree(int fd, int argc, char **argv)
{
	struct evfs_extent ext;
	int ret;

	if (argc != 3) {
		fprintf(stderr, "usage: evfs_runner efree <ino_nr> <blkaddr> <length>\n");
		return 1;
	}

	ext.ino_nr = strtoll(argv[0], NULL, 10);
	ext.start = strtoll(argv[1], NULL, 10);
	ext.length = strtoll(argv[2], NULL, 10);

	ret = ioctl(fd, FS_IOC_EXTENT_FREE, &ext);
	if (ret) {
		perror("ioctl");
		return ret;
	}

	return ret;
}

int evfs_eactive(int fd, int argc, char **argv)
{
	struct evfs_extent_query query;
	int ret;

	if (argc != 3) {
		fprintf(stderr, "usage: evfs_runner eactive <type> <blkaddr> <length>\n");
		return 1;
	}

	query.query = strtoll(argv[0], NULL, 10);
	query.extent.start = strtoll(argv[1], NULL, 10);
	query.extent.length = strtoll(argv[2], NULL, 10);

	ret = ioctl(fd, FS_IOC_EXTENT_ACTIVE, &query);
	if (ret < 0) {
		perror("ioctl");
		return 1;
	} else if (ret) {
		printf("Given extent is active\n");
	} else {
		printf("Given extent is NOT active\n");
	}

	return ret;
}

int evfs_ewrite(int fd, int argc, char **argv)
{
	const int nbytes = 12000;
	struct evfs_ext_write_op op;
	char *data = malloc(nbytes);

	if (argc != 1) {
		fprintf(stderr, "usage: evfs_runner ewrite <addr>\n");
		return 1;
	}

	op.addr = strtoll(argv[0], NULL, 10);
	op.length = nbytes;
	op.data = data;

	memset(data, 'a', nbytes);

	if (ioctl(fd, FS_IOC_EXTENT_WRITE, &op)) {
		perror("ioctl");
		return 1;
	}

	return 0;
}

int evfs_eiter(int fd, int argc, char **argv)
{
	struct __evfs_ext_iter_param *param;
	struct evfs_iter_ops iter = { .start_from = 0 };
	int ret = 0, count = 0;

	if (argc != 1) {
		fprintf(stderr, "usage: evfs_runner eiter <ino_nr>\n");
		return 1;
	}

	iter.ino_nr = strtoll(argv[0], NULL, 10);

iterate:
	ret = ioctl(fd, FS_IOC_EXTENT_ITERATE, &iter);
	if (ret < 0) {
		perror("ioctl");
		return 1;
	}

	while (count < iter.count) {
		param = ((struct __evfs_ext_iter_param *) iter.buffer) + count;

		printf("inode: %lu, log_blkoff: %lu, phy_blkoff: %lu, "
				"length: %lu\n", iter.ino_nr,
				param->log_blkoff, param->phy_blkoff,
				param->length);

		++count;
	}

	count = 0;
	iter.start_from = param->log_blkoff + param->length;
	if (ret)
		goto iterate;

	return 0;
}

int evfs_freespiter(int fd, int argc, char **argv)
{
	struct __evfs_fsp_iter_param *param;
	struct evfs_iter_ops iter = { .start_from = 0 };
	int ret = 0, count = 0;

iterate:
	ret = ioctl(fd, FS_IOC_FREESP_ITERATE, &iter);
	if (ret < 0) {
		perror("ioctl");
		return 1;
	}

	while (count < iter.count) {
		param = ((struct __evfs_fsp_iter_param *) iter.buffer) + count;
		printf("addr: %lu, length: %lu\n", param->addr, param->length);
		++count;
	}

	count = 0;
	iter.start_from = param->addr + param->length;
	if (ret)
		goto iterate;

	return 0;
}

int evfs_ialloc(int fd, int argc, char **argv)
{
	struct evfs_inode inode;
	int perm;

	if (argc != 4) {
		fprintf(stderr, "usage: evfs_runner ialloc <ino nr> <uid> "
				"<gid> <perm>\n");
		return 1;
	}

	inode.ino_nr = strtoll(argv[0], NULL, 10);
	inode.uid = strtoll(argv[1], NULL, 10);
	inode.gid = strtoll(argv[2], NULL, 10);
	perm = strtoll(argv[3], NULL, 8);
	inode.mode = S_IFREG | perm;

	if (ioctl(fd, FS_IOC_INODE_ALLOC, &inode) < 0) {
		perror("ioctl");
		return 1;
	}

	printf("Created inode %d\n", inode.ino_nr);
	return 0;
}

int evfs_ifree(int fd, int argc, char **argv)
{
	long ino_nr;

	if (argc != 1) {
		fprintf(stderr, "usage: evfs_runner ifree <ino nr>\n");
		return 1;
	}

	ino_nr = strtoll(argv[0], NULL, 10);
	if (ioctl(fd, FS_IOC_INODE_FREE, &ino_nr) < 0) {
		perror("ioctl free");
		return 1;
	}

	return 0;
}

int evfs_iread(int fd, int argc, char **argv)
{
	struct evfs_inode_read_op read_op;
	int ret = 0;

	if (argc != 3) {
		fprintf(stderr, "usage: evfs_runner iread <ino nr> "
				"<offset> <length>\n");
		return 1;
	}

	read_op.ino_nr = strtoll(argv[0], NULL, 10);
	read_op.ofs = strtoll(argv[1], NULL, 10);
	read_op.length = strtoll(argv[2], NULL, 10);
	read_op.data = malloc(read_op.length + 1);

	read_op.data[read_op.length] = '\0';
	ret = ioctl(fd, FS_IOC_INODE_READ, &read_op);
	if (ret) {
		perror("ioctl iread");
		return 1;
	}

	printf("%s\n", read_op.data);

	return 0;
}

int evfs_iget(int fd, int argc, char **argv)
{
	struct evfs_inode inode;

	if (argc != 1) {
		fprintf(stderr, "usage: evfs_runner iget <ino nr>\n");
		return 1;
	}

	inode.ino_nr = strtoll(argv[0], NULL, 10);
	if (ioctl(fd, FS_IOC_INODE_GET, &inode) < 0) {
		perror("ioctl");
		return 1;
	}

	printf("Inode %d:\n"
		"\tuid: %d\n"
		"\tgid: %d\n"
		"\tmode: %o\n",
		inode.ino_nr,
		inode.uid,
		inode.gid,
		inode.mode);
	return 0;
}

int evfs_iset(int fd, int argc, char **argv)
{
	struct evfs_inode inode;

	if (argc != 3) {
		fprintf(stderr, "usage: evfs_runner iset <ino nr> <uid> "
				"<gid>\n");
		return 1;
	}

	inode.ino_nr = strtoll(argv[0], NULL, 10);
	inode.uid = strtoll(argv[1], NULL, 10);
	inode.gid = strtoll(argv[2], NULL, 10);
	inode.mode = S_IFREG;

	if (ioctl(fd, FS_IOC_INODE_SET, &inode) < 0) {
		perror("ioctl");
		return 1;
	}
	return 0;
}

int evfs_imap(int fd, int argc, char **argv)
{
	struct evfs_imap evfs_i;

	if (argc != 4) {
		fprintf(stderr, "usage: evfs_runner imap <ino nr> <length> "
				"<logical blk> <physical blk>\n");
		return 1;
	}

	evfs_i.ino_nr = strtoll(argv[0], NULL, 10);
	evfs_i.length = strtoll(argv[1], NULL, 10);
	evfs_i.log_blkoff = strtoll(argv[2], NULL, 10);
	evfs_i.phy_blkoff = strtoll(argv[3], NULL, 10);

	if (ioctl(fd, FS_IOC_INODE_MAP, &evfs_i) < 0) {
		perror("ioctl");
		return 1;
	}

	printf("Physical block %d-%d mapped to logical block %d-%d for "
			"inode %d\n", evfs_i.phy_blkoff,
			evfs_i.phy_blkoff + evfs_i.length - 1,
			evfs_i.log_blkoff, evfs_i.log_blkoff + evfs_i.length - 1,
			evfs_i.ino_nr);

	return 0;
}

int evfs_iunmap(int fd, int argc, char **argv)
{
	struct evfs_imap evfs_i;

	if (argc != 3) {
		fprintf(stderr, "usage: evfs_runner iunmap <ino nr> <length> "
				"<logical blk>\n");
		return 1;
	}

	evfs_i.ino_nr = strtoll(argv[0], NULL, 10);
	evfs_i.length = strtoll(argv[1], NULL, 10);
	evfs_i.log_blkoff = strtoll(argv[2], NULL, 10);

	if (ioctl(fd, FS_IOC_INODE_UNMAP, &evfs_i) < 0) {
		perror("ioctl");
		return 1;
	}

	return 0;
}

int evfs_inoiter(int fd, int argc, char **argv)
{
	struct __evfs_ino_iter_param *param;
	struct evfs_iter_ops iter = { .start_from = 0 };
	int ret = 0, count = 0;

iterate:
	ret = ioctl(fd, FS_IOC_INODE_ITERATE, &iter);
	if (ret < 0) {
		perror("ioctl");
		return 1;
	}

	while (count < iter.count) {
		param = ((struct __evfs_ino_iter_param *) iter.buffer) + count;
		printf("inode: %d\n", param->ino_nr);
		++count;
	}

	count = 0;
	iter.start_from = param->ino_nr + 1;
	if (ret)
		goto iterate;

	return 0;
}

int evfs_dadd(int fd, int argc, char **argv)
{
	int ret = 0;
	struct stat file_stat;
	struct evfs_dirent_add_op op;

	if (argc != 3) {
		printf("usage: evfs_runner dadd <dir> <inode> <name>\n");
		return 1;
	}

	ret = stat(argv[0], &file_stat);
	if (ret < 0) {
		perror("stat");
		return 1;
	}

	op.dir_nr = file_stat.st_ino;
	op.ino_nr = strtoll(argv[1], NULL, 10);
	op.name_len = strlen(argv[2]);
	strncpy(op.name, argv[2], sizeof(op.name));
	op.file_type = REGULAR_FILE;

	ret = ioctl(fd, FS_IOC_DIRENT_ADD, &op);
	if (ret < 0) {
		perror("ioctl");
		return 1;
	}

	return 0;
}

int evfs_drm(int fd, int argc, char **argv)
{
	int ret = 0;
	struct evfs_dirent_add_op op;

	if (argc != 2) {
		fprintf(stderr, "usage: evfs_runner drm <name> <dir_nr>\n");
		return 1;
	}

	strncpy(op.name, argv[0], sizeof(op.name));
	op.dir_nr = strtoll(argv[1], NULL, 10);

	ret = ioctl(fd, FS_IOC_DIRENT_REMOVE, &op);
	if (ret < 0) {
		perror("ioctl");
		return 1;
	}

	return 0;
}

int evfs_sbget(int fd, int argc, char **argv)
{
	int ret = 0;
	struct evfs_super_block sb;

	ret = ioctl(fd, FS_IOC_SUPER_GET, &sb);
	if (ret < 0) {
		perror("sbget");
		return 1;
	}

	printf("max extent size: %lu\n"
		"max file size: %lu\n"
		"page size: %lu\n"
		"root inode: %lu\n",
		sb.max_extent, sb.max_bytes,
		sb.page_size, sb.root_ino);
}

int main(int argc, char **argv)
{
	char *command;
	int (*command_fn)(int, int, char **);
	int fd;
	int err;

	if (argc < 3) {
		fprintf(stderr, "usage: evfs_runner <device> [ialloc|ifree| "
				"[args...]\n");
		return 1;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open device");
		return 1;
	}

	command = argv[2];
	if (!strcmp(command, "ialloc"))
		command_fn = &evfs_ialloc;
	else if (!strcmp(command, "ifree"))
		command_fn = &evfs_ifree;
	else if (!strcmp(command, "iget"))
		command_fn = &evfs_iget;
	else if (!strcmp(command, "iset"))
		command_fn = &evfs_iset;
	else if (!strcmp(command, "iread"))
		command_fn = &evfs_iread;
	else if (!strcmp(command, "imap"))
		command_fn = &evfs_imap;
	else if (!strcmp(command, "iunmap"))
		command_fn = &evfs_iunmap;
	else if (!strcmp(command, "eactive"))
		command_fn = &evfs_eactive;
	else if (!strcmp(command, "ealloc"))
		command_fn = &evfs_ealloc;
	else if (!strcmp(command, "efree"))
		command_fn = &evfs_efree;
	else if (!strcmp(command, "ewrite"))
		command_fn = &evfs_ewrite;
	else if (!strcmp(command, "eiter"))
		command_fn = &evfs_eiter;
	else if (!strcmp(command, "freespiter"))
		command_fn = &evfs_freespiter;
	else if (!strcmp(command, "inoiter"))
		command_fn = &evfs_inoiter;
	else if (!strcmp(command, "dadd"))
		command_fn = &evfs_dadd;
	else if (!strcmp(command, "drm"))
		command_fn = &evfs_drm;
	else if (!strcmp(command, "sbget"))
		command_fn = &evfs_sbget;
	else
		command_fn = NULL;

	if (!command_fn) {
		fprintf(stderr, "unknown command %s\n", command);
		err = 1;
	} else {
		err = command_fn(fd, argc - 3, argv + 3);
	}

	close(fd);
	return err;
}
