#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <math.h>

#include "kernevfs.h"
#include "defrag.h"

struct extent {
	long blk_nr;
	long length;
	struct extent *next;
};

struct cpy_ext_param {
	uint64_t ino_nr;
	struct evfs_super_block *sb;
	struct extent *new_ext;
	struct extent *old_ext;
};

struct extent *get_curr_ext(struct cpy_ext_param *param, int curr_ext)
{
	struct extent *head = param->new_ext;
	for (int count = 0; count != curr_ext && head; count++)
		head = head->next;
	return head;
}

void free_ext_struct(struct extent *ext)
{
	struct extent *tmp;

	while(!ext) {
		tmp = ext->next;
		free(ext);
		ext = tmp;
	}
}

void free_param(struct cpy_ext_param *param)
{
	free_ext_struct(param->new_ext);
	free_ext_struct(param->old_ext);
	free(param);
}

long copy_extents(int fd, uint64_t log_blk_nr, uint64_t phy_blk_nr,
		uint64_t len, void *priv)
{
	struct cpy_ext_param *param = (struct cpy_ext_param *)priv;
	struct extent *curr, *old = param->old_ext;
	struct evfs_super_block *sb = param->sb;
	uint64_t bytesize = len * PAGE_SIZE, ofs = log_blk_nr * PAGE_SIZE;
	uint64_t target_blk, ext_start;
	char *data;
	int curr_ext = log_blk_nr / sb->max_extent;
	int ret = 0;

	data = malloc(bytesize);
	curr = get_curr_ext(param, curr_ext);
	ext_start = log_blk_nr - (curr_ext * sb->max_extent);
	target_blk = param->blk_nr + ext_start;

	ret = inode_read(fd, param->ino_nr, ofs, data, bytesize);
	if (ret < 0) {
		perror("copy_extents: inode_read");
		goto out;
	}

	ret = inode_unmap(fd, param->ino_nr, log_blk_nr, len);
	if (ret < 0) {
		perror("copy_extents: inode_unmap");
		goto out;
	}

	if (ext_start + len > sb->max_extent) {
		uint64_t partial_len = sb->max_extent - ext_start;
		uint64_t partial_bytesize = partial_len * PAGE_SIZE;

		ret = extent_write(fd, target_blk, partial_bytesize, data);
		if (ret < 0) {
			perror("copy_extents: extent_write");
			goto out;
		}

		bytesize -= partial_bytesize;
		data += partial_bytesize;
		target_blk = param->next->blk_nr;
	}

	ret = extent_write(fd, target_blk, bytesize, data);
	if (ret < 0)
		perror("copy_extents: extent_write");

	// Keep track of old extents to free later
	if (!old) {
		old = param->old_ext = malloc(sizeof(struct extent));
	} else {
		while(!old->next)
			old = old->next;
		old->next = malloc(sizeof(struct extent));
		old = old->next;
	}
	old->blk_nr = phy_blk_nr;
	old->length = len;
	old->next = NULL;

out:
	free(data);
	return ret;
}

long inode_callback(int fd, uint64_t ino_nr, struct evfs_inode *i, void *priv)
{
	struct cpy_ext_param *param;
	struct extent *head, *prev, *tmp, *old;
	struct evfs_super_block *sb = (struct evfs_super_block *)priv;
	struct evfs_inode_property prop = i->prop;
	int blocksleft = prop.blockcount;
	int ret = 0;

	printf("inode: %lu, size: %lu, blkcount: %lu\n", ino_nr, prop.bytesize,
			prop.blockcount);

	// Ignore inodes with inlined/no data or non-regular inodes
	if (!(i->mode & S_IFREG) || !prop.bytesize || prop.inlined)
		return ret;

	/* ret = inode_lock(fd, ino_nr); */
	/* if (ret) */
	/* 	return ret; */

	param = malloc(sizeof(struct cpy_ext_param));
	param->ino_nr = ino_nr;
	param->sb = sb;
	param->new_ext = tmp = malloc(sizeof(struct extent));
	param->old_ext = NULL;

	// Allocate new, bigger extents
	do {
		prev = tmp;
		tmp = malloc(sizeof(struct extent));

		if (!head)
			head = tmp;
		else
			prev->next = tmp;

		tmp->length = blocksleft > sb->max_extent
				? sb->max_extent : blocksleft;
		tmp->blk_nr = extent_alloc(fd, 0, tmp->length, 0);
		tmp->next = NULL;
		blocksleft -= tmp->length;
		printf("curr blk_nr: %u\n", tmp->blk_nr);
	} while (blocksleft);

	ret = extent_iterate(fd, ino_nr, head, &copy_extents);

	inode_map(fd, ino_nr, 0, head->blk_nr, prop.blockcount);

	/* inode_unlock(fd, ino_nr); */

	old = param->old_ext;
	while(!old) {
		extent_free(fd, old->blk_nr, old->length);
		old = old->next;
	}

	free_param(param);

	return ret;
}

int main(int argc, char **argv)
{
	struct evfs_super_block sb;
	int fd, err;

	if (argc != 2) {
		fprintf(stderr, "usage: defrag <device>\n");
		return 1;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open device");
		return 1;
	}

	sb_get(fd, &sb);
	inode_iterate(fd, &sb, &inode_callback);

	return 0;
}
