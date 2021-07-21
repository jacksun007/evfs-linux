/*
 * linux/fs/ext4/evfs.c
 *
 * Copyright (C) 2018
 *
 * Kyo-Keun Park
 * Kuei Sun
 */

#include <linux/evfs.h>
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
 * Also uses EVFS_ALL/EVFS_ANY to indicate whether we should check for
 * all extents in given range to be active, or at least one of them.
 *
 * If valid type is not provided, -EFAULT will be returned.
 */
static inline int ext4_extent_check(struct ext4_free_extent fex,
		struct buffer_head *bitmap_bh, int type)
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

#if 0
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
#endif

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
ext4_evfs_free_inode(struct super_block * sb, u64 ino_nr)
{
	struct inode * inode;
	long err = 0;

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

	return -ENOSYS;
	return err;
}

static long
ext4_evfs_inode_free(struct super_block *sb, void __user * arg)
{
	unsigned long ino_nr;
	if (get_user(ino_nr, (unsigned long __user *) arg))
		return -EFAULT;

	return ext4_evfs_free_inode(sb, ino_nr);
}

static long
ext4_evfs_inode_info(struct super_block *sb, void __user * arg)
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
ext4_evfs_inode_set(struct super_block * sb, void __user * arg)
{
	struct inode *inode;
	struct evfs_inode evfs_i;

	if (copy_from_user(&evfs_i, (struct evfs_inode __user *) arg, sizeof(evfs_i)))
		return -EFAULT;

	inode = ext4_iget_normal(sb, evfs_i.ino_nr);
	if (IS_ERR(inode)) {
		ext4_msg(sb, KERN_ERR, "Inode %llu not found", evfs_i.ino_nr);
		return PTR_ERR(inode);
	}

	evfs_to_vfs_inode(&evfs_i, inode);
	write_inode_now(inode, 1);
	iput(inode);

	return 0;
}

static long
ext4_evfs_inode_alloc(struct super_block * sb, void __user * arg)
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
ext4_evfs_imap_entry(struct inode * inode, struct evfs_imentry * entry)
{
	struct ext4_map_blocks map;
	handle_t *handle = NULL;
	long err = 0;

	map.m_flags = EXT4_GET_BLOCKS_CREATE;
	map.m_lblk = entry->log_addr;
	map.m_pblk = entry->phy_addr;
	map.m_len = entry->len;

	err = ext4_ext_map_blocks(handle, inode, &map, EXT4_GET_BLOCKS_EVFS_MAP);
	inode->i_blocks += entry->len;

	fsync_bdev(inode->i_sb->s_bdev);
	return err;
}

static long
ext4_evfs_inode_map(struct file * filp, void __user * arg)
{
	struct super_block *sb = file_inode(filp)->i_sb;
	struct evfs_imap_op op;
	struct evfs_imap *imap;
	struct inode *inode;
	long err;
	unsigned i;

	if (copy_from_user(&op, arg, sizeof(struct evfs_imap_op)) != 0)
		return -EFAULT;

	/* Check validity of inode before getting the imap */
	inode = ext4_iget_normal(sb, op.ino_nr);
	if (IS_ERR(inode)) {
		ext4_msg(sb, KERN_ERR, "iget failed during evfs");
		return -EINVAL;
	}
	else if (!S_ISREG(inode->i_mode)) {
		ext4_msg(sb, KERN_ERR, "evfs_inode_map: "
				"can only map/unmap extents from regular file");
		err = -EINVAL;
		goto clean_inode;
	}
	else if (ext4_has_inline_data(inode)) {
		/* TODO: Handle inline data by removing it */
		ext4_msg(sb, KERN_ERR, "evfs_inode_map: "
				"inode contains inline data");
		err = -ENOSYS;
		goto clean_inode;
	}
	else if (!ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		/* TODO: Handle inodes that are not extent based as well */
		ext4_msg(sb, KERN_ERR, "evfs_inode_map: "
			"inode %ld is not extent based. Currently not supported",
			inode->i_ino);
		err = -ENOSYS;
		goto clean_inode;
	}

	/* Get the new mapping requested */
	err = evfs_imap_from_user(&imap, op.imap);
	if (err < 0)
		return err;

	/*
	 * Unmap first before we map.
	 * Make sure to unmap *everything*, since FS bugs out if we
	 * try to map to logical address that has already been mapped.
	 */
	for (i = 0; i < imap->count; i++) {
		err = ext4_ext_unmap_space(inode, imap->entry[i].log_addr,
				imap->entry[i].log_addr + imap->entry[i].len);
		if (err < 0)
			goto clean_imap;
	}

	/* Map all entries now */
	for (i = 0; i < imap->count; i++) {
		struct evfs_extent extent;

		/* Skip non-mapping extents */
		if (!imap->entry[i].phy_addr)
			continue;

		err = ext4_evfs_imap_entry(inode, &imap->entry[i]);
		if (err < 0)
			goto sync_bdev;

		evfs_imap_to_extent(&extent, &imap->entry[i]);
		err = evfs_remove_my_extent(filp, &extent);
		if (err < 0)
			goto clean_imap;
	}

sync_bdev:
	fsync_bdev(sb->s_bdev);
clean_imap:
	kfree(imap);
clean_inode:
	iput(inode);
	return err;
}

static long
ext4_evfs_inode_iter(struct super_block * sb, void __user * arg)
{
	struct evfs_iter_ops iter;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct inode *inode;
	struct ext4_group_desc *gdp;
	struct buffer_head *bh;
	u64 param;
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
				iput(inode);
				param = ino_nr;
				if (evfs_copy_param(&iter, &param, sizeof(u64))) {
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
ext4_evfs_extent_active(struct super_block * sb, void __user * arg)
{
	struct evfs_extent_op op;
	struct ext4_free_extent fex;
	struct buffer_head *bitmap_bh = NULL;
	ext4_group_t group;
	ext4_grpblk_t off_block;
	ext4_fsblk_t block;
	handle_t *handle = NULL;
	int err = 0;

	if (copy_from_user(&op, arg, sizeof(struct evfs_extent_op)))
		return -EFAULT;

	block = op.extent.addr;

	ext4_get_group_no_and_offset(sb, block, &group, &off_block);

	fex.fe_group = group;
	fex.fe_start = off_block;
	fex.fe_len = op.extent.len;

	bitmap_bh = ext4_read_block_bitmap(sb, group);
	if (IS_ERR(bitmap_bh)) {
		err = PTR_ERR(bitmap_bh);
		bitmap_bh = NULL;
		goto out;
	}

	err = ext4_journal_get_write_access(handle, bitmap_bh);
	if (err)
		goto out;

	err = ext4_extent_check(fex, bitmap_bh, op.flags);

out:
	brelse(bitmap_bh);
	return err;
}

static long
__ext4_evfs_extent_free(struct super_block * sb, const struct evfs_extent * ext)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
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

	block = ext->addr;

	ext4_get_group_no_and_offset(sb, block, &group, &off_block);

	fex.fe_start = off_block;
	fex.fe_group = group;
	fex.fe_len = ext->len;

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

	BUFFER_TRACE(gdp_bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, gdp_bh);
	if (err)
		goto out;

	err = ext4_mb_load_buddy(sb, group, &e4b);
	if (err) {
		ext4_error(sb, "mb_load_buddy failed (%d)", err);
		goto out;
	}

	mb_free_blocks(NULL, &e4b, off_block, fex.fe_len);
	mb_clear_bits(bitmap_bh->b_data, fex.fe_start, fex.fe_len);

	len = ext4_free_group_clusters(sb, gdp) + fex.fe_len;
	grp = ext4_get_group_info(sb, group);
	grp->bb_free += fex.fe_len;
	ext4_free_group_clusters_set(sb, gdp, len);
	ext4_block_bitmap_csum_set(sb, group, gdp, bitmap_bh);
	ext4_group_desc_csum_set(sb, group, gdp);

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
ext4_evfs_free_extent(struct super_block * sb, const struct evfs_extent * ext)
{
	ext4_group_t group, start_group, end_group;
	ext4_grpblk_t off;

	ext4_get_group_no_and_offset(sb, ext->addr, &start_group, &off);
	ext4_get_group_no_and_offset(sb, ext->addr + ext->len, &end_group, &off);

	for (group = start_group; group <= end_group; group++)
		ext4_lock_group(sb, group);
	__ext4_evfs_extent_free(sb, ext);
	for (group = start_group; group <= end_group; group++)
		ext4_unlock_group(sb, group);

	return 0;
}

static long
ext4_evfs_extent_free(struct super_block * sb, void __user * arg)
{
	struct evfs_extent op;

	if (copy_from_user(&op, (struct evfs_ext __user *) arg,
				sizeof(struct evfs_extent)))
		return -EFAULT;

	return __ext4_evfs_extent_free(sb, &op);
}

static long
ext4_evfs_extent_alloc(struct file * filp, struct evfs_opentry * op)
{
	struct super_block * sb = filp->f_inode->i_sb;
	struct evfs_extent extent;
	struct ext4_allocation_context ac;
	void __user * arg = op->data;
	handle_t *handle = NULL;
	ext4_group_t group, max_groups = ext4_get_groups_count(sb);
	ext4_grpblk_t off_block;
	int err = 0;

	if (copy_from_user(&extent, (struct evfs_extent __user *) arg,
				sizeof(extent)))
		return -EFAULT;

	if (!extent.addr) {
		if (!op->lkb->object_id) {
			ext4_msg(sb, KERN_ERR, "Both extent address and lockable address is NULL");
			return -ENOMEM;
		}
		extent.addr = op->lkb->object_id;
	}

	ext4_get_group_no_and_offset(sb, extent.addr, &group, &off_block);

	if (max_groups < group) {
		ext4_error(sb, "Given physical address (%llu) out of range", extent.addr);
		return -EINVAL;
	}

	ac.ac_sb = sb;
	ac.ac_g_ex.fe_group = group;
	ac.ac_g_ex.fe_start = off_block;
	ac.ac_g_ex.fe_len = extent.len;
	ac.ac_found = 0;
	ac.ac_status = AC_STATUS_CONTINUE;
	ac.ac_flags = EXT4_MB_HINT_TRY_GOAL | EXT4_MB_EVFS;
	ac.ac_inode = NULL;

	/* TODO: Obsolete now? */
	/* if (ext_op.flags & EVFS_EXTENT_ALLOC_FIXED) { */
	/* 	ext4_msg(sb, KERN_ERR, "Hint goal only!"); */
	/* 	ac.ac_flags |= EXT4_MB_HINT_GOAL_ONLY; */
	/* } */

	err = ext4_mb_regular_allocator(&ac);
	if (err) {
		ext4_error(sb, "ext4_mb_find_by_goal ERROR");
		goto out;
	}

	if (ac.ac_status != AC_STATUS_FOUND) {
		ext4_msg(sb, KERN_ERR, "Failed to find space");
		goto out;
	}

	err = evfs_add_my_extent(filp, &extent);
	if (err < 0) {
		ext4_msg(sb, KERN_ERR, "Failed to add EVFS extent struct");
		ext4_discard_allocated_blocks(&ac);
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
ext4_evfs_extent_iter(struct super_block * sb, void __user * arg)
{
	struct evfs_iter_ops iter;
	struct evfs_extent param = { 0 };
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
				param.len = 1;
			} else if (is_set && start_marked) {
				start_marked = 0;
				if (evfs_copy_param(&iter, &param,
							sizeof(struct evfs_extent))) {
					err = 1;
					goto cleanup;
				}
			} else if (start_marked) {
				param.len++;
				if (param.len == INT_MAX) {
					start_marked = 0;
					if (evfs_copy_param(&iter, &param,
								sizeof(struct evfs_extent))) {
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
						sizeof(struct evfs_extent))) {
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
ext4_evfs_sb_get(struct super_block * sb, void __user * arg)
{
	struct evfs_super_block evfs_sb;

	evfs_sb.block_count = EXT4_BLOCKS_PER_GROUP(sb) * ext4_get_groups_count(sb);
	evfs_sb.max_extent_size = EXT_INIT_MAX_LEN;
	evfs_sb.max_bytes = sb->s_maxbytes;
	evfs_sb.block_size = PAGE_SIZE;
	evfs_sb.root_ino = EXT4_ROOT_INO;

	if (copy_to_user((struct evfs_super_block __user *) arg, &evfs_sb,
				sizeof(struct evfs_super_block)))
		return -EFAULT;
	return 0;

}

static long
ext4_evfs_imap_info(struct file *filp, struct evfs_imap_param __user *uparam)
{
	struct evfs_imap_param param;
	struct inode *inode = file_inode(filp), * request_inode;
	struct super_block *sb = inode->i_sb;

	if (copy_from_user(&param, uparam, sizeof(param)))
		return -EFAULT;

	request_inode = ext4_iget_normal(sb, param.ino_nr);
	if (IS_ERR(request_inode))
		return -ENOENT;

	return __ioctl_fiemap(request_inode, param.fiemap);

	return 0;
}

static long
ext4_evfs_inode_lock(struct super_block * sb, struct evfs_lockable * lkb)
{
	struct inode * inode = ext4_iget_normal(sb, lkb->object_id);
	if (IS_ERR(inode))
		return -ENOENT;

	/* NOTE: Write lock if exclusive, read lock otherwise */
	if (lkb->exclusive) {
		inode_lock(inode);
		down_write(&EXT4_I(inode)->i_data_sem);
		down_write(&EXT4_I(inode)->i_mmap_sem);
		printk("EXCLUSIVE LOCK\n");
	} else {
		inode_lock_shared(inode);
		down_read(&EXT4_I(inode)->i_data_sem);
		down_read(&EXT4_I(inode)->i_mmap_sem);
		printk("NONEXCLUSIVE LOCK\n");
	}

	iput(inode);
	return 0;
}

static long
ext4_evfs_ext_group_lock(struct super_block * sb, struct evfs_lockable * lkb)
{
	struct ext4_group_info * grp;
	struct ext4_buddy e4b;
	ext4_group_t group;
	ext4_grpblk_t offset;
	unsigned long addr = lkb->object_id, len = lkb->data;

	ext4_get_group_no_and_offset(sb, addr, &group, &offset);

	if (!addr) {
		for (group = 0; group < ext4_get_groups_count(sb); group++) {
			ext4_mb_load_buddy(sb, group, &e4b);
			grp = ext4_get_group_info(sb, group);
			ext4_mb_unload_buddy(&e4b);
			if (len <= grp->bb_free) {
				lkb->object_id = group * EXT4_BLOCKS_PER_GROUP(sb)
					+ grp->bb_first_free;
				goto lock;
			}
		}
		return -ENOMEM;
	}

	grp = ext4_get_group_info(sb, group);

	/* Ensure that the group is loaded first */
	ext4_mb_load_buddy(sb, group, &e4b);
	ext4_mb_unload_buddy(&e4b);

	/*
	 * TODO: Make this flexible later
	 */
	if (len > grp->bb_free) {
		return -ENOMEM;
	}

lock:
	ext4_lock_group(sb, group);

	return 0;
}

static long
ext4_evfs_ino_group_lock(struct super_block * sb, struct evfs_lockable * lkb)
{
	struct inode * inode = ext4_iget_normal(sb, lkb->object_id);
	struct ext4_group_info * grp;
	struct ext4_buddy e4b;
	/* TODO: Currently, assume that object_id maps to inode number */
	unsigned long ino = lkb->object_id;
	ext4_group_t group = ino / EXT4_INODES_PER_GROUP(sb);

	if (!inode)
		return -ENOSYS;
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	iput(inode);

	grp = ext4_get_group_info(sb, group);

	/* Ensure that the group is loaded first */
	ext4_mb_load_buddy(sb, group, &e4b);
	ext4_mb_unload_buddy(&e4b);

	ext4_lock_group(sb, group);

	return 0;
}

static void
ext4_evfs_inode_unlock(struct super_block * sb, struct evfs_lockable * lkb)
{
	struct inode * inode = ext4_iget_normal(sb, lkb->object_id);
	if (IS_ERR(inode)) {
		panic("trying to unlock inode %lu but it does not exist!\n",
			lkb->object_id);
		return;
	}

	if (lkb->exclusive) {
		inode_unlock(inode);
		up_write(&EXT4_I(inode)->i_data_sem);
		up_write(&EXT4_I(inode)->i_mmap_sem);
	} else {
		inode_unlock_shared(inode);
		up_read(&EXT4_I(inode)->i_data_sem);
		up_read(&EXT4_I(inode)->i_mmap_sem);
	}

	iput(inode);
}

static void
ext4_evfs_ext_group_unlock(struct super_block * sb, struct evfs_lockable * lkb)
{
	ext4_group_t group;
	ext4_grpblk_t offset;
	unsigned long addr = lkb->object_id;

	ext4_get_group_no_and_offset(sb, addr, &group, &offset);

	ext4_unlock_group(sb, group);
}

static void
ext4_evfs_ino_group_unlock(struct super_block * sb, struct evfs_lockable * lkb)
{
	unsigned long ino = lkb->object_id;
	ext4_group_t group = ino / EXT4_INODES_PER_GROUP(sb);

	ext4_unlock_group(sb, group);
}

static long
ext4_evfs_prepare(struct evfs_atomic_action * aa, struct evfs_opentry * op)
{
	long ret = 0;

	switch(op->code) {
	case EVFS_INODE_MAP:
		ret = evfs_prepare_inode_map(aa->filp, op->data);
		break;
	/* TODO: Do check for extent alloc/free/write as well */
	default:
		ret = 0;
	}

	return ret;
}

static long
ext4_evfs_lock(struct evfs_atomic_action * aa, struct evfs_lockable * lockable)
{
	long err = 0;

	switch (lockable->type) {
	case EVFS_TYPE_INODE:
		err = ext4_evfs_inode_lock(aa->sb, lockable);
		break;
	case EVFS_TYPE_SUPER:
		break;
	case EVFS_TYPE_EXTENT_GROUP:
		err = ext4_evfs_ext_group_lock(aa->sb, lockable);
		break;
	case EVFS_TYPE_INODE_GROUP:
		err = ext4_evfs_ino_group_lock(aa->sb, lockable);
		break;
	case EVFS_TYPE_EXTENT:
	case EVFS_TYPE_DIRENT:
	case EVFS_TYPE_METADATA:
		break;
	default:
		printk("evfs: cannot lock object type %u\n", lockable->type);
	}
	return err;
}

static void
ext4_evfs_unlock(struct evfs_atomic_action * aa, struct evfs_lockable * lockable)
{
	switch (lockable->type) {
	case EVFS_TYPE_INODE:
		ext4_evfs_inode_unlock(aa->sb, lockable);
		break;
	case EVFS_TYPE_SUPER:
		break;
	case EVFS_TYPE_EXTENT_GROUP:
		ext4_evfs_ext_group_unlock(aa->sb, lockable);
		break;
	case EVFS_TYPE_INODE_GROUP:
		ext4_evfs_ino_group_unlock(aa->sb, lockable);
		break;
	case EVFS_TYPE_EXTENT:
	case EVFS_TYPE_DIRENT:
	case EVFS_TYPE_METADATA:
		break;
	default:
		printk("evfs: cannot lock object type %u\n", lockable->type);
	}
}

static long
ext4_evfs_execute(struct evfs_atomic_action * aa, struct evfs_opentry * op)
{
	long err = -1;

	switch (op->code) {
	case EVFS_INODE_INFO:
		err = ext4_evfs_inode_info(aa->sb, op->data);
		break;
	case EVFS_SUPER_INFO:
		err = ext4_evfs_sb_get(aa->sb, op->data);
		break;
	case EVFS_EXTENT_ACTIVE:
		err = ext4_evfs_extent_active(aa->sb, op->data);
		break;
	case EVFS_DIRENT_INFO:
		break;
	case EVFS_EXTENT_READ:
		break;
	case EVFS_INODE_READ:
		err = evfs_inode_read(aa->sb, op->data, &find_get_page);
		break;
	case EVFS_INODE_UPDATE:
		err = ext4_evfs_inode_set(aa->sb, op->data);
		break;
	case EVFS_SUPER_UPDATE:
	case EVFS_DIRENT_UPDATE:
		break;
	case EVFS_EXTENT_ALLOC:
		err = ext4_evfs_extent_alloc(aa->filp, op);
		break;
	case EVFS_EXTENT_FREE:
		err = ext4_evfs_extent_free(aa->sb, op->data);
		break;
	case EVFS_INODE_ALLOC:
		err = ext4_evfs_inode_alloc(aa->sb, op->data);
		break;
	case EVFS_INODE_FREE:
		err = ext4_evfs_inode_free(aa->sb, op->data);
		break;
	case EVFS_EXTENT_WRITE:
		err = evfs_extent_write(aa->sb, op->data);
		break;
	case EVFS_INODE_WRITE:
		err = evfs_inode_write(aa->sb, op->data, &find_get_page);
		break;
	case EVFS_DIRENT_ADD:
	case EVFS_DIRENT_REMOVE:
	case EVFS_DIRENT_RENAME:
		err = -ENOSYS;
		break;
	case EVFS_INODE_MAP:
		err = ext4_evfs_inode_map(aa->filp, op->data);
		break;
	default:
		printk("evfs: unknown opcode %d\n", op->code);
		err = -ENOSYS;
	}
	return err;
}

struct evfs_atomic_op ext4_evfs_atomic_ops = {
	.prepare = ext4_evfs_prepare,
	.lock = ext4_evfs_lock,
	.unlock = ext4_evfs_unlock,
	.execute = ext4_evfs_execute,
};

struct evfs_op ext4_evfs_ops = {
	.free_extent = ext4_evfs_free_extent,
	.free_inode = ext4_evfs_free_inode,
};

long
ext4_evfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;

	switch (cmd) {
	case FS_IOC_ATOMIC_ACTION:
		return evfs_run_atomic_action(filp, &ext4_evfs_atomic_ops, (void *)arg);
	case FS_IOC_EVFS_OPEN:
		return evfs_open(filp, &ext4_evfs_ops);
	case FS_IOC_IMAP_INFO:
		return ext4_evfs_imap_info(filp, (void *) arg);
	case FS_IOC_INODE_ITERATE:
		return ext4_evfs_inode_iter(sb, (void *) arg);
	case FS_IOC_EXTENT_ITERATE:
		return ext4_evfs_extent_iter(sb, (void *) arg);
	}

	return -ENOTTY;
}

