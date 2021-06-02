#include "defrag-tx.h"

/*
 * Structs used specifically for defrag application. Not affected by the
 * eVFS API design decision.
 */

struct extent {
	uint64_t blk_nr;
	uint64_t length;
	struct extent *next;
};

struct cpy_ext_param {
	uint64_t ino_nr;
	uint64_t isize;
	struct evfs_super *sb;
	struct extent *new_ext;
	struct extent *old_ext;
};

/*
 * Given the linked-list of extents, return the extent struct for
 * current extent.
 */
struct extent *get_curr_ext(struct cpy_ext_param *param, int curr_ext)
{
	struct extent *head = param->new_ext;
	for (int count = 0; count != curr_ext & head; count++)
		head = head->next;
	return head;
}

/*
 * Safely frees the linked-list extent struct from memory
 */
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

long copy_extents(struct evfs *fs, uint64_t log_blk_nr, uint64_t phy_blk_nr,
		uint64_t length, void * priv)
{
	struct cpy_ext_param *param = (struct cpy_ext_param *)priv;
	struct extent *curr, *old = param->old_ext;
	struct evfs_super *sb = param->sb;
	uint64_t bytesize = length * sb->page_size;
	uint64_t ofs = lob_blk_nr * sb->page_size;
	uint64_t target_blk, ext_start;
	char *data;
	int curr_ext t= log_blk_nr / sb->max_extent;
	int ret = 0;

	data = malloc(bytesize);
	curr =  get_curr_ext(param, curr_ext);
	ext_start = log_blk_nr - (curr_ext * sb->max_extent);
	target_blk = param->blk_nr + ext_start;

	struct  evfs_tx *cpy_tx = evfs_new_tx(fs);
	long rid = evfs_tx_read(cpy_tx, EVFS_INODE, param->ino_nr);
	long cid = evfs_tx_compare(cpy_tx, EVFS_INT_EQ,
			EVFS_FIELD(rid, EVFS_I_SIZE), EVFS_INT(param->isize));
	evfs_tx_inode_read(cpy_tx, param->ino_nr, ofs, data, bytesize);
	evfs_tx_inode_unmap(cpy_tx, param->ino_nr, log_blk_nr, len);
	if (ext_start  + len > sb->max_extent) {
		uint64_t partial_len = sb->max_extent - ext_start;
		uint64_t partial_bytesize = partial_len * sb->page_size;

		evfs_tx_extent_write(cpy_tx, target_blk, partial_bytesize, data);
		bytesize -= partial_bytesize;
		data += partial_bytesize;
		target_blk = param->next->blk_nr;
	}
	evfs_tx_extent_write(cpy_tx,  target_blk, partial_bytesize, data);

	if ((ret = evfs_tx_commit(cpy_tx)) < 0)
		goto out;

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
	evfs_tx_free(cpy_tx);
	free(data);
	return ret;
}

long inode_callback(struct evfs * fs, uint64_t ino_nr, struct evfs_inode * i,
		void * priv)
{
	struct cpy_ext_param *param;
	struct extent *head, *prev, *tmp, *old;
	struct evfs_inode_property prop = i->prop;
	struct evfs_super *sb = (struct evfs_super *)priv;
	int blocksleft = prop.blockcount;
	int ret = 0;

	// Ignore inodes with inline/no data or non-regular inodes
	if (!(i->mode & S_IFREG) || !prop.bytesize || prop.inlined)
		return 0;

	param = malloc(sizeof(struct cpy_ext_param));
	param->ino_nr = ino_nr;
	param->isize = prop.bytesize;
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
		tmp->blk_nr = extent_alloc(fs, 0, tmp->length, 0);
		if (tmp->blk_nr < 0) // TODO: cleanup previously allocated extents
			goto out;
		tmp->next = NULL;
		blocksleft -= tmp->length;
	}  while (blocksleft);

	// TODO: Fill up extent iter struct properly
	if ((ret = extent_iterate(fs, NULL)) < 0)
		goto out;

	// Using minitransaction, map the newly allocated extent to inode
	struct evfs_tx * imap_tx = evfs_new_tx(fs);
	long rid = evfs_tx_read(imap_tx, EVFS_INODE, ino_nr);
	long cid = evfs_tx_compare(imap_tx, EVFS_INT_EQ,
			EVFS_FIELD(rid, EVFS_I_SIZE), EVFS_INT(prop.bytesize));
	long count = 0;
	tmp = head;
	while(!tmp) {
		evfs_tx_inode_map(imap_tx, ino_nr, count, tmp->blk_nr, tmp->length);
		count += tmp->length;
	}
	ret = evfs_tx_commit(imap_tx);
	if (ret < 0) {
		tmp = head;
		while (!tmp) {
			extent_free(fs, tmp->blk_nr, tmp->length);
			tmp = tmp->next;
		}
		goto out;
	}

	// Free inode's old extents
	old = param->old_ext;
	while(!old) {
		extent_free(fs, old->blk_nr, old->length);
		old = old->next;
	}

out:
	evfs_tx_free(imap_tx);
	free_param(param);
	return ret;
}

int main(int argc, char **argv)
{
	struct evfs_mount mnt;
	struct evfs *fs;
	struct evfs_super sb;

	if (argc != 2) {
		fprintf(stderr, "usage: defrag <device>\n");
		return 1;
	}

	mnt.name = strtoll(argv[1], NULL, 10);
	fs = evfs_open(&mnt);
	if (!fs) {
		perror("evfs_open");
		return 1;
	}

	super_make(fs, &sup);
	// TODO: fill inode iter properly
	inode_iterate(fs, NULL);
	fs_close(fs);

	return 0;
}
