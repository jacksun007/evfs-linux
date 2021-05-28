/* 
 * fs/f2fs/evfs.c
 *
 * Implementation of the Evfs operations for F2FS.
 *
 * Copyright (c) 2021 University of Toronto
 *                    Kyo-Keun Park
 *                    Kuei (Jack) Sun
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/capability.h>
#include <linux/time.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/random.h>
#include <linux/evfs.h>
#include <linux/swap.h>
#include <asm/uaccess.h>
#include <linux/quotaops.h>
#include <linux/uio.h>

#include "xattr.h"
#include "f2fs.h"
#include "segment.h"
#include "node.h"
#include <trace/events/f2fs.h>

static inline void
__do_map_lock(struct f2fs_sb_info *sbi, int flag, bool lock)
{
	if (flag == F2FS_GET_BLOCK_PRE_AIO) {
		if (lock)
			down_read(&sbi->node_change);
		else
			up_read(&sbi->node_change);
	} else {
		if (lock)
			f2fs_lock_op(sbi);
		else
			f2fs_unlock_op(sbi);
	}
}

static struct f2fs_summary *
get_sum_entry(struct f2fs_sb_info *sbi, block_t blkaddr)
{
	unsigned int segno = GET_SEGNO(sbi, blkaddr);
	struct seg_entry *se = get_seg_entry(sbi, segno);
	struct curseg_info *curseg = CURSEG_I(sbi, se->type);
	struct f2fs_summary *sum = NULL;
	block_t blkoff = blkaddr - START_BLOCK(sbi, segno);

	if (segno == curseg->segno) {
		sum = curseg->sum_blk->entries + blkoff;
	} else {
		struct page *sum_page = get_meta_page(sbi,
						GET_SUM_BLOCK(sbi, segno));
		struct f2fs_summary_block *sum_block;

		unlock_page(sum_page);
		sum_block = (struct f2fs_summary_block *)page_address(sum_page);
		sum = sum_block->entries + blkoff;

		f2fs_put_page(sum_page, 0);
	}

	return sum;
}

/* Reference: set_node_addr from node.c */
static inline void
evfs_write_node_page(unsigned int nid, struct f2fs_io_info *fio, int type)
{
	struct f2fs_sb_info *sbi = fio->sbi;
	struct curseg_info *curseg = CURSEG_I(sbi, type);
	struct f2fs_summary sum;
	int err, prev_segno = curseg->segno, target_segno = GET_SEGNO(sbi, fio->new_blkaddr);

	/*
	 * We need to change the curseg in order to allocate node block.
	 * In order to make this as transparent as possible, switch back to
	 * old segno after we are done.
	 */
	if (prev_segno != target_segno) {
		curseg->next_segno = target_segno;
		change_curseg(sbi, type, true);
	}

	set_summary(&sum, nid, 0, 0);

reallocate:
	evfs_alloc_data_block(sbi, fio->page, fio->old_blkaddr,
						  &fio->new_blkaddr, &sum, type, fio, true);

	/* writeout dirty page into bdev */
	err = f2fs_submit_page_write(fio);
	if (err == -EAGAIN) {
		fio->old_blkaddr = fio->new_blkaddr;
		goto reallocate;
	}

	/* Restore curseg after we are done, if possible */
	if (prev_segno != curseg->segno && test_bit(prev_segno, FREE_I(sbi)->free_segmap)) {
		curseg->next_segno = prev_segno;
		change_curseg(sbi, type, true);
	}
}

static inline bool
is_valid_segment(int type, int is_nodeseg)
{
	return (IS_DATASEG(type) && !is_nodeseg) || (IS_NODESEG(type) && is_nodeseg);
}

long
f2fs_extent_check(struct f2fs_sb_info *sbi, block_t start,
		block_t length, enum evfs_query type)
{
	unsigned int segno = GET_SEGNO(sbi, start), offset, count;
	struct seg_entry *se = get_seg_entry(sbi, segno);

	/* Check whether the beginning of extent belongs in data segment */
	if (start < SEG0_BLKADDR(sbi) || !IS_DATASEG(se->type))
		return -EFAULT;

	if (type == EVFS_ANY) {
		for (count = 0; count < length; count++) {
			segno = GET_SEGNO(sbi, start + count);
			se = get_seg_entry(sbi, segno);
			offset = GET_BLKOFF_FROM_SEG0(sbi, start + count);
			if (f2fs_test_bit(offset, se->cur_valid_map))
				return 1;
		}
		return 0;
	} else if (type == EVFS_ALL) {
		for (count = 0; count < length; count++) {
			segno = GET_SEGNO(sbi, start + count);
			se = get_seg_entry(sbi, segno);
			offset = GET_BLKOFF_FROM_SEG0(sbi, start + count);
			if (!f2fs_test_bit(offset, se->cur_valid_map))
				return 0;
		}
		return 1;
	}
	return -EFAULT;
}

/*
 * Look for a valid segment that can be used as next curseg that is
 * smaller than end_seg
 * If usable segment is found, modify the curseg->next_segno. Otherwise,
 * do not modify anything.
 */
static bool
find_next_curseg(struct f2fs_sb_info *sbi, struct curseg_info *curseg,
				 int type, unsigned short end_seg)
{
	unsigned short segno, contander = 0;
	block_t freesp = SEGMENT_SIZE(sbi);
	bool found = false;
	struct seg_entry *se;

	for (segno = 0; segno < end_seg; segno++) {
		se = get_seg_entry(sbi, segno);
		if (se->type == type && se->valid_blocks < freesp) {
			freesp = se->valid_blocks;
			contander = segno;
			found = true;
		}
	}

	if (found)
		curseg->next_segno = contander;

	return found;
}

/*
 * Called by f2fs_evfs_map_blocks() to add summary pages to preallocated
 * blocks. Then, updated the inode size accordingly.
 * Derived from __allocate_data_block() from fs/f2fs/data.c
 */
static int
__allocate_data_block(struct dnode_of_data *dn, block_t target_blkaddr)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	struct node_info ni;
	pgoff_t fofs;
	blkcnt_t count = 1;
	int ret;

	if (unlikely(is_inode_flag_set(dn->inode, FI_NO_ALLOC)))
		return -EPERM;

	dn->data_blkaddr = datablock_addr(dn->node_page, dn->ofs_in_node);
	if (dn->data_blkaddr == NEW_ADDR)
		goto alloc;

	if (unlikely((ret = inc_valid_block_count(sbi, dn->inode, &count))))
		return ret;

alloc:
	f2fs_replace_block(sbi, dn, NULL_ADDR, target_blkaddr,
			ni.version, true, false);

	/* update i_size */
	fofs = start_bidx_of_node(ofs_of_node(dn->node_page), dn->inode) +
		dn->ofs_in_node;
	if (i_size_read(dn->inode) < ((loff_t)(fofs + 1) << PAGE_SHIFT))
		f2fs_i_size_write(dn->inode,
				((loff_t)(fofs + 1) << PAGE_SHIFT));

	return 0;
}

/*
 * f2fs_evfs_map_blocks() is a lighter reimplementation of f2fs_map_blocks()
 * from fs/f2fs/data.c. It is used to allocate node pages and map them to
 * preallocated physical blocks.
 *
 * It is a lighter implementation, in a sense that only block allocation
 * related codes are ported from original f2fs_map_blocks().
 */
static int
f2fs_evfs_map_blocks(struct inode *inode, struct f2fs_map_blocks *map)
{
	unsigned int maxblocks = map->m_len;
	struct dnode_of_data dn;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	pgoff_t pgofs, end_offset, end;
	int ret = 0, ofs = 1;
	unsigned int ofs_in_node, last_ofs_in_node;
	block_t blkaddr, target_start = map->m_pblk, target_blkaddr, count = 0;

	if (!maxblocks)
		return 0;

	map->m_len = 0;
	map->m_flags = 0;

	/* it only supports block size == page size */
	pgofs = (pgoff_t)map->m_lblk;
	end = pgofs + maxblocks;

next_dnode:
	__do_map_lock(sbi, F2FS_MAP_NEW, true);

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	ret = get_dnode_of_data(&dn, pgofs, ALLOC_NODE);
	if (ret == -ENOENT) {
		ret = 0;
		if (map->m_next_pgofs)
			*map->m_next_pgofs =
				get_next_page_offset(&dn, pgofs);
		goto unlock_out;
	}

	last_ofs_in_node = ofs_in_node = dn.ofs_in_node;
	end_offset = ADDRS_PER_PAGE(dn.node_page, inode);

next_block:
	blkaddr = datablock_addr(dn.node_page, dn.ofs_in_node);
	target_blkaddr = target_start + count;

	if (blkaddr == NEW_ADDR || blkaddr == NULL_ADDR) {
		if (unlikely(f2fs_cp_error(sbi))) {
			ret = -EIO;
			goto sync_out;
		}
		ret = __allocate_data_block(&dn, target_blkaddr);
		if (!ret)
			set_inode_flag(inode, FI_APPEND_WRITE);
		else
			goto sync_out;
		map->m_flags |= F2FS_MAP_NEW;
		blkaddr = dn.data_blkaddr;
	}

	if (map->m_len == 0) {
		if (blkaddr == NEW_ADDR)
			map->m_flags |= F2FS_MAP_UNWRITTEN;
		map->m_flags |= F2FS_MAP_MAPPED;
		map->m_pblk = blkaddr;
		map->m_len = 1;
	} else if ((map->m_pblk != NEW_ADDR &&
				blkaddr == (map->m_pblk + ofs)) ||
			(map->m_pblk == NEW_ADDR && blkaddr == NEW_ADDR)) {
		ofs++;
		map->m_len++;
	} else {
		goto sync_out;
	}

	dn.ofs_in_node++;
	pgofs++;
	count++;

	if (pgofs >= end)
		goto sync_out;
	else if (dn.ofs_in_node < end_offset)
		goto next_block;

	f2fs_put_dnode(&dn);

	__do_map_lock(sbi, F2FS_MAP_NEW, false);
	f2fs_balance_fs(sbi, dn.node_changed);

	goto next_dnode;

sync_out:
	f2fs_put_dnode(&dn);
unlock_out:
	__do_map_lock(sbi, F2FS_MAP_NEW, false);
	f2fs_balance_fs(sbi, dn.node_changed);
	return ret;
}


static long
__metadata_move(struct f2fs_sb_info *sbi, block_t from_addr,
			block_t to_addr)
{
	struct f2fs_summary *sum;
	struct seg_entry *from_se, *to_se;
	struct node_info ni;
	struct page *page;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 1,
		.for_reclaim = 0
	};
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.type = NODE,
		.op = REQ_OP_WRITE,
		.op_flags = wbc_to_write_flags(&wbc),
		.page = NULL,
		.encrypted_page = NULL,
		.submitted = false,
	};
	nid_t nid;
	unsigned int from_segno, to_segno;
	int ret = 0;

	from_segno = GET_SEGNO(sbi, from_addr);
	from_se = get_seg_entry(sbi, from_segno);
	to_segno = GET_SEGNO(sbi, to_addr);
	to_se = get_seg_entry(sbi, to_segno);

	/* Make sure that we are working with node segment */
	if (!IS_NODESEG(from_se->type) || !IS_NODESEG(to_se->type)) {
		f2fs_msg(sbi->sb, KERN_ERR, "Original or destination address is not "
				 "a part node segment");
		return -EFAULT;
	}

	if (!f2fs_test_bit(GET_BLKOFF_FROM_SEG0(sbi, to_addr), to_se->cur_valid_map)) {
		f2fs_msg(sbi->sb, KERN_ERR, "Destination address is not allocated");
		return -EINVAL; // TODO: Find a better error code
	}

	sum = get_sum_entry(sbi, from_addr);
	nid = le32_to_cpu(sum->nid);
	page = get_node_page(sbi, nid);
	if (!page) {
		f2fs_msg(sbi->sb, KERN_ERR, "eVFS metadata move: cannot retrieve node page");
		return -EFAULT;
	}

	/* Reference: fs/f2fs/node.c:__write_node_page */
	if (wbc.for_reclaim) {
		if (!down_read_trylock(&sbi->node_write)) {
			ret = -EFAULT;
			unlock_page(page);
			goto out;
		}
	} else {
		down_read(&sbi->node_write);
	}

	get_node_info(sbi, nid, &ni);

	fio.page = page;
	fio.old_blkaddr = ni.blk_addr;
	fio.new_blkaddr = to_addr;

	set_page_dirty(page);
	set_node_addr(sbi, &ni, to_addr, is_fsync_dnode(page));
	set_page_writeback(page);
	evfs_write_node_page(nid, &fio, to_se->type);
	dec_page_count(sbi, F2FS_DIRTY_NODES);
	up_read(&sbi->node_write);
	unlock_page(page);

	invalidate_blocks(sbi, from_addr);

	f2fs_wait_on_page_writeback(page, NODE, true);
	if (wbc.for_reclaim)
		f2fs_submit_merged_write_cond(sbi, page->mapping->host, 0,
									  page->index, NODE);

out:
	return ret;
}

long
f2fs_evfs_extent_alloc(struct file *filp,
		struct super_block *sb, unsigned long arg)
{
	struct evfs_extent evfs_ext;
	struct evfs_extent_alloc_op ext_op;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int ret = 0, use_hint = 0;

	if (copy_from_user(&ext_op, (struct evfs_extent_alloc_op __user *) arg,
				sizeof(struct evfs_extent_alloc_op)))
		return -EFAULT;

	evfs_ext = ext_op.extent;

	if (ext_op.flags == EVFS_EXTENT_ALLOC_FIXED) {
		ret = f2fs_extent_check(sbi, evfs_ext.start,
					evfs_ext.length, EVFS_ANY);
		if (ret < 0) {
			ret = -ENOSPC;
			goto out;
		}
		use_hint = 1;
	}

	ret = reserve_extents(sbi, evfs_ext.start, evfs_ext.length, use_hint);

out:
	f2fs_balance_fs(sbi, true);
	return ret;
}

long
f2fs_evfs_extent_free(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_extent evfs_ext;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	block_t blk_addr, start, end;
	int ret = 0;

	if (copy_from_user(&evfs_ext, (struct evfs_ext __user *) arg,
				sizeof(struct evfs_extent)))
		return -EFAULT;

	ret = f2fs_extent_check(sbi, evfs_ext.start, evfs_ext.length, EVFS_ANY);
	if (ret < 0) {
		f2fs_msg(sb, KERN_ERR, "f2fs_evfs_extent_free: "
				"invalid extent range");
		return ret;
	} else if (!ret) {
		f2fs_msg(sb, KERN_ERR, "f2fs_evfs_extent_free: "
				"given range is already free");
		return -ENOMEM;
	}

	start = evfs_ext.start;
	end = start + evfs_ext.length;
	for (blk_addr = start; blk_addr < end; blk_addr++) {
		invalidate_blocks(sbi, blk_addr);
	}

	return 0;
}

long
f2fs_evfs_extent_active(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_extent_query evfs_query;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int ret;

	if (copy_from_user(&evfs_query, (struct evfs_extent_query __user *) arg,
				sizeof (struct evfs_extent_query)))
		return -EFAULT;

	ret = f2fs_extent_check(sbi, evfs_query.extent.start,
			evfs_query.extent.length, evfs_query.query);

	if (ret < 0) {
		f2fs_msg(sb, KERN_ERR, "f2fs_evfs_extent_active: "
				"invalid query request");
	} else if (ret) {
		f2fs_msg(sb, KERN_ERR, "f2fs_evfs_extent_active: "
				"selected block is active");
	} else {
		f2fs_msg(sb, KERN_ERR, "f2fs_evfs_extent_active: "
				"selected block is NOT active");
	}
	return ret;
}

long
f2fs_evfs_extent_write(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_ext_write_op write_op;
	struct iovec iov;
	struct iov_iter iter;
	int ret = 0;

	if (copy_from_user(&write_op, (struct evfs_ext_write_op __user *) arg,
				sizeof(struct evfs_ext_write_op)))
		return -EFAULT;

	iov.iov_base = write_op.data;
	iov.iov_len = write_op.length;
	iov_iter_init(&iter, WRITE, &iov, 1, write_op.length);

	ret = evfs_perform_write(sb, &iter, write_op.addr);
	if (iov.iov_len != ret) {
		f2fs_msg(sb, KERN_ERR, "evfs_extent_write: expected to write "
				"%lu bytes, but wrote %d bytes instead",
				write_op.length, ret);
		return -EFAULT;
	}

	return 0;
}

long
f2fs_evfs_extent_iter(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_iter_ops iter;
	struct __evfs_ext_iter_param param;
	struct dnode_of_data dn;
	struct inode *inode;
	block_t blk_addr;
	pgoff_t offset = 0, end_offset;
	int ret = 0;

	if (copy_from_user(&iter, (struct evfs_iter_ops __user *) arg,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;

	inode = f2fs_iget(sb, iter.ino_nr);
	if (IS_ERR(inode)) {
		f2fs_msg(sb, KERN_ERR, "evfs_extent_iter: given inode "
				"does not exist");
		return PTR_ERR(inode);
	}

	/* Make sure everything is committed first */
	commit_inmem_pages(inode);

	if (f2fs_has_inline_data(inode)) {
		f2fs_msg(sb, KERN_ERR, "evfs_extent_iteration: "
				"given inode contains inlined data");
		ret = -EINVAL;
		goto out;
	}

	iter.count = 0;
	offset = iter.start_from;

	/* Block size == page size */
	end_offset = i_size_read(inode) >> PAGE_SHIFT;
	param.phy_blkoff = param.log_blkoff = param.length = 0;

	for (; offset <= end_offset; offset++) {
		set_new_dnode(&dn, inode, NULL, NULL, 0);
		ret = get_dnode_of_data(&dn, offset, LOOKUP_NODE);

		/*
		 * If there are no entry for given logical block,
		 * it might just be that there is an "hole" in the
		 * file. So, just continue
		 */
		if (ret == -ENOENT)
			continue;

		blk_addr = datablock_addr(dn.node_page, dn.ofs_in_node);

		if (!param.phy_blkoff) {
			param.phy_blkoff = blk_addr;
			param.log_blkoff = offset;
			param.length = 1;
		} else if (param.phy_blkoff != blk_addr - param.length) {
			if (evfs_copy_param(&iter, &param,
					sizeof(struct __evfs_ext_iter_param))) {
				ret = 1;
				goto out;
			}
			param.phy_blkoff = blk_addr;
			param.log_blkoff = offset;
			param.length = 1;
		} else {
			++param.length;
		}

		f2fs_put_dnode(&dn);
	}

	/* Read the last extent as well */
	if (param.phy_blkoff) {
		evfs_copy_param(&iter, &param,
				sizeof(struct __evfs_ext_iter_param));
	}

out:
	if (copy_to_user((struct evfs_iter_ops __user *) arg, &iter,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;
	iput(inode);
	return ret;
}

long
f2fs_evfs_freesp_iter(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_iter_ops iter;
	struct __evfs_fsp_iter_param param;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct seg_entry *se;
	unsigned int max_segno = MAIN_SEGS(sbi), segno = 0;
	block_t blkoff, max_blkoff = sbi->blocks_per_seg;
	int ret = 0;

	if (copy_from_user(&iter, (struct evfs_iter_ops __user *) arg,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;

	if (iter.start_from < MAIN_BLKADDR(sbi))
		iter.start_from = MAIN_BLKADDR(sbi);
	segno = GET_SEGNO(sbi, iter.start_from);
	blkoff = GET_BLKOFF_FROM_SEG0(sbi, iter.start_from);
	iter.count = 0;

	/* Iterate through all segments */
	for (; segno < max_segno; segno++) {
		se = get_seg_entry(sbi, segno);
		param.length = 0;
		param.addr = 0;

		/* We only care about data blocks */
		if (!IS_DATASEG(se->type))
			continue;

		/* Iterate through all of the blocks from given segment */
		/* TODO: Currently a single extent cannot be larger than
		 *       single segment. Maybe change this? */
		for(; blkoff < max_blkoff; blkoff++) {
			if (!(f2fs_test_bit(blkoff, se->cur_valid_map))) {
				if (!param.length) {
					param.addr = START_BLOCK(sbi, segno)
							+ blkoff;
					param.length = 1;
				} else {
					++param.length;
				}
			} else if (param.length) {
				if (evfs_copy_param(&iter, &param,
					sizeof(struct __evfs_fsp_iter_param))) {
					ret = (param.addr + param.length
							>= MAX_BLKADDR(sbi))
						? 0 : 1;
					goto out;
				}
				param.addr = 0;
				param.length = 0;
			}
		}

		if (param.length) {
			if (evfs_copy_param(&iter, &param,
				sizeof(struct __evfs_fsp_iter_param))) {
				ret = (param.addr + param.length
						>= MAX_BLKADDR(sbi)) ? 0 : 1;
				goto out;
			}
			param.addr = 0;
			param.length = 0;
		}
		blkoff = 0;
	}

out:
	if (copy_to_user((struct evfs_iter_ops __user *) arg, &iter,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;
	return ret;
}

long
f2fs_evfs_inode_map(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct evfs_imap evfs_i;
	struct inode *inode;
	struct f2fs_map_blocks map = { .m_next_pgofs = NULL };
	int ret;

	if (copy_from_user(&evfs_i, (struct evfs_imap __user *) arg,
				sizeof(struct evfs_imap)))
		return -EFAULT;

	inode = f2fs_iget(sb, evfs_i.ino_nr);
	if (IS_ERR(inode)) {
		f2fs_msg(sb, KERN_ERR, "iget failed during evfs");
		return PTR_ERR(inode);
	} else if (!S_ISREG(inode->i_mode)) {
		f2fs_msg(sb, KERN_ERR, "evfs_inode_map: "
				"can only map extents to regular file");
		iput(inode);
		return -EINVAL;
	}

	if (!f2fs_extent_check(sbi, evfs_i.phy_blkoff,
				evfs_i.length, EVFS_ALL)) {
		f2fs_msg(sb, KERN_ERR, "evfs_inode_map: "
				"given physical block range is not allocated");
		ret = -EINVAL;
		goto out;
	}

	ret = inode_newsize_ok(inode, i_size_read(inode) + evfs_i.length);
	if (ret) {
		f2fs_msg(sb, KERN_ERR, "evfs_inode_map: "
				 "new inode size exceeds the size limit");
		goto out;
	}

	if (f2fs_has_inline_data(inode)) {
		f2fs_msg(sb, KERN_ERR, "evfs_inode_map: "
				"inode contains inline data");
		ret = -EINVAL;
		goto out;
	}

	f2fs_balance_fs(sbi, true);

	map.m_lblk = evfs_i.log_blkoff;
	map.m_pblk = evfs_i.phy_blkoff;
	map.m_len = evfs_i.length;

	ret = f2fs_evfs_map_blocks(inode, &map);
	if (ret) {
		if (!map.m_len)
			goto out;

		/* TODO: Free the allocated blocks if it's partially complete */
		f2fs_msg(sb, KERN_ERR, "evfs_inode_map: partially allocated, "
				"hence freed");
		goto out;
	}

out:
	fsync_bdev(sb->s_bdev);
	iput(inode);
	return ret;
}

long
f2fs_evfs_inode_unmap(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_imap evfs_i;
	struct inode *inode;
	block_t blk_addr, start, len;

	if (copy_from_user(&evfs_i, (struct evfs_imap __user *) arg,
				sizeof(struct evfs_imap)))
		return -EFAULT;

	inode = f2fs_iget(sb, evfs_i.ino_nr);
	if (IS_ERR(inode)) {
		f2fs_msg(sb, KERN_ERR, "iget failed during evfs");
		return PTR_ERR(inode);
	} else if (!S_ISREG(inode->i_mode)) {
		f2fs_msg(sb, KERN_ERR, "evfs_inode_unmap: "
				"can only unmap extent from regular file");
		return -EINVAL;
	}

	if (f2fs_has_inline_data(inode)) {
		f2fs_msg(sb, KERN_ERR, "evfs_inode_unmap: "
				"given inode contains inline data");
	}

	start = evfs_i.log_blkoff;
	len = evfs_i.length;
	for (blk_addr = start; blk_addr < start + len; blk_addr++) {
		unmap_block(inode, blk_addr);
	}

	iput(inode);
	return 0;
}

long f2fs_evfs_dirent_add(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct evfs_dirent_add_op add_op;
	struct inode *dir;
	struct inode *entry_inode;
	struct qstr entry_name;
	int err;

	if (copy_from_user(&add_op, (struct evfs_inode __user *) arg,
				sizeof(add_op)))
		return -EFAULT;

	dir = f2fs_iget(sb, add_op.dir_nr);
	if (IS_ERR(dir)) return PTR_ERR(dir);

	entry_inode = f2fs_iget(sb, add_op.ino_nr);
	if (IS_ERR(entry_inode)) return PTR_ERR(entry_inode);

	entry_name.name = add_op.name;
	entry_name.len = strnlen(add_op.name, sizeof(add_op.name));

	d_drop_entry_in_dir(dir, &entry_name);
	err = __f2fs_add_link(dir, &entry_name, entry_inode, entry_inode->i_ino,
			entry_inode->i_mode);

	return err;
}

long f2fs_evfs_dirent_remove(struct file *filp, struct super_block *sb,
		unsigned long arg)
{
	struct evfs_dirent_del_op del_op;
	struct f2fs_dir_entry *de;
	struct page *page;
	struct inode *dir;
	struct qstr entry_name;

	if (copy_from_user(&del_op, (struct evfs_inode __user *) arg,
				sizeof(del_op)))
		return -EFAULT;

	dir = f2fs_iget(sb, del_op.dir_nr);
	if (IS_ERR(dir)) return PTR_ERR(dir);

	entry_name.name = del_op.name;
	entry_name.len = strnlen(del_op.name, sizeof(del_op.name));

	de = f2fs_find_entry(dir, &entry_name, &page);
	if (!de) return PTR_ERR(de);

	d_drop_entry_in_dir(dir, &entry_name);
	f2fs_delete_entry(de, page, dir, NULL);

	return 0;
}

long
f2fs_evfs_inode_alloc(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct inode *new_i;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct evfs_inode i;
	struct page *page;
	int ret;

	if (copy_from_user(&i, (struct evfs_inode __user *) arg, sizeof(i)))
		return -EFAULT;

	/* Check if the given inode exists first with iget check */
	/* If found, put it back and return EEXIST error */
	new_i = f2fs_iget(sb, i.ino_nr);
	if (!IS_ERR(new_i)) {
		iput(new_i);
		f2fs_msg(sb, KERN_ERR, "given inode exists already");
		return -EEXIST;
	}

	/* From f2fs_new_inode in namei.c */
	new_i = new_inode(sb);
	if (!new_i) {
		f2fs_msg(sb, KERN_ERR, "new_inode failed during evfs");
		return -ENOMEM;
	}

	new_i->i_state = 0;
	new_i->i_op = &f2fs_file_inode_operations;
	new_i->i_fop = &f2fs_file_operations;
	new_i->i_mapping->a_ops = &f2fs_dblock_aops;
	new_i->i_ino = i.ino_nr;

	evfs_to_vfs_inode(&i, new_i);

	/* From f2fs_new_inode in namei.c */
	new_i->i_blocks = 0;
	new_i->i_generation = sbi->s_next_generation++;

	/* Below code reused from f2fs_create and f2fs_new_inode in namei.c */
	ret = insert_inode_locked(new_i);
	if (ret) {
		f2fs_msg(sb, KERN_ERR, "insert_inode_locked failed during evfs");
		return -ret;
	}

	ret = dquot_initialize(new_i);
	if (ret) {
		f2fs_msg(sb, KERN_ERR, "dquot_initialize failed during evfs");
		unlock_new_inode(new_i);
		return -ret;
	}
	ret = dquot_alloc_inode(new_i);
	if (ret) {
		f2fs_msg(sb, KERN_ERR, "dquot_alloc_inode failed during evfs");
		unlock_new_inode(new_i);
		return -ret;
	}

	set_inode_flag(new_i, FI_NEW_INODE);

	f2fs_init_extent_tree(new_i, NULL);

	stat_inc_inline_xattr(new_i);
	stat_inc_inline_inode(new_i);
	stat_inc_inline_dir(new_i);

	trace_f2fs_new_inode(new_i, 0);

	unlock_new_inode(new_i);
	/* ================================================================ */

	/* Inode is created, but we still need to create an entry in NAT */
	/* Below code reused from init_inode_metadata and f2fs_add_link in dir.c */
	page = new_inode_page(new_i);
	if (IS_ERR(page)) {
		f2fs_msg(sb, KERN_ERR, "new_inode_page failed during evfs");
		return PTR_ERR(page);
	}
	f2fs_i_pino_write(new_i, new_i->i_ino);
	f2fs_put_page(page, 1);

	if (copy_to_user((struct evfs_inode __user *) arg, &i, sizeof(i)))
		return -EFAULT;

	return 0;
}

long
f2fs_evfs_inode_free(struct file *filp, struct super_block *sb, unsigned long arg)
{
	int ret = 0;
	long ino_nr;
	struct inode *inode;

	if (get_user(ino_nr, (long __user *) arg)) {
		f2fs_msg(sb, KERN_ERR, "failed to retrieve argument");
		return -EFAULT;
	}

	inode = f2fs_iget(sb, ino_nr);
	if (IS_ERR(inode)) {
		f2fs_msg(sb, KERN_ERR, "iget failed during evfs");
		return PTR_ERR(inode);
	}
	if (inode->i_state & I_NEW) {
		return -ENOENT;
	}

	inode_lock(inode);
	f2fs_evict_inode(inode);
	inode_unlock(inode);

	return ret;
}

struct page *
get_page_cb(struct address_space *mapping, pgoff_t index)
{
	struct inode *inode = mapping->host;
	return find_data_page(inode, index);
}

long
f2fs_evfs_inode_read(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_inode_read_op read_op;
	struct inode *inode;
	struct iovec iov;
	struct iov_iter iter;
	int ret = 0;

	if (copy_from_user(&read_op, (struct evfs_inode_read_op __user *) arg,
				sizeof(struct evfs_inode_read_op)))
		return -EFAULT;

	inode = f2fs_iget(sb, read_op.ino_nr);
	if (IS_ERR(inode)) {
		f2fs_msg(sb, KERN_ERR, "evfs_inode_read: failed to find inode");
		return PTR_ERR(inode);
	}

	iov.iov_base = read_op.data;
	iov.iov_len = read_op.length;
	iov_iter_init(&iter, READ, &iov, 1, read_op.length);

	ret = evfs_page_read_iter(inode, (loff_t *)&read_op.ofs, &iter, 0,
			&get_page_cb);
	if (ret < 0)
		return ret;

	iput(inode);
	return 0;
}

long
f2fs_evfs_inode_get(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_inode evfs_i;
	struct inode *inode;

	/* Get evfs_inode struct from user */
	if (copy_from_user(&evfs_i, (long __user *) arg, sizeof(struct evfs_inode))) {
		f2fs_msg(sb, KERN_ERR, "failed to retrieve argument");
		return -EFAULT;
	}

	/* Retrieve inode */
	inode = f2fs_iget(sb, evfs_i.ino_nr);
	if (IS_ERR(inode)) {
		f2fs_msg(sb, KERN_ERR, "iget failed during evfs");
		return PTR_ERR(inode);
	}

	vfs_to_evfs_inode(inode, &evfs_i);
	evfs_i._prop.inlined = f2fs_has_inline_data(inode);

	if (copy_to_user((struct evfs_inode __user *) arg, &evfs_i, sizeof(evfs_i)))
		return -EFAULT;

	return 0;
}

static
long
f2fs_evfs_inode_update(struct super_block *sb, struct evfs_inode * evfs_inode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct inode *inode;
	struct page *page;

	inode = f2fs_iget(sb, evfs_inode->ino_nr);
	if (IS_ERR(inode)) {
		f2fs_msg(sb, KERN_ERR, "iget failed during evfs");
		return PTR_ERR(inode);
	}

	evfs_to_vfs_inode(evfs_inode, inode);

	page = get_node_page(sbi, evfs_inode->ino_nr);
	if (IS_ERR(page)) {
		f2fs_msg(sb, KERN_ERR, "get_node_page failed during evfs");
		iput(inode);
		return PTR_ERR(page);
	}
	update_inode(inode, page);
	f2fs_put_page(page, 1);

	/* Call iput twice in order to consider this function's iget call */
	iput(inode);
	iput(inode);

	return 0;
}

long
f2fs_evfs_inode_set(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_inode evfs_i;

	if (copy_from_user(&evfs_i, (struct evfs_inode __user *) arg,
				sizeof(struct evfs_inode)))
		return -EFAULT;

	return f2fs_evfs_inode_update(sb, &evfs_i);
}

long
f2fs_evfs_inode_iter(struct file *filp, struct super_block *sb, unsigned long arg)
{
	struct evfs_iter_ops iter;
	struct __evfs_ino_iter_param param;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_nm_info *nm = NM_I(sbi);
	struct node_info ni;
	struct inode *inode;
	nid_t end_nid = nm->max_nid, nid = 0;
	int ret = 0;

	if (copy_from_user(&iter, (struct evfs_iter_ops __user *) arg,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;

	nid = iter.start_from;

	for (; nid < end_nid; nid++) {
		get_node_info(sbi, nid, &ni);

		if (ni.blk_addr < MAIN_BLKADDR(sbi) || ni.ino != nid)
			continue;

		inode = f2fs_iget(sb, nid);
		if (IS_ERR(inode))
			continue;

		param.ino_nr = ni.ino;
		f2fs_msg(sb, KERN_ERR, "i_bytes: %u, i_size: %lld", inode->i_bytes, inode->i_size);
		vfs_to_evfs_inode(inode, &param.i);
		iput(inode);

		if (evfs_copy_param(&iter, &param,
				sizeof(struct __evfs_ino_iter_param))) {
			ret = 1;
			goto out;
		}
	}

out:
	if (copy_to_user((struct evfs_iter_ops __user *) arg, &iter,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;
	return ret;
}

long
f2fs_evfs_inode_stat(struct super_block *sb, unsigned long arg)
{
	int ret = 0;
	long ino_nr;
	struct evfs_inode *evfs_i;
	struct inode *inode;

	/* Get inode */
	if (get_user(ino_nr, (long __user *) arg)) {
		f2fs_msg(sb, KERN_ERR, "failed to retrieve argument");
		return -EFAULT;
	};
	inode = f2fs_iget(sb, ino_nr);
	if (IS_ERR(inode)) {
		f2fs_msg(sb, KERN_ERR, "iget failed during evfs");
		return PTR_ERR(inode);
	}

	vfs_to_evfs_inode(inode, evfs_i);

	return ret;

}

long
f2fs_evfs_sb_get(struct super_block *sb, unsigned long arg)
{
	struct evfs_super_block evfs_sb;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	/*
	 * Since F2FS does not have the notion of extents in the disk layout
	 * (there exists in-memory extent struct, which does not affect us),
	 * just return the maximum possible # of blocks for a given inode.
	 */
	evfs_sb.max_extent = sb->s_maxbytes >> PAGE_SHIFT;
	evfs_sb.max_bytes = sb->s_maxbytes;
	evfs_sb.page_size = PAGE_SIZE;
	evfs_sb.root_ino = F2FS_ROOT_INO(sbi);

	if (copy_to_user((struct evfs_super_block __user *) arg, &evfs_sb,
				sizeof(struct evfs_super_block)))
		return -EFAULT;
	return 0;
}


long
f2fs_evfs_sb_set(struct super_block *sb, unsigned long arg)
{
	struct evfs_super_block evfs_sb;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_super_block *f2fs_sb = F2FS_RAW_SUPER(sbi);
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	block_t main_blkaddr = MAIN_BLKADDR(sbi);
	unsigned int seg_nr, seg_nr_main, block_nr;
	long long bc_delta = le64_to_cpu(f2fs_sb->block_count);
	int ret = 0, type;

	if (copy_from_user(&evfs_sb, (struct evfs_super_block __user *) arg,
				sizeof(struct evfs_super_block))) {
		f2fs_msg(sb, KERN_ERR, "sb_set copying arg failed");
		return -EFAULT;
	}

	if (evfs_sb.block_count == bc_delta)
		goto out;

	// TODO: It may be necessary for the block count to be segment-aligned
	seg_nr = (evfs_sb.block_count + (sbi->blocks_per_seg / 2))
		>> sbi->log_blocks_per_seg;
	block_nr = seg_nr << sbi->log_blocks_per_seg;
	seg_nr_main = (block_nr - main_blkaddr) >> sbi->log_blocks_per_seg;
	bc_delta -= block_nr;

	/*
	 * Make sure that all cursegs are within the new range
	 */
	for (type = 0; type < NR_CURSEG_TYPE; type++) {
		struct curseg_info *curseg = CURSEG_I(sbi, type);

		f2fs_msg(sb, KERN_INFO, "curseg segno: %u, curseg type: %u, next_segno: %u",
				curseg->segno, type, curseg->next_segno);

		if (curseg->segno > seg_nr_main) {
			f2fs_msg(sb, KERN_INFO, "sb_set detected curseg (type %d)"
					"which is out of bounds. Attempting "
					"to relocate", type);
			if (!find_next_curseg(sbi, curseg, type, seg_nr_main)) {
				f2fs_msg(sb, KERN_ERR, "Relocation failed"
						 " (no free space available). Aborting...");
				ret = -ENOSPC;
				goto out;
			}
			change_curseg(sbi, type, true);
			f2fs_msg(sb, KERN_INFO, "sb_set: Newly assigned segno "
					"is %u", curseg->segno);
		}
	}

	/*
	 * In order to update the block size in F2FS,
	 * couple of variables below needs to be updated as well
	 * in order for it to be consistent
	 */
	f2fs_sb->block_count = cpu_to_le64(block_nr);
	sbi->user_block_count -= bc_delta;
	ckpt->user_block_count = cpu_to_le64(sbi->user_block_count);
	f2fs_sb->segment_count = cpu_to_le32(seg_nr - 1);
	sbi->total_sections = (seg_nr - 1) / sbi->segs_per_sec;
	f2fs_sb->section_count = cpu_to_le32(sbi->total_sections);
	f2fs_sb->segment_count_main = cpu_to_le32((block_nr - main_blkaddr)
					>> sbi->log_blocks_per_seg);

	if ((ret = f2fs_commit_super(sbi, 0))) {
		f2fs_msg(sb, KERN_ERR, "sb_set failed to commit super");
	}

	f2fs_sync_fs(sb, 1);

out:
	return ret;
}

long
f2fs_evfs_meta_iter(struct super_block *sb, unsigned long arg)
{
	struct evfs_iter_ops iter;
	struct __evfs_meta_iter param;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_nm_info *nm = NM_I(sbi);
	struct node_info ni;
	nid_t end_nid = nm->max_nid, nid = 0;
	int ret = 0;

	if (copy_from_user(&iter, (struct evfs_iter_ops __user *) arg,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;

	iter.count = 0;
	nid = iter.start_from;

	for (; nid < end_nid; nid++) {
		get_node_info(sbi, nid, &ni);

		if (ni.blk_addr < MAIN_BLKADDR(sbi))
			continue;

		if (ni.nid == ni.ino) {
			struct inode *inode = f2fs_iget(sb, ni.nid);

			if (IS_ERR(inode))
				continue;

			if (S_ISDIR(inode->i_mode))
				param.md.type = EVFS_META_DIRECTORY;
			else if (S_ISREG(inode->i_mode))
				param.md.type = EVFS_META_FILE;
			else
				param.md.type = EVFS_META_UNKNOWN;

			iput(inode);
		} else {
			param.md.type = EVFS_META_INDIR;
		}

		param.id = ni.nid;
		param.md.owner = ni.ino;
		param.md.blkaddr = ni.blk_addr;
		param.md.size = 1;
		param.md.loc_type = EVFS_META_DYNAMIC;
		param.md.region_start = START_BLOCK(sbi, GET_SEGNO(sbi, ni.blk_addr));
		param.md.region_len = 1ULL << sbi->log_blocks_per_seg;

		if (evfs_copy_param(&iter, &param,
				sizeof(struct __evfs_meta_iter))) {
			ret = 1;
			goto out;
		}
	}

out:
	if (copy_to_user((struct evfs_iter_ops __user *) arg, &iter,
				sizeof(struct evfs_iter_ops)))
		return -EFAULT;
	f2fs_msg(sb, KERN_INFO, "return value: %d, iter_count: %lu",
				ret, iter.count);
	return ret;
}


long
f2fs_evfs_meta_move(struct super_block *sb, unsigned long arg)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct evfs_meta_mv_ops op;

	if (copy_from_user(&op, (struct evfs_meta_mv_ops __user *) arg,
				sizeof(struct evfs_meta_mv_ops)))
		return -EFAULT;

	return __metadata_move(sbi, op.md.blkaddr, op.to_blkaddr);
}

static
long
f2fs_evfs_inode_lock(struct super_block * sb, struct evfs_lockable * lkb)
{
    struct inode * inode = f2fs_iget(sb, lkb->object_id);
	if (IS_ERR(inode)) {
		return -ENOENT;
	}
	
	if (lkb->exclusive) {
	    inode_lock(inode);
	}
	else {
	    inode_lock_shared(inode);
	}
	
	iput(inode);
	return 0;
}

static
void
f2fs_evfs_inode_unlock(struct super_block * sb, struct evfs_lockable * lkb)
{
    struct inode * inode = f2fs_iget(sb, lkb->object_id);
	if (IS_ERR(inode)) {
	    panic("trying to unlock inode %lu but it does not exist!\n", 
	        lkb->object_id);
	    return;
	}

    if (lkb->exclusive) {
        inode_unlock(inode);
    }
    else {
        inode_unlock_shared(inode);
    }

    iput(inode);
}

static long f2fs_evfs_lock_all(struct super_block * sb, 
                               struct evfs_lockable * lockables,
                               int nr_lockable)
{
    long err = 0;
    int i;

    for (i = 0; i < nr_lockable; i++) {
        switch (lockables[i].type) {
        case EVFS_TYPE_INODE:
            err = f2fs_evfs_inode_lock(sb, &lockables[i]);
            break;
        default:
            panic("not implemented. should not get here\n");
        }
        
        if (err < 0) {
            break;
        }
    }
    
    return err;
}

static void f2fs_evfs_unlock_all(struct super_block * sb, 
                                 struct evfs_lockable * lockables,
                                 int nr_lockable)
{
    int i;

    for (i = 0; i < nr_lockable; i++) {
        switch (lockables[i].type) {
        case EVFS_TYPE_INODE:
            f2fs_evfs_inode_unlock(sb, &lockables[i]);
            break;
        default:
            panic("not implemented. should not get here\n");
        }
    }
}

static
long
f2fs_evfs_atomic_action(struct super_block * sb, struct evfs_atomic_action * aa)
{
    long err = 0;

    // TODO: move into a generic evfs function
    int max_lockable = aa->nr_read + 1;
    int nr_lockable = 0;
    int lsize = max_lockable * sizeof(struct evfs_lockable);
    struct evfs_lockable * lockables = kmalloc(lsize, GFP_KERNEL | GFP_NOFS);
    
    if (!lockables) {
        return -ENOMEM;
    }

    if (aa->write_op) {
        switch (aa->write_op->opcode) {
        case EVFS_INODE_MAP:
        case EVFS_INODE_UPDATE:
            lockables[0].type = EVFS_TYPE_INODE;
            lockables[0].object_id = aa->write_op->inode.ino_nr;
            lockables[0].exclusive = 1;
            break;
        default:
            err = -EINVAL;
            goto fail;
        }
        
        nr_lockable++;
    }
    
    err = f2fs_evfs_lock_all(sb, lockables, nr_lockable);
    if (err < 0)
        goto fail;
    
    // TODO: perform read/comp operations
    
    // perform write operation
    if (aa->write_op) {
        switch (aa->write_op->opcode) {
        case EVFS_INODE_MAP:
        case EVFS_INODE_UPDATE:
            err = f2fs_evfs_inode_update(sb, &aa->write_op->inode);
            break;
        default:
            panic("this shouldn't be possible...");
        }
    }
    
    // unlock
    f2fs_evfs_unlock_all(sb, lockables, nr_lockable);

fail:
    kfree(lockables);
    (void)sb;
    return err;
}

long
f2fs_evfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	struct evfs_atomic_action aa;
	long err = 0;

	switch(cmd) {
	case FS_IOC_EVFS_ACTION:    
	    err = evfs_get_user_atomic_action(&aa, (void *) arg);
	    if (err < 0)
	        return err;
	    err = f2fs_evfs_atomic_action(sb, &aa);
	    evfs_destroy_atomic_action(&aa);
	    return err;
	case FS_IOC_EXTENT_ALLOC:
		return f2fs_evfs_extent_alloc(filp, sb, arg);
	case FS_IOC_EXTENT_ACTIVE:
		return f2fs_evfs_extent_active(filp, sb, arg);
	case FS_IOC_EXTENT_FREE:
		return f2fs_evfs_extent_free(filp, sb, arg);
	case FS_IOC_EXTENT_WRITE:
		return f2fs_evfs_extent_write(filp, sb, arg);
	case FS_IOC_EXTENT_ITERATE:
		return f2fs_evfs_extent_iter(filp, sb, arg);
	case FS_IOC_FREESP_ITERATE:
		return f2fs_evfs_freesp_iter(filp, sb, arg);
	case FS_IOC_INODE_ALLOC:
		return f2fs_evfs_inode_alloc(filp, sb, arg);
	case FS_IOC_INODE_FREE:
		return f2fs_evfs_inode_free(filp, sb, arg);
	case FS_IOC_INODE_STAT:
		return f2fs_evfs_inode_stat(sb, arg);
	case FS_IOC_INODE_GET:
		return f2fs_evfs_inode_get(filp, sb, arg);
	case FS_IOC_INODE_SET:
		return f2fs_evfs_inode_set(filp, sb, arg);
	case FS_IOC_INODE_READ:
		return f2fs_evfs_inode_read(filp, sb, arg);
	case FS_IOC_INODE_MAP:
		return f2fs_evfs_inode_map(filp, sb, arg);
	case FS_IOC_INODE_UNMAP:
		return f2fs_evfs_inode_unmap(filp, sb, arg);
	case FS_IOC_INODE_ITERATE:
		return f2fs_evfs_inode_iter(filp, sb, arg);
	case FS_IOC_DIRENT_ADD:
		return f2fs_evfs_dirent_add(filp, sb, arg);
	case FS_IOC_DIRENT_REMOVE:
		return f2fs_evfs_dirent_remove(filp, sb, arg);
	case FS_IOC_SUPER_GET:
		return f2fs_evfs_sb_get(sb, arg);
	case FS_IOC_SUPER_SET:
		return f2fs_evfs_sb_set(sb, arg);
	case FS_IOC_META_MOVE:
		return f2fs_evfs_meta_move(sb, arg);
	case FS_IOC_META_ITER:
		return f2fs_evfs_meta_iter(sb, arg);
	}

	return -ENOTTY;
}

