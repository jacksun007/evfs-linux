/*
 * linux/fs/ext4/ioctl.c
 *
 * Copyright (C) 2018
 * Kuei Sun
 */
#include <linux/fs.h>
#include <linux/capability.h>
#include <linux/time.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/random.h>
#include <linux/dcache.h>
#include <linux/buffer_head.h>
#include <asm/uaccess.h>
#include "mballoc.h"
#include "ext4_jbd2.h"
#include "ext4.h"
#include "ext4_extents.h"
#include "extents_status.h"

/*
 * Checks whether the range of extent is marked allocated or not.
 *
 * Note that the code assume that fex.fe_group has already been locked
 * and bitmap_bh has been given write access by the caller.
 *
 * Returns 1 if the range of extents are active, and 0 if they are not.
 * Also uses evfs_query enum to indicate whether we should check for
 * all extents in given range to be active, or at least one of them.
 *
 * If valid type is not provided, -EFAULT will be returned.
 */
static inline int ext4_extent_check(struct ext4_free_extent fex,
		struct buffer_head *bitmap_bh, enum evfs_query type)
{
	int i = 0;
	if (type == EVFS_ANY) {
		for (i = 0; i < fex.fe_len; i++) {
			if (mb_test_bit(fex.fe_start + i, bitmap_bh->b_data))
				return 1;
		}
		return 0;
	} else if (type == EVFS_ALL) {
		for (i = 0; i < fex.fe_len; i++) {
			if (!mb_test_bit(fex.fe_start + i, bitmap_bh->b_data))
				return 0;
		}
		return 1;
	}
	return -EFAULT;
}

struct buffer_head *find_entry(struct inode *dir, const char *name,
		struct ext4_dir_entry_2 **de) {
	struct qstr name_qs = QSTR_INIT(name, strnlen(name, EVFS_MAX_NAME_LEN));
	return ext4_find_entry(dir, &name_qs, de, NULL);
}

static long
dirent_remove(struct file *filep, struct super_block *sb,
		unsigned long arg) {
	int retval;
	struct buffer_head *bh;
	struct inode *dir;
	struct ext4_dir_entry_2 *de;
	struct evfs_dirent_del_op remove_op;
	struct qstr name_qs;
	handle_t *handle = NULL;

	ext4_msg(sb, KERN_ERR, "in dirent_remove");
	retval = -EFAULT;
	if (copy_from_user(&remove_op, (struct evfs_dirent_remove_op __user *) arg,
				sizeof(remove_op)))
		goto out;

	ext4_msg(sb, KERN_ERR, "copied arg from userspace (dirent_remove)");
	dir = ext4_iget(sb, remove_op.dir_nr);
	if (IS_ERR(dir)) {
		retval = PTR_ERR(dir);
		goto out;
	}

	ext4_msg(sb, KERN_ERR, "got inode (dirent_remove)");
	bh = find_entry(dir, remove_op.name, &de);
	if (IS_ERR(bh)) {
		retval = PTR_ERR(bh);
		goto out;
	} else if (!bh) {
		retval = -ENOENT;
		goto out;
	}

	ext4_msg(sb, KERN_ERR, "got handle (dirent_remove)");
	handle = ext4_journal_start(dir, EXT4_HT_DIR,
		EXT4_DATA_TRANS_BLOCKS(sb));
	if (IS_ERR(handle)) {
		retval = PTR_ERR(handle);
		goto out;
	}

	/* Ensure this entry doesn't remain cached as existing in the dcache */
	name_qs.name = remove_op.name;
	name_qs.len = strnlen(remove_op.name, sizeof(remove_op.name));
	d_drop_entry_in_dir(dir, &name_qs);

	retval = ext4_delete_entry(handle, dir, de, bh);
	if (retval)
		goto out;

	ext4_msg(sb, KERN_ERR, "deleted entry (dirent_remove)");

out:
	if (handle)
		ext4_journal_stop(handle);

	return retval;
}

static long
dirent_add(struct file *filep, struct super_block *sb, unsigned long arg) {
	struct evfs_dirent_add_op add_op;
	struct inode *dir;
	struct inode *entry_inode;
	struct qstr name_qs;
	int err;
	handle_t *handle;

	ext4_msg(sb, KERN_ERR, "in dirent_add");

	if (copy_from_user(&add_op, (struct evfs_dirent_add_op __user *) arg,
				sizeof(add_op)))
		return -EFAULT;

	dir = ext4_iget(sb, add_op.dir_nr);
	if (IS_ERR(dir)) {
		ext4_msg(sb, KERN_ERR, "iget failed during efvs");
		return PTR_ERR(dir);
	}
	ext4_msg(sb, KERN_ERR, "Found inode of parent inode");

	entry_inode = ext4_iget(sb, add_op.ino_nr);
	if (IS_ERR(entry_inode)) {
		ext4_msg(sb, KERN_ERR, "Could not find inode to be pointed to"
					"by new entry");
		return PTR_ERR(entry_inode);
	}
	ext4_msg(sb, KERN_ERR, "Found inode to be pointed to by new entry");


	handle = ext4_journal_start(dir, EXT4_HT_DIR, EXT4_MAX_TRANS_DATA);
	if (IS_ERR(handle)) {
		ext4_msg(sb, KERN_ERR, "Could not acquire journal handle");
		return PTR_ERR(handle);
	}

	/* Ensure this entry doesn't remain cached as existing in the dcache */
	name_qs.name = add_op.name;
	name_qs.len = strnlen(add_op.name, sizeof(add_op.name));
	d_drop_entry_in_dir(dir, &name_qs);

	err = ext4_add_entry_under_inode(handle, dir, add_op.name, entry_inode);
	if (err) {
		ext4_msg(sb, KERN_ERR, "Could not add directory entry");
		return -EFAULT;
	}


	/* TODO(rhysre) remove this (this just keeps things consistent for the demo
	 * program) */
	ext4_inc_count(handle, entry_inode);
	err = ext4_mark_inode_dirty(handle, entry_inode);
	if (err) {
		ext4_msg(sb, KERN_ERR, "Could not mark inode dirty");
		return err;
	}

	if (handle)
		ext4_journal_stop(handle);

	iput(entry_inode);
	iput(dir);
	return 0;
}

void
ext4_evfs_copy_timeval(struct timespec *to, struct evfs_timeval *from)
{
	to->tv_sec = from->tv_sec;
	to->tv_nsec = from->tv_usec * 1000;
}

void
ext4_evfs_copy_timespec(struct evfs_timeval *to, struct timespec *from)
{
	to->tv_sec = from->tv_sec;
	to->tv_usec = from->tv_nsec / 1000;
}

static long
ext4_evfs_inode_free(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct inode *inode;
	unsigned long ino_nr;
	long err = 0;
	if (get_user(ino_nr, (unsigned long __user *) arg))
		return -EFAULT;

	ext4_msg(sb, KERN_ERR, "fetch inode %ld\n", ino_nr);
	inode = ext4_iget_normal(sb, ino_nr);
	if (IS_ERR(inode)) {
		ext4_msg(sb, KERN_ERR, "iget failed during evfs");
		return PTR_ERR(inode);
	}

	if (atomic_read(&inode->i_count) == 1)
		clear_nlink(inode);
	else
		err = -EBUSY;

	iput(inode);
	return err;
}

static long
ext4_evfs_inode_read(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct evfs_inode_read_op read_op;
	struct inode *inode;
	struct iovec iov;
	struct iov_iter iter;
	int ret = 0;

	if (copy_from_user(&read_op, (struct evfs_inode_read_op __user *) arg,
				sizeof(struct evfs_inode_read_op)))
		return -EFAULT;

	inode = ext4_iget_normal(sb, read_op.ino_nr);
	if (IS_ERR(inode)) {
		ext4_msg(sb, KERN_ERR, "evfs_inode_read: failed to find inode");
		return PTR_ERR(inode);
	}

	iov.iov_base = read_op.data;
	iov.iov_len = read_op.length;
	iov_iter_init(&iter, READ, &iov, 1, read_op.length);

	ret = evfs_page_read_iter(inode, (loff_t *)&read_op.ofs, &iter, 0,
				&find_get_page);
	if (ret < 0)
		return ret;

	iput(inode);
	return 0;
}

static long
ext4_evfs_inode_get(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct inode *vfs_inode;
	struct evfs_inode i;

	if (copy_from_user(&i, (struct evfs_inode __user *) arg, sizeof(i)))
		return -EFAULT;

	vfs_inode = ext4_iget_normal(sb, i.ino_nr);
	if (IS_ERR(vfs_inode)) {
		ext4_msg(sb, KERN_ERR, "iget failed during evfs");
		return PTR_ERR(vfs_inode);
	}

	vfs_to_evfs_inode(vfs_inode, &i);

	iput(vfs_inode);
	if (copy_to_user((struct evfs_inode __user *) arg, &i, sizeof(i)))
		return -EFAULT;
	return 0;
}

static long
ext4_evfs_inode_set(struct file *filp, struct super_block *sb,
					unsigned long arg)
{
	struct inode *inode;
	struct evfs_inode evfs_i;

	if (copy_from_user(&evfs_i, (struct evfs_inode __user *) arg, sizeof(evfs_i)))
		return -EFAULT;

	inode = ext4_iget_normal(sb, evfs_i.ino_nr);
	if (IS_ERR(inode)) {
		ext4_msg(sb, KERN_ERR, "Inode %lu not found", evfs_i.ino_nr);
		return PTR_ERR(inode);
	}

	evfs_to_vfs_inode(&evfs_i, inode);
	write_inode_now(inode, 1);
	// mark_inode_dirty(inode);
	iput(inode);

	return 0;
}

static long
ext4_evfs_inode_alloc(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct inode *new_inode;
	struct evfs_inode i;
	handle_t *handle;

	if (copy_from_user(&i, (struct evfs_inode __user *) arg, sizeof(i)))
		return -EFAULT;

	// TODO(tbrindus): the kernel has to be built with CONFIG_IMA=n to prevent
	// this routine from erroring out.
	new_inode = ext4_new_inode_start_handle1(
			d_inode(sb->s_root),
			S_IFREG, /* maybe pass ours here from the start */
			NULL,
			i.ino_nr /* goal */,
			NULL /* owner */,
			/* these params are from namei.c:ext4_tmpfile */
			EXT4_HT_DIR,
			EXT4_MAXQUOTAS_INIT_BLOCKS(sb) + 4 + EXT4_XATTR_TRANS_BLOCKS);

	handle = ext4_journal_current_handle();
	if (IS_ERR(new_inode)) {
		ext4_msg(sb, KERN_ERR, "couldn't create new inode");
		return PTR_ERR(new_inode);
	}

	new_inode->i_op = &ext4_file_inode_operations;
	new_inode->i_fop = &ext4_file_operations;
	ext4_set_aops(new_inode);

	i_uid_write(new_inode, i.uid);
	i_gid_write(new_inode, i.gid);
	new_inode->i_mode = le16_to_cpu(i.mode);
	new_inode->i_flags = le32_to_cpu(i.flags);

	ext4_evfs_copy_timeval(&(new_inode->i_atime), &(i.atime));
	ext4_evfs_copy_timeval(&(new_inode->i_ctime), &(i.ctime));
	ext4_evfs_copy_timeval(&(new_inode->i_mtime), &(i.mtime));
	// TODO(tbrindus): evfs has this field, but kernel doesn't
	// ext4_evfs_copy_timeval(&(new_inode->i_otime), &(i.otime));

	mark_inode_dirty(new_inode);
	unlock_new_inode(new_inode);

	// TODO(tbrindus): don't think we actually need this.
	ext4_handle_sync(handle);
	ext4_journal_stop(handle);

	// If user requested an arbitrary inode allocation, copy the real inode number
	// back into userspace.
	i.ino_nr = new_inode->i_ino;
	iput(new_inode);

	if (copy_to_user((struct evfs_inode __user *) arg, &i, sizeof(i)))
		return -EFAULT;
	return 0;
}

static long
ext4_evfs_inode_map(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct evfs_imap evfs_i;
	struct inode *inode;
	struct extent_status es;
	struct ext4_free_extent fex;
	struct buffer_head *bitmap_bh = NULL;
	struct ext4_map_blocks map;
	handle_t *handle = NULL;
	ext4_group_t group;
	ext4_grpblk_t off_block;
	int err = 0;

	if (copy_from_user(&evfs_i, (struct evfs_imap __user *) arg,
				sizeof(struct evfs_imap)))
		return -EFAULT;

	if (unlikely(evfs_i.length > INT_MAX)) {
		ext4_error(sb, "Extent size too large");
		return -EFSCORRUPTED;
	}
	if (unlikely(evfs_i.log_blkoff >= EXT_MAX_BLOCKS)) {
		ext4_error(sb, "Logical block number is too large");
		return -EFSCORRUPTED;
	}

	inode = ext4_iget_normal(sb, evfs_i.ino_nr);
	if (IS_ERR(inode)) {
		ext4_msg(sb, KERN_ERR, "iget failed during evfs");
		return PTR_ERR(inode);
	} else if (!S_ISREG(inode->i_mode)) {
		ext4_msg(sb, KERN_ERR, "evfs_inode_map: "
				"can only map extents to regular file");
		iput(inode);
		return -EINVAL;
	}

	ext4_get_group_no_and_offset(sb, evfs_i.phy_blkoff, &group, &off_block);

	fex.fe_group = group;
	fex.fe_start = off_block;
	fex.fe_len = evfs_i.length;

	bitmap_bh = ext4_read_block_bitmap(sb, group);
	if (IS_ERR(bitmap_bh)) {
		err = PTR_ERR(bitmap_bh);
		bitmap_bh = NULL;
		goto out;
	}

	err = ext4_journal_get_write_access(handle, bitmap_bh);
	if (err)
		goto out;

	ext4_lock_group(sb, fex.fe_group);
	if (!ext4_extent_check(fex, bitmap_bh, EVFS_ALL)) {
		ext4_error(sb, "Given disk area is not allocated");
		ext4_unlock_group(sb, fex.fe_group);
		err = -EINVAL;
		goto out;
	}

	if (ext4_es_lookup_extent(inode, evfs_i.log_blkoff, &es)) {
		ext4_error(sb, "Logical block address is already mapped");
		err = -EBUSY;
		goto out;
	}

	/* TODO: This should be removed later on when EVFS handles inode locking separately */
	down_write(&EXT4_I(inode)->i_data_sem);

	map.m_flags = EXT4_GET_BLOCKS_CREATE;
	map.m_lblk = evfs_i.log_blkoff;
	map.m_pblk = evfs_i.phy_blkoff;
	map.m_len = evfs_i.length;

	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		err = ext4_ext_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_EVFS_MAP);
	} else {
		// TODO:
		ext4_msg(sb, KERN_ERR, "Inode %ld is NOT extent based!"
			"EVFS operations currently not supported", inode->i_ino);
		err = -EINVAL;
	}

	inode->i_blocks += evfs_i.length;
	inode->i_size += evfs_i.length * PAGE_SIZE;

	/* TODO: Similar to down_write, remove when inode lock is implemented */
	up_write(&EXT4_I(inode)->i_data_sem);
	ext4_unlock_group(sb, fex.fe_group);
out:
	brelse(bitmap_bh);
	fsync_bdev(sb->s_bdev);
	iput(inode);
	return err;
}

/*
 * TODO: Currently no error when invalid request is received. Need to
 *       check and fix this.
 */
static long
ext4_evfs_inode_unmap(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct evfs_imap evfs_i;
	struct inode *inode;
	ext4_lblk_t start, end;
	int err = 0;

	if (copy_from_user(&evfs_i, (struct evfs_imap __user *) arg,
				sizeof(struct evfs_imap)))
		return -EFAULT;

	start = evfs_i.log_blkoff;
	end = start + evfs_i.length;

	inode = ext4_iget_normal(sb, evfs_i.ino_nr);
	if (IS_ERR(inode)) {
		ext4_error(sb, "iget failed during evfs");
		return PTR_ERR(inode);
	} else if (!S_ISREG(inode->i_mode)) {
		ext4_error(sb, "evfs_inode_map: "
				   "can only map extents to regular file");
		iput(inode);
		return -EINVAL;
	}

	if (evfs_i.flag & EVFS_IMAP_UNMAP_ONLY) {
		/* TODO: This should be removed later on when EVFS handles inode locking separately */
		down_write(&EXT4_I(inode)->i_data_sem);

		if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
			err = ext4_ext_unmap_space(inode, evfs_i.log_blkoff, evfs_i.log_blkoff + evfs_i.length);
		} else {
			// TODO:
			ext4_msg(sb, KERN_ERR, "Inode %ld is NOT extent based!"
				"EVFS operations currently not supported", inode->i_ino);
			err = -EINVAL;
		}

		/* TODO: Similar to down_write, remove when inode lock is implemented */
		up_write(&EXT4_I(inode)->i_data_sem);
	} else {
		err = ext4_punch_hole(inode, start * PAGE_SIZE, evfs_i.length * PAGE_SIZE);
	}

	fsync_bdev(sb->s_bdev);
	iput(inode);
	return err;
}

static long
ext4_evfs_inode_iter(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct evfs_iter_ops iter;
	struct __evfs_ino_iter_param param;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct inode *inode;
	struct ext4_group_desc *gdp;
	struct buffer_head *bh;
	ext4_group_t group, max_group = sbi->s_groups_count;
	unsigned long ino_offset;
	int ino_nr = sbi->s_first_ino;
	int ret = 0;

	iter.count = 0;

	if (copy_from_user(&iter, (struct evfs_iter_ops __user *) arg,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;

	if (iter.ino_nr > ino_nr)
		ino_nr = iter.ino_nr;

	ino_offset = (ino_nr - 1) % EXT4_INODES_PER_GROUP(sb);
	group = (ino_nr - 1) / EXT4_INODES_PER_GROUP(sb);

	for (; group < max_group; group++) {
		gdp = ext4_get_group_desc(sb, group, NULL);
		if (!gdp) {
			ext4_msg(sb, KERN_ERR, "group %d invalid", group);
			continue;
		}
		bh = sb_getblk(sb, ext4_inode_bitmap(sb, gdp));
		lock_buffer(bh);
		if (!buffer_uptodate(bh)) {
			ext4_msg(sb, KERN_ERR, "group %d buffer not up to date!", group);
			get_bh(bh);
			bh->b_end_io = end_buffer_read_sync;
			submit_bh(REQ_OP_READ, REQ_META | REQ_PRIO, bh);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh)) {
				ext4_msg(sb, KERN_ERR, "group %d buffer can't fetch!", group);
				unlock_buffer(bh);
				brelse(bh);
				continue;
			}
			set_buffer_uptodate(bh);
		}
		if (!ext4_inode_bitmap_csum_verify(sb, group, gdp, bh, EXT4_INODES_PER_GROUP(sb) / 8)) {
			ext4_msg(sb, KERN_ERR, "group %d bitmap failed to verify!", group);
			brelse(bh);
			unlock_buffer(bh);
			continue;
		}
		unlock_buffer(bh);
		for (; ino_offset < EXT4_INODES_PER_GROUP(sb); ino_offset++) {
			if (mb_test_bit(ino_offset, bh->b_data)) {
				ino_nr = (group * EXT4_INODES_PER_GROUP(sb)) + ino_offset + 1;
				inode = ext4_iget(sb, ino_nr);
				if (!inode || IS_ERR(inode) || inode->i_state & I_CLEAR)
					continue;
				param.ino_nr = ino_nr;
				vfs_to_evfs_inode(inode, &param.i);
				iput(inode);
				if (evfs_copy_param(&iter, &param,
						sizeof(struct __evfs_ino_iter_param))) {
					brelse(bh);
					ret = 1;
					goto out;
				}
			}
		}
		brelse(bh);
		ino_offset = 0;
	}

out:
	if (copy_to_user((struct evfs_iter_ops __user *) arg, &iter,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;
	return ret;
}

static long
ext4_evfs_extent_active(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct evfs_extent_query op;
	struct ext4_free_extent fex;
	struct buffer_head *bitmap_bh = NULL;
	ext4_group_t group;
	ext4_grpblk_t off_block;
	ext4_fsblk_t block;
	handle_t *handle = NULL;
	int err = 0;

	if (copy_from_user(&op, (struct evfs_extent_query __user *) arg,
				sizeof(struct evfs_extent_query)))
		return -EFAULT;

	block = op.extent.start;

	ext4_get_group_no_and_offset(sb, block, &group, &off_block);

	fex.fe_group = group;
	fex.fe_start = off_block;
	fex.fe_len = op.extent.length;

	bitmap_bh = ext4_read_block_bitmap(sb, group);
	if (IS_ERR(bitmap_bh)) {
		err = PTR_ERR(bitmap_bh);
		bitmap_bh = NULL;
		goto out;
	}

	err = ext4_journal_get_write_access(handle, bitmap_bh);
	if (err)
		goto out;

	ext4_lock_group(sb, fex.fe_group);
	err = ext4_extent_check(fex, bitmap_bh, op.query);
	ext4_unlock_group(sb, fex.fe_group);

out:
	brelse(bitmap_bh);
	return err;
}

static long
ext4_evfs_extent_free(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct evfs_extent op;
	struct buffer_head *bitmap_bh = NULL;
	struct ext4_group_desc *gdp;
	struct buffer_head *gdp_bh;
	struct ext4_free_extent fex;
	struct ext4_group_info *grp = NULL;
	struct ext4_buddy e4b;
	ext4_group_t group;
	ext4_grpblk_t off_block;
	ext4_fsblk_t block;
	handle_t *handle = NULL;
	int err = 0, len;

	if (copy_from_user(&op, (struct evfs_ext __user *) arg,
				sizeof(struct evfs_extent)))
		return -EFAULT;

	block = op.start;

	ext4_get_group_no_and_offset(sb, block, &group, &off_block);

	fex.fe_start = off_block;
	fex.fe_group = group;
	fex.fe_len = op.length;

	/*
	 * NOTE (kyokeun): This uses the older method of reading the disk bitmap
	 *                 to determine whether it has been allocated. This
	 *                 bypasses some of the implications made by the
	 *                 buddy cache and preallocation system which
	 *                 Ext4 uses. Like extent allocate, this should use
	 *                 buddy bitmap instead.
	 */
	bitmap_bh = ext4_read_block_bitmap(sb, group);
	if (IS_ERR(bitmap_bh)) {
		err = PTR_ERR(bitmap_bh);
		bitmap_bh = NULL;
		goto out;
	}

	err = ext4_journal_get_write_access(handle, bitmap_bh);
	if (err)
		goto out;

	gdp = ext4_get_group_desc(sb, group, &gdp_bh);
	if (!gdp)
		goto out;

	ext4_debug("using block group %u(%d)\n", group,
			   ext4_free_group_clusters(sb, gdp));

	BUFFER_TRACE(gdp_bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, gdp_bh);
	if (err)
		goto out;

	err = ext4_mb_load_buddy(sb, group, &e4b);
	if (err) {
		ext4_error(sb, "mb_load_buddy failed (%d)", err);
		goto out;
	}

	ext4_lock_group(sb, group);

	mb_free_blocks(NULL, &e4b, off_block, fex.fe_len);

	mb_clear_bits(bitmap_bh->b_data, fex.fe_start, fex.fe_len);

	len = ext4_free_group_clusters(sb, gdp) + fex.fe_len;
	grp = ext4_get_group_info(sb, group);
	grp->bb_free += fex.fe_len;
	ext4_free_group_clusters_set(sb, gdp, len);
	ext4_block_bitmap_csum_set(sb, group, gdp, bitmap_bh);
	ext4_group_desc_csum_set(sb, group, gdp);

	ext4_unlock_group(sb, group);
	ext4_mb_unload_buddy(&e4b);
	percpu_counter_add(&sbi->s_freeclusters_counter, fex.fe_len);

	if (sbi->s_log_groups_per_flex) {
		ext4_group_t flex_group = ext4_flex_group(sbi, group);
		atomic64_add(fex.fe_len, &sbi->s_flex_groups[flex_group].free_clusters);
	}

	err = ext4_handle_dirty_metadata(handle, NULL, bitmap_bh);
	if (err)
		goto out;
	err = ext4_handle_dirty_metadata(handle, NULL, gdp_bh);
	if (err)
		goto out;

	err = 0;
out:
	brelse(bitmap_bh);
	return err;
}

static long
ext4_evfs_extent_alloc(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct evfs_extent_alloc_op ext_op;
	struct evfs_extent op;
	struct ext4_allocation_context ac;
	handle_t *handle = NULL;
	ext4_group_t group, max_groups = ext4_get_groups_count(sb);
	ext4_grpblk_t off_block;
	int err = 0;

	if (copy_from_user(&ext_op, (struct evfs_extent_alloc_op __user *) arg,
				sizeof(ext_op)))
		return -EFAULT;
	op = ext_op.extent;

	ext4_get_group_no_and_offset(sb, op.start, &group, &off_block);

	if (max_groups < group) {
		ext4_error(sb, "Given physical address (%lu) out of range", op.start);
		return -EINVAL;
	}

	ac.ac_sb = sb;
	ac.ac_g_ex.fe_group = group;
	ac.ac_g_ex.fe_start = off_block;
	ac.ac_g_ex.fe_len = op.length;
	ac.ac_found = 0;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_flags = EXT4_MB_HINT_TRY_GOAL;
	ac.ac_inode = NULL;
	if (ext_op.flags & EVFS_EXTENT_ALLOC_FIXED) {
		ext4_msg(sb, KERN_ERR, "Hint goal only!");
		ac.ac_flags |= EXT4_MB_HINT_GOAL_ONLY;
	}

	err = ext4_mb_regular_allocator(&ac);
	if (err) {
		ext4_error(sb, "ext4_mb_find_by_goal ERROR");
		goto out;
	}

	if (ac.ac_status != AC_STATUS_FOUND) {
		ext4_msg(sb, KERN_ERR, "Failed to find space");
		goto out;
	}

	err = ext4_mb_mark_diskspace_used(&ac, handle, 0);
	if (err) {
		ext4_error(sb, "Failed while marking diskspace");
		ext4_discard_allocated_blocks(&ac);
		goto out;
	}

	err = group * EXT4_BLOCKS_PER_GROUP(sb) + ac.ac_b_ex.fe_start;

out:
	return err;

}

static long
ext4_evfs_extent_iter(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct evfs_iter_ops iter;
	struct __evfs_ext_iter_param param = { 0 };
	struct inode *vfs_inode;

	struct extent_status es;

	ext4_lblk_t last, end;
	int ret;

	if (copy_from_user(&iter, (struct evfs_iter_ops __user *) arg, sizeof(iter)))
		return -EFAULT;

	vfs_inode = ext4_iget_normal(sb, iter.ino_nr);
	if (IS_ERR(vfs_inode)) {
		ext4_msg(sb, KERN_ERR, "iget failed during evfs");
		return PTR_ERR(vfs_inode);
	}

	// start_from is implicitly >> blkbits
	last = iter.start_from;
	// TODO(tbrindus): i_sb == sb, can probably replace?
	end = i_size_read(vfs_inode) >> vfs_inode->i_sb->s_blocksize_bits;

	iter.count = 0;
	do {
		ret = ext4_get_next_extent(vfs_inode, last, end - last + 1, &es);
		if (ret == 0)
			break;

		if (ret < 0)
			goto err_next_extent;

		// ext4 has status flags in higher bits of pblk, so mask them out.
		param.phy_blkoff = es.es_pblk & ~ES_MASK;
		param.log_blkoff = es.es_lblk;
		param.length = es.es_len;

		if (evfs_copy_param(&iter, &param, sizeof(param))) {
			ret = 1;
			goto out;
		}

		last += es.es_len;
	} while (last <= end);

out:
	if (copy_to_user((struct evfs_iter_ops __user *) arg, &iter,
				sizeof(struct evfs_iter_ops)))
		ret = -EFAULT;
err_next_extent:
	iput(vfs_inode);
	return ret;
}

static long
ext4_evfs_freesp_iter(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct evfs_iter_ops iter;
	struct __evfs_fsp_iter_param param = { 0 };
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_group_info *grp;
	struct ext4_group_desc *gdp;
	struct buffer_head *bh;
	ext4_group_t group, max_groups = ext4_get_groups_count(sb);
	ext4_grpblk_t off_block;
	int err = 0, start_marked = 0;

	if (copy_from_user(&iter, (struct evfs_iter_ops __user *) arg,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;
	iter.count = 0;
	if (iter.start_from < le32_to_cpu(sbi->s_es->s_first_data_block))
		iter.start_from = le32_to_cpu(sbi->s_es->s_first_data_block);
	ext4_get_group_no_and_offset(sb, iter.start_from, &group, &off_block);
	if (max_groups < group) {
		ext4_msg(sb, KERN_ERR, "max group: %d, group: %d", max_groups, group);
		ext4_error(sb, "Givern physical addres (%lu) out of range",
				iter.start_from);
		return -EINVAL;
	}

	for (; group < max_groups; group++) {
		gdp = ext4_get_group_desc(sb, group, NULL);
		grp = ext4_get_group_info(sb, group);
		if (!gdp) {
			ext4_msg(sb, KERN_ERR, "group %d invalid", group);
			continue;
		}
		bh = sb_getblk(sb, ext4_block_bitmap(sb, gdp));
		if (unlikely(EXT4_MB_GRP_NEED_INIT(grp))) {
			if (ext4_mb_init_group(sb, group, GFP_NOFS))
				continue;
		}
		ext4_lock_group(sb, group);
		lock_buffer(bh);
		if (!buffer_uptodate(bh)) {
			ext4_msg(sb, KERN_ERR, "group %d buffer not up to date!", group);
			get_bh(bh);
			bh->b_end_io = end_buffer_read_sync;
			submit_bh(REQ_OP_READ, REQ_META | REQ_PRIO, bh);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh)) {
				ext4_msg(sb, KERN_ERR, "group %d buffer can't fetch!", group);
				goto cleanup;
			}
			set_buffer_uptodate(bh);
		}
		for (; off_block < EXT4_BLOCKS_PER_GROUP(sb); off_block++) {
			int is_set = mb_test_bit(off_block, bh->b_data);
			if (!is_set && !start_marked) {
				start_marked = 1;
				param.addr = (group * EXT4_BLOCKS_PER_GROUP(sb)) + off_block;
				param.length = 1;
			} else if (is_set && start_marked) {
				start_marked = 0;
				if (evfs_copy_param(&iter, &param,
							sizeof(struct __evfs_fsp_iter_param))) {
					err = 1;
					goto cleanup;
				}
			} else if (start_marked) {
				param.length++;
				if (param.length == INT_MAX) {
					start_marked = 0;
					if (evfs_copy_param(&iter, &param,
								sizeof(struct __evfs_fsp_iter_param))) {
						err = 1;
						goto cleanup;
					}
				}
			}
		}
		// Need to check for trailing freesp here.
		if (start_marked) {
			start_marked = 0;
			if (evfs_copy_param(&iter, &param,
						sizeof(struct __evfs_fsp_iter_param))) {
				err = 1;
				goto cleanup;
			}
		}
cleanup:
		unlock_buffer(bh);
		ext4_unlock_group(sb, group);
		brelse(bh);
		off_block = 0;
		if (err)
			goto out;
	}

out:
	if (copy_to_user((struct evfs_iter_ops __user *) arg, &iter,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;
	return err;
}

static long
ext4_evfs_extent_write(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_ext_write_op write_op;
	struct iovec iov;
	struct iov_iter iter;
	int err = 0;

	if (copy_from_user(&write_op, (struct evfs_ext_write_op __user *) arg,
					   sizeof(struct evfs_ext_write_op)))
		return -EFAULT;

	iov.iov_base = write_op.data;
	iov.iov_len = write_op.length;
	iov_iter_init(&iter, WRITE, &iov, 1, write_op.length);

	err = evfs_perform_write(sb, &iter, write_op.addr);
	if (iov.iov_len != err) {
		ext4_msg(sb, KERN_ERR, "evfs_extent_write: expected to write "
				"%lu bytes, but wrote %d bytes instead",
				write_op.length, err);
		return -EFAULT;
	}
	ext4_msg(sb, KERN_ERR, "err = %d", err);
	return 0;
}

static long
ext4_evfs_sb_get(struct super_block *sb, unsigned long arg)
{
	struct evfs_super_block evfs_sb;

	evfs_sb.block_count = EXT4_BLOCKS_PER_GROUP(sb) * ext4_get_groups_count(sb);
	evfs_sb.max_extent = EXT_INIT_MAX_LEN;
	evfs_sb.max_bytes = sb->s_maxbytes;
	evfs_sb.page_size = PAGE_SIZE;
	evfs_sb.root_ino = EXT4_ROOT_INO;

	if (copy_to_user((struct evfs_super_block __user *) arg, &evfs_sb,
				sizeof(struct evfs_super_block)))
		return -EFAULT;
	return 0;

}

static long
ext4_evfs_execute(struct evfs_atomic_action * aa, struct evfs_opentry * op)
{
	long err = -1;

	switch (op->code) {
		case EVFS_INODE_INFO:
			break;
		case EVFS_INODE_UPDATE:
			err = ext4_evfs_inode_set;
			break;
		default:
			panic("not implemented. should not get here\n");
	}
	return err;
}

struct evfs_atomic_op ext4_evfs_atomic_ops = {
	.execute = ext4_evfs_execute,
};

long
ext4_evfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	unsigned long ino_nr;
	long err;

	switch (cmd) {
	case FS_IOC_ATOMIC_ACTION:
		return evfs_run_atomic_action(sb, &ext4_evfs_atomic_ops, arg);
	case FS_IOC_INODE_LOCK:
		if (get_user(ino_nr, (unsigned long __user *) arg))
			return -EFAULT;
		inode = ext4_iget(sb, ino_nr);
		if (IS_ERR(inode)) {
			ext4_msg(sb, KERN_ERR, "iget failed during evfs");
			return PTR_ERR(inode);
		}

		err = mnt_want_write_file(filp);
		if (err)
			return err;
		inode_lock(inode);
		return 0;
	case FS_IOC_INODE_UNLOCK:
		if (get_user(ino_nr, (unsigned long __user *) arg))
			return -EFAULT;
		inode = ext4_iget(sb, ino_nr);
		if (IS_ERR(inode)) {
			ext4_msg(sb, KERN_ERR, "iget failed during evfs");
			return PTR_ERR(inode);
		}

		inode_unlock(inode);
		mnt_drop_write_file(filp);
		break;
	case FS_IOC_DIRENT_ADD:
		return dirent_add(filp, sb, arg);
	case FS_IOC_DIRENT_REMOVE:
		return dirent_remove(filp, sb, arg);
	case FS_IOC_INODE_ALLOC:
		return ext4_evfs_inode_alloc(filp, sb, arg);
	case FS_IOC_INODE_FREE:
		return ext4_evfs_inode_free(filp, sb, arg);
	case FS_IOC_EXTENT_WRITE:
		return ext4_evfs_extent_write(filp, sb, arg);
	case FS_IOC_INODE_GET:
		return ext4_evfs_inode_get(filp, sb, arg);
	case FS_IOC_INODE_SET:
		return ext4_evfs_inode_set(filp, sb, arg);
	case FS_IOC_INODE_READ:
		return ext4_evfs_inode_read(filp, sb, arg);
	case FS_IOC_INODE_MAP:
		return ext4_evfs_inode_map(filp, sb, arg);
	case FS_IOC_INODE_UNMAP:
		return ext4_evfs_inode_unmap(filp, sb, arg);
	case FS_IOC_INODE_ITERATE:
		return ext4_evfs_inode_iter(filp, sb, arg);
	case FS_IOC_EXTENT_ACTIVE:
		return ext4_evfs_extent_active(filp, sb, arg);
	case FS_IOC_EXTENT_ALLOC:
		return ext4_evfs_extent_alloc(filp, sb, arg);
	case FS_IOC_EXTENT_FREE:
		return ext4_evfs_extent_free(filp, sb, arg);
	case FS_IOC_EXTENT_ITERATE:
		return ext4_evfs_extent_iter(filp, sb, arg);
	case FS_IOC_FREESP_ITERATE:
		return ext4_evfs_freesp_iter(filp, sb, arg);
	case FS_IOC_SUPER_GET:
		return ext4_evfs_sb_get(sb, arg);
	}

	return -ENOTTY;
}

