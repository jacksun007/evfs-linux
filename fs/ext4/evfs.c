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

int dirent_remove(struct file *filep, struct super_block *sb,
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

int dirent_add(struct file *filep, struct super_block *sb, unsigned long arg) {
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

long
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

long
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

long
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

	i.uid = i_uid_read(vfs_inode);
	i.gid = i_gid_read(vfs_inode);
	i.mode = cpu_to_le16(vfs_inode->i_mode);
	i.flags = cpu_to_le32(vfs_inode->i_flags);

	ext4_evfs_copy_timespec(&(i.atime), &(vfs_inode->i_atime));
	ext4_evfs_copy_timespec(&(i.ctime), &(vfs_inode->i_ctime));
	ext4_evfs_copy_timespec(&(i.mtime), &(vfs_inode->i_mtime));

	ext4_msg(sb, KERN_ERR,
			"no : %ld\n"
		 	"mode : %d\n"
			"uid : %d\n"
			"gid : %d\n"
			"refcount : %d\n",
		vfs_inode->i_ino,
		vfs_inode->i_mode,
		vfs_inode->i_uid.val,
		vfs_inode->i_gid.val,
		atomic_read(&vfs_inode->i_count));

	iput(vfs_inode);
	if (copy_to_user((struct evfs_inode __user *) arg, &i, sizeof(i)))
		return -EFAULT;
	return 0;
}

long
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

long
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
long
ext4_evfs_inode_unmap(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct evfs_imap evfs_i;
	struct inode *inode;
	long long partial_cluster = 0;
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
	fsync_bdev(sb->s_bdev);
	iput(inode);
	return err;
}

long
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

long
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

	ext4_lock_group(sb, group);

	mb_clear_bits(bitmap_bh->b_data, fex.fe_start, fex.fe_len);

	len = ext4_free_group_clusters(sb, gdp) + fex.fe_len;
	grp = ext4_get_group_info(sb, group);
	grp->bb_free += fex.fe_len;
	ext4_free_group_clusters_set(sb, gdp, len);
	ext4_block_bitmap_csum_set(sb, group, gdp, bitmap_bh);
	ext4_group_desc_csum_set(sb, group, gdp);

	ext4_unlock_group(sb, group);
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

/*
 * TODO: Currently does not allow the use of hints. This should be fixed
 */
long
ext4_evfs_extent_alloc(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct ext4_sb_info *sbi;
	struct evfs_extent_alloc_op ext_op;
	struct evfs_extent op;
	struct buffer_head *bitmap_bh = NULL;
	struct ext4_group_desc *gdp;
	struct buffer_head *gdp_bh;
	struct ext4_free_extent fex;
	struct ext4_group_info *grp = NULL;
	ext4_group_t group;
	ext4_grpblk_t off_block;
	ext4_fsblk_t block;
	handle_t *handle = NULL;
	int err = 0, len;

	if (copy_from_user(&ext_op, (struct evfs_extent_alloc_op __user *) arg,
				sizeof(ext_op)))
		return -EFAULT;

	sbi = EXT4_SB(sb);

	op = ext_op.extent;
	block = op.start;

	ext4_get_group_no_and_offset(sb, block, &group, &off_block);

	fex.fe_start = off_block;
	fex.fe_group = group;
	fex.fe_len = op.length;

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

	block = ext4_grp_offs_to_block(sb, &fex);

	/*
	 * TODO: Code taken from ext4 source code. I am not entirely sure
	 *       what this does at the moment and not even sure if this is
	 *       necessary.
	 */
	len = EXT4_C2B(sbi, fex.fe_len);
	if (!ext4_data_block_valid(sbi, block, len)) {
		ext4_error(sb, "Allocating blocks %llu-%llu which overlap "
				"fs metadata", block, block + len);
		/* File system mounted not to panic on error
		 * Fix the bitmap and return EFSCORRUPTED
		 * We leak some of the blocks here.
		 */
		ext4_lock_group(sb, fex.fe_group);
		ext4_set_bits(bitmap_bh->b_data, fex.fe_start,
					  fex.fe_len);
		ext4_unlock_group(sb, fex.fe_group);
		err = ext4_handle_dirty_metadata(handle, NULL, bitmap_bh);
		if (!err)
			err = -EFSCORRUPTED;
		goto out;
	}

	ext4_lock_group(sb, group);

	if (ext4_extent_check(fex, bitmap_bh, EVFS_ALL)) {
		ext4_error(sb, "Given range of extent is allocated already");
		ext4_unlock_group(sb, group);
		err = -ENOSPC;
		goto out;
	}

	ext4_set_bits(bitmap_bh->b_data, fex.fe_start, fex.fe_len);

	len = ext4_free_group_clusters(sb, gdp) - fex.fe_len;
	grp = ext4_get_group_info(sb, group);
	grp->bb_free -= fex.fe_len;
	ext4_free_group_clusters_set(sb, gdp, len);
	ext4_block_bitmap_csum_set(sb, fex.fe_group, gdp, bitmap_bh);
	ext4_group_desc_csum_set(sb, fex.fe_group, gdp);

	ext4_unlock_group(sb, group);
	percpu_counter_sub(&sbi->s_freeclusters_counter, fex.fe_len);

	/*
	 * Now reduce the dirty block count also. Should not go negative
	 */
	if (sbi->s_log_groups_per_flex) {
		ext4_group_t flex_group = ext4_flex_group(sbi, fex.fe_group);
		atomic64_sub(fex.fe_len, &sbi->s_flex_groups[flex_group].free_clusters);
	}

	err = ext4_handle_dirty_metadata(handle, NULL, bitmap_bh);
	if (err)
		goto out;
	err = ext4_handle_dirty_metadata(handle, NULL, gdp_bh);
	if (err)
		goto out;

	err = op.start;
out:
	brelse(bitmap_bh);
	return err;
}

long
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

long
ext4_evfs_sb_get(struct super_block *sb, unsigned long arg)
{
	struct evfs_super_block evfs_sb;

	evfs_sb.max_extent = EXT_INIT_MAX_LEN;
	evfs_sb.max_bytes = sb->s_maxbytes;
	evfs_sb.page_size = PAGE_SIZE;
	evfs_sb.root_ino = EXT4_ROOT_INO;

	if (copy_to_user((struct evfs_super_block __user *) arg, &evfs_sb,
				sizeof(struct evfs_super_block)))
		return -EFAULT;
	return 0;

}

long
ext4_evfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	unsigned long ino_nr;
	long err;

	switch (cmd) {
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
	case FS_IOC_INODE_GET:
		return ext4_evfs_inode_get(filp, sb, arg);
	case FS_IOC_INODE_READ:
		return ext4_evfs_inode_read(filp, sb, arg);
	case FS_IOC_INODE_MAP:
		return ext4_evfs_inode_map(filp, sb, arg);
	case FS_IOC_INODE_UNMAP:
		return ext4_evfs_inode_unmap(filp, sb, arg);
	case FS_IOC_EXTENT_ACTIVE:
		return ext4_evfs_extent_active(filp, sb, arg);
	case FS_IOC_EXTENT_ALLOC:
		return ext4_evfs_extent_alloc(filp, sb, arg);
	case FS_IOC_EXTENT_FREE:
		return ext4_evfs_extent_free(filp, sb, arg);
	case FS_IOC_EXTENT_ITERATE:
		return ext4_evfs_extent_iter(filp, sb, arg);
	case FS_IOC_SUPER_GET:
		return ext4_evfs_sb_get(sb, arg);
	}

	return -ENOTTY;
}

