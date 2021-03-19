#include <unistd.h>

#include "kernevfs.h"

#define PAGE_SIZE getpagesize()

static inline int inode_lock(int fd, uint64_t ino_nr)
{
	return ioctl(fd, FS_IOC_INODE_LOCK, &ino_nr);
}

static inline int inode_unlock(int fd, uint64_t ino_nr)
{
	return ioctl(fd, FS_IOC_INODE_UNLOCK, &ino_nr);
}

static inline int extent_alloc(int fd, uint64_t addr, uint64_t len, int flags)
{
	struct evfs_extent_alloc_op ext = { .flags = flags };
	ext.extent.start = addr;
	ext.extent.length = len;
	return ioctl(fd, FS_IOC_EXTENT_ALLOC, &ext);
}

static inline int extent_free(int fd, uint64_t addr, uint64_t len)
{
	struct evfs_extent ext = { .start = addr, .length = len };
	return ioctl(fd, FS_IOC_EXTENT_FREE, &ext);
}

static inline int extent_write(int fd, uint64_t addr, uint64_t len, char *data)
{
	struct evfs_ext_write_op write_op = { .addr = addr,
						.length = len,
						.data = data };
	return ioctl(fd, FS_IOC_EXTENT_WRITE, &write_op);
}

static inline int inode_map(int fd, uint64_t ino_nr, uint64_t log_blk_nr,
		uint64_t phy_blk_nr, uint64_t len)
{
	struct evfs_imap ext = { .ino_nr = ino_nr, .log_blkoff = log_blk_nr,
		.phy_blkoff = phy_blk_nr, .length = len };
	return ioctl(fd, FS_IOC_INODE_MAP, &ext);
}

static inline int inode_unmap(int fd, uint64_t ino_nr, uint64_t log_blk_nr,
		uint64_t len)
{
	struct evfs_imap ext = { .ino_nr = ino_nr, .log_blkoff = log_blk_nr,
		.length = len };
	return ioctl(fd, FS_IOC_INODE_UNMAP, &ext);
}

static inline int inode_read(int fd, uint64_t ino_nr, uint64_t ofs, char *data, uint64_t len)
{
	struct evfs_inode_read_op op = { .ino_nr = ino_nr,
		.ofs = ofs, .data = data, .length = len };
	return ioctl(fd, FS_IOC_INODE_READ, &op);
}

static inline int sb_get(int fd, struct evfs_super_block *sb)
{
	return ioctl(fd, FS_IOC_SUPER_GET, sb);
}

int inode_iterate(int fd, void *priv, long (* cb)(int fd, uint64_t ino_nr,
			struct evfs_inode *i, void *priv))
{
	struct __evfs_ino_iter_param *param;
	struct evfs_iter_ops iter = { .start_from = 0};
	int ret = 0, count = 0;

iterate:
	ret = ioctl(fd, FS_IOC_INODE_ITERATE, &iter);
	if (ret < 0) {
		perror("ioctl");
		return 1;
	}

	while (count < iter.count) {
		param = ((struct __evfs_ino_iter_param *) iter.buffer) + count;
		ret = cb(fd, param->ino_nr, &param->i, priv);

		/*
		 * Currently making an assumption that any non-zero ret value
		 * is an error. This is probably not an ideal behaviour.
		 */
		if (ret) {
			perror("inode iterate callback");
			printf("inode: %lu\n", param->ino_nr);
			return 1;
		}
		++count;
	}

	count = 0;
	iter.start_from = param->ino_nr + 1;
	if (ret)
		goto iterate;

	return 0;
}

int extent_iterate(int fd, uint64_t ino_nr, void *priv, long (* cb)(
			int fd, uint64_t log_blk_nr, uint64_t phy_blk_nr,
			uint64_t len, void *priv))
{
	struct __evfs_ext_iter_param *param;
	struct evfs_iter_ops iter = { .start_from = 0, .ino_nr = ino_nr };
	int ret = 0, count = 0;

iterate:
	ret = ioctl(fd, FS_IOC_EXTENT_ITERATE, &iter);
	if (ret < 0) {
		perror("ioctl");
		return 1;
	}

	while (count < iter.count) {
		param = ((struct __evfs_ext_iter_param *) iter.buffer) + count;
		ret = cb(fd, param->log_blkoff, param->phy_blkoff,
				param->length, priv);

		/*
		 * Currently making an assumption that any non-zero ret value
		 * is an error. This is probably not an ideal behaviour.
		 */
		if (ret) {
			perror("extent iterate callback");
			return 1;
		}
		++count;
	}

	count = 0;
	iter.start_from = param->log_blkoff + 1;
	if (ret)
		goto iterate;

	return 0;
}
