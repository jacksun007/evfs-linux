/*
 * linux/fs/evfs.c
 *
 * Implementation of generic Evfs interface
 *
 * Copyright (C) 2018
 *
 * Kyo-Keun Park
 * Kuei Sun
 */

#include <linux/fs.h>
#include <linux/evfs.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/uio.h>
#include <linux/swap.h>
#include <linux/gfp.h>
#include <linux/kernel.h>

/*
 * Based off of do_generic_file_read in mm/filemap.c
 */
ssize_t
evfs_page_read_iter(struct inode *inode, loff_t *ppos,
		struct iov_iter *iter, ssize_t written,
		struct page * (*page_cb)(struct address_space *, pgoff_t))
{
	struct address_space *mapping = inode->i_mapping;
	pgoff_t index;
	pgoff_t last_index;
	pgoff_t prev_index;
	unsigned long offset;
	unsigned int prev_offset;
	int error;

	if (unlikely(*ppos >= inode->i_sb->s_maxbytes))
		return 0;
	iov_iter_truncate(iter, inode->i_sb->s_maxbytes);

	index = *ppos >> PAGE_SHIFT;
	prev_index = index - 1;
	prev_offset = prev_index << PAGE_SHIFT;
	last_index = (*ppos + iter->count + PAGE_SIZE - 1) >> PAGE_SHIFT;
	offset = *ppos & ~PAGE_MASK;

	while(1) {
		struct page *page;
		pgoff_t end_index;
		loff_t isize;
		unsigned int nr, ret;

		cond_resched();

find_page:
		page = page_cb(mapping, index);
		if (unlikely(page == NULL)) {
			goto no_cached_page;
		}

		if (!PageUptodate(page)) {
			/*
			 * See comment in do_read_cache_page on why
			 * wait_on_page_locked is used to avoid unnecessarily
			 * serialisations and why it's safe.
			 */
			error = wait_on_page_locked_killable(page);
			if (unlikely(error))
				goto readpage_error;
			if (PageUptodate(page))
				goto page_ok;
			if (inode->i_blkbits == PAGE_SHIFT ||
					!mapping->a_ops->is_partially_uptodate)
				goto page_not_up_to_date;
			/* pipes can't handle partially uptodate pages */
			if (unlikely(iter->type & ITER_PIPE))
				goto page_not_up_to_date;
			if (!trylock_page(page))
				goto page_not_up_to_date;
			/* Did it get truncated before we got the lock? */
			if (!page->mapping)
				goto page_not_up_to_date_locked;
			if (!mapping->a_ops->is_partially_uptodate(page,
						offset, iter->count))
				goto page_not_up_to_date_locked;
			unlock_page(page);
		}

page_ok:
		/*
		 * i_size must be checked after we know the page is Uptodat
		 *
		 * Checking i_size after the check allows us to calculate
		 * the correct value for "nr", which means the zero-filled
		 * part of the page is not copied back to userspace (unless
		 * another truncate extends the file - this is desired though).
		 */

		isize = i_size_read(inode);
		end_index = (isize - 1) >> PAGE_SHIFT;
		if (unlikely(!isize || index > end_index)) {
			put_page(page);
			goto out;
		}

		/* nr is the maximum number of bytes to copy from this page */
		nr = PAGE_SIZE;
		if (index == end_index) {
			nr = ((isize - 1) & ~PAGE_MASK) + 1;
			if (nr <= offset) {
				put_page(page);
				goto out;
			}
		}
		nr = nr - offset;

		/* If users can be writing to this page using arbitrary
		 * virtual addresses, take care about potential aliasing
		 * before reading the page on the kernel size.
		 */
		if (mapping_writably_mapped(mapping))
			flush_dcache_page(page);

		/*
		 * When a sequential read accesses a page several times,
		 * only mark it as accessed the first time.
		 */
		if (prev_index != index || offset != prev_offset)
			mark_page_accessed(page);
		prev_index = index;

		/*
		 * Ok, we have the page, and it's up-to-date, so
		 * now we can copy it to user space...
		 */

		ret = copy_page_to_iter(page, offset, nr, iter);
		offset += ret;
		index += offset >> PAGE_SHIFT;
		offset &= ~PAGE_MASK;
		prev_offset = offset;

		put_page(page);
		written += ret;
		if (!iov_iter_count(iter))
			goto out;
		if (ret < nr) {
			error = -EFAULT;
			goto out;
		}
		continue;

page_not_up_to_date:
		/* Get exclusive access to the page ... */
		error = lock_page_killable(page);
		if(unlikely(error))
			goto readpage_error;

page_not_up_to_date_locked:
		/* Did it get truncated before we got the lock? */
		if (!page->mapping) {
			unlock_page(page);
			put_page(page);
			// continue;
		}

		/* Did somebody else fill it already? */
		if (PageUptodate(page)) {
			unlock_page(page);
			goto page_ok;
		}

readpage:
		ClearPageError(page);
		/* TODO: file struct should not be NULL here */
		error = mapping->a_ops->readpage(NULL, page);

		if (unlikely(error)) {
			if (error == AOP_TRUNCATED_PAGE) {
				put_page(page);
				error = 0;
				goto find_page;
			}
			goto readpage_error;
		}

		if (!PageUptodate(page)) {
			error = lock_page_killable(page);
			if (unlikely(error))
				goto readpage_error;
			if (!PageUptodate(page)) {
				if (page->mapping == NULL) {
					unlock_page(page);
					put_page(page);
					goto find_page;
				}
				unlock_page(page);
				error = -EIO;
				goto readpage_error;
			}
			unlock_page(page);
		}

		goto page_ok;

readpage_error:
		/* UHHUH!! A synchronous read error occurred. Report it */
		put_page(page);
		goto out;

no_cached_page:
		/*
		 * Ok, it wasn't cached, so we need to create a new
		 * page...
		 */
		page = page_cache_alloc_cold(mapping);
		if (!page) {
			error = -ENOMEM;
			goto out;
		}
		error = add_to_page_cache_lru(page, mapping, index,
				mapping_gfp_constraint(mapping, GFP_KERNEL));
		if (error) {
			put_page(page);
			if (error == -EEXIST) {
				error = 0;
				goto find_page;
			}
			goto out;
		}
		goto readpage;
	}
out:
	*ppos = ((loff_t)index << PAGE_SHIFT) + offset;
	return written ? written : error;
}

/*
 * Generic implementation of inode_read EVFS call. Since we are using VFS inode,
 * most file systems shouild be able to utilize this.
 */
long
evfs_inode_read(struct super_block * sb, void __user * arg,
		struct page * (*page_cb)(struct address_space *, pgoff_t))
{
	struct evfs_inode_read_op read_op;
	struct iovec iov;
	struct iov_iter iter;
	struct inode * inode;
	int err = 0;

	if (copy_from_user(&read_op, arg, sizeof(struct evfs_inode_read_op)))
	    return -EFAULT;

	inode = iget_locked(sb, read_op.ino_nr);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	iov.iov_base = read_op.data;
	iov.iov_len = read_op.length;
	iov_iter_init(&iter, READ, &iov, 1, read_op.length);

	err = evfs_page_read_iter(inode, (loff_t *)&read_op.ofs,
				  &iter, 0, page_cb);

	iput(inode);

	if (err < 0)
		return err;

	return 0;
}

/*
 * Based off of generic_perform_write in mm/filemap.c
 */
ssize_t
evfs_page_write_iter(struct inode * inode, loff_t * ppos,
		struct iov_iter * iter, ssize_t written,
		struct page * (*page_cb)(struct address_space *, pgoff_t))
{
	struct address_space *mapping = inode->i_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	pgoff_t index;
	pgoff_t last_index;
	pgoff_t prev_index;
	unsigned long offset;
	unsigned int prev_offset;
	long status = 0;

	index = *ppos >> PAGE_SHIFT;
	prev_index = index - 1;
	prev_offset = prev_index << PAGE_SHIFT;
	last_index = (*ppos + iter->count + PAGE_SIZE - 1) >> PAGE_SHIFT;
	offset = *ppos & ~PAGE_MASK;

	do {
		struct page *page;
		unsigned long offset;
		unsigned long bytes;
		ssize_t copied;
		void *fsdata;

		page = page_cb(mapping, index);

		offset = (*ppos & (PAGE_SIZE - 1));
		bytes = min_t(unsigned long, PAGE_SIZE - offset,
			      iov_iter_count(iter));

again:
		if (unlikely(iov_iter_fault_in_readable(iter, 0))) {
			status = -EFAULT;
			break;
		}

		if (fatal_signal_pending(current)) {
			status = -EINTR;
			break;
		}

		status = a_ops->write_begin(NULL, mapping, *ppos, bytes, 0, &page, &fsdata);

		if (unlikely(status < 0))
			break;

		if (mapping_writably_mapped(mapping))
			flush_dcache_page(page);

		copied = iov_iter_copy_from_user_atomic(page, iter, offset, bytes);
		flush_dcache_page(page);

		status = a_ops->write_end(NULL, mapping, *ppos, bytes, copied, page, fsdata);

		if(unlikely(status < 0))
			break;
		copied = status;

		cond_resched();

		iov_iter_advance(iter, copied);
		if (unlikely(copied == 0)) {
			bytes = min_t(unsigned long, PAGE_SIZE - offset,
				      iov_iter_single_seg_count(iter));
			goto again;
		}
		*ppos += copied;
		written += copied;

		balance_dirty_pages_ratelimited(mapping);
	} while(iov_iter_count(iter));

	return written ? written : status;
}

/*
 * Generic implementation of inode_write EVFS call. Since we are using VFS inode,
 * most file systems shouild be able to utilize this.
 *
 * BUG: Kernel panic few seconds after calling inode_write
 */
long
evfs_inode_write(struct super_block * sb, void __user * arg,
		struct page * (*page_cb)(struct address_space *, pgoff_t))
{
	struct evfs_inode_read_op read_op;
	struct iovec iov;
	struct iov_iter iter;
	struct inode * inode;
	int err = 0;

	if (copy_from_user(&read_op, arg, sizeof(struct evfs_inode_read_op)))
	    return -EFAULT;

	inode = iget_locked(sb, read_op.ino_nr);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	iov.iov_base = read_op.data;
	iov.iov_len = read_op.length;
	iov_iter_init(&iter, READ, &iov, 1, read_op.length);

	err = evfs_page_write_iter(inode, (loff_t *)&read_op.ofs,
				  &iter, 0, page_cb);

	iput(inode);

	if (err < 0)
		return err;

	return 0;
}


ssize_t
evfs_perform_write(struct super_block *sb, struct iov_iter *i, pgoff_t pg_offset)
{
	struct address_space *mapping = sb->s_bdev->bd_inode->i_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	long status = 0;
	ssize_t written = 0;
	unsigned int flags = 0;
	loff_t pos = pg_offset << PAGE_SHIFT;

	do {
		struct page *page;
		unsigned long offset;
		unsigned long bytes;
		size_t copied;
		void *fsdata;

		offset = (pos & (PAGE_SIZE - 1));
		bytes = min_t(unsigned long, PAGE_SIZE - offset,
				iov_iter_count(i));

again:
		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 *
		 * Not only is this an optimisation, but it is also required
		 * to check that the address is actually valid, when atomic
		 * usercopies are used, below.
		 */
		if (unlikely(iov_iter_fault_in_readable(i, bytes))) {
			status = -EFAULT;
			break;
		}

		if (fatal_signal_pending(current)) {
			status = -EINTR;
			break;
		}

		/* TODO: file struct is set to NULL here. Upon quick
		 *       investigation, it seems F2FS and Ext4 does
		 *       not actually utilizes this struct. But,
		 *       this may not always be the case. */
		status = a_ops->write_begin(NULL, mapping, pos, bytes, flags,
						&page, &fsdata);
		if (unlikely(status < 0))
			break;

		if (mapping_writably_mapped(mapping))
			flush_dcache_page(page);

		copied = iov_iter_copy_from_user_atomic(page, i, offset, bytes);
		flush_dcache_page(page);

		status = a_ops->write_end(NULL, mapping, pos, bytes, copied,
						page, fsdata);
		if (unlikely(status < 0))
			break;
		copied = status;

		wait_on_page_writeback(page);

		cond_resched();

		iov_iter_advance(i, copied);
		if (unlikely(copied == 0)) {
			/*
			 * If we were unable to copy any data at all, we must
			 * fall back to a single segment length write.
			 *
			 * If we didn't fallback here, we could livelock
			 * because not all segments in the iov can be copied at
			 * once without a pagefault.
			 */
			bytes = min_t(unsigned long, PAGE_SIZE - offset,
					iov_iter_single_seg_count(i));
			goto again;
		}
		pos += copied;
		written += copied;

		balance_dirty_pages_ratelimited(mapping);
	} while (iov_iter_count(i));

	fsync_bdev(sb->s_bdev);

	return written ? written : status;
}

long
evfs_extent_write(struct super_block * sb, void __user * arg)
{
	struct evfs_ext_rw_op write_op;
	struct iovec iov;
	struct iov_iter iter;
	ssize_t ret;
	size_t bytes;
	long err = 0;

	if (copy_from_user(&write_op, (struct evfs_ext_rw_op __user *) arg,
					   sizeof(struct evfs_ext_rw_op)))
		return -EFAULT;

	bytes = write_op.len * PAGE_SIZE;
	iov.iov_base = (char *)write_op.__data;
	iov.iov_len = bytes;
	iov_iter_init(&iter, WRITE, &iov, 1, bytes);

	ret = evfs_perform_write(sb, &iter, write_op.addr);
	if (iov.iov_len != ret) {
		printk("evfs_extent_write: expected to write "
			"%lu bytes, but wrote %ld bytes instead\n",
				bytes, ret);
		return -EFAULT;
	}
	printk("extent write err = %lu\n", err);
	return 0;
}

/*
 * Used for all iterate functions.
 * Copy the given parameter onto iter->buffer and increment iter->count.
 * If no additional parameter can be copied to the buffer (full), then return 1
 * Otherwise, return 0.
 */
int
evfs_copy_param(struct evfs_iter_ops *iter, const void *param, int size)
{
	unsigned int offset = size * iter->count;

	memcpy(iter->buffer + offset, param, size);
	++iter->count;

	if (size * (iter->count + 1) >= EVFS_BUFSIZE)
		return 1;
	return 0;
}

//
// evfs atomic action implementation
// 

void 
evfs_destroy_atomic_action(struct evfs_atomic_action * aa)
{
    if (aa) {
        kfree(aa->read_set);
        kfree(aa->comp_set);
        kfree(aa);
    }
}

#define _IS_EVFS_OP(v, t) \
    ((v) >= EVFS_ ## t ## _OP_BEGIN && (v) < EVFS_ ## t ## _OP_END)
#define IS_EVFS_READ_OP(v) _IS_EVFS_OP(v, READ)
#define IS_EVFS_COMP_OP(v) _IS_EVFS_OP(v, COMP)   
#define IS_EVFS_WRITE_OP(v) _IS_EVFS_OP(v, WRITE) 

static 
long
evfs_new_atomic_action(struct evfs_atomic_action ** aap, void * arg)
{
    struct evfs_atomic_action_param param;
    struct evfs_atomic_action * aa;
    unsigned long ret;
    long err;
    int i, r, c;
        
    /* copy the header */
    ret = copy_from_user(&param, (struct evfs_atomic_action_param __user *)arg,
                         sizeof(struct evfs_atomic_action_param));
    if (ret != 0) {
        return -EFAULT;
    }
    
    /* allocate entire structure */
    aa = kmalloc(sizeof(struct evfs_atomic_action) + 
                 param.count*sizeof(struct evfs_opentry), GFP_KERNEL | GFP_NOFS);
    if (!aa) {
        return -ENOMEM;
    }
    
    /* copy the flexible array */
    aa->param = param;
    ret = copy_from_user((char *)&aa->param + sizeof(param), 
                         (char *)arg + sizeof(param),
                         param.count*sizeof(struct evfs_opentry));
    if (ret != 0) {
        err = -EFAULT;
        goto fail;
    }
    
    /* initialize rest of struct */
    aa->nr_read = 0;
    aa->nr_comp = 0;
    aa->write_op = NULL;
    aa->read_set = NULL;
    aa->comp_set = NULL;
    
    /* these are set outside of this function */
    aa->sb = NULL;
    aa->fsop = NULL;
    
    /* count number of comp/read ops */
    for (i = 0; i < param.count; i++) {
        struct evfs_opentry * entry = &aa->param.item[i];
        if (IS_EVFS_READ_OP(entry->code)) {
            aa->nr_read += 1;
            printk("evfs: adding %d to read set\n", entry->code);
        }
        else if (IS_EVFS_COMP_OP(entry->code)) {
            aa->nr_comp += 1;
            printk("evfs: adding %d to comp set\n", entry->code);
        }
        else {
            BUG_ON(!IS_EVFS_WRITE_OP(entry->code));
            
            if (aa->write_op != NULL) {
                err = -EINVAL;
                goto fail;
            }
            
            aa->write_op = entry;
            printk("evfs: adding %d to write set\n", entry->code);
        }
    }
    
    /* allocate for read and comp set */
    if (aa->nr_read > 0) {
        aa->read_set = kmalloc(sizeof(struct evfs_opentry *)*aa->nr_read, 
                               GFP_KERNEL | GFP_NOFS);
        if (!aa->read_set) {
            ret = -ENOMEM;
            goto fail;
        }
    }
    
    if (aa->nr_comp > 0) {   
        aa->comp_set = kmalloc(sizeof(struct evfs_opentry *)*aa->nr_comp, 
                               GFP_KERNEL | GFP_NOFS);
        if (!aa->comp_set) {
            kfree(aa->read_set);
            ret = -ENOMEM;
            goto fail;
        }
    }
    
    /* set up read and comp set */
    for (i = 0, r = 0, c = 0; i < param.count; i++) {
        struct evfs_opentry * entry = &aa->param.item[i];
        if (IS_EVFS_READ_OP(entry->code)) {
            aa->read_set[r++] = entry;
        }
        else if (IS_EVFS_COMP_OP(entry->code)) {
            aa->comp_set[c++] = entry;
        }
    }
    
    aa->param.errop = 0;
    *aap = aa;
    return 0;
fail:
    kfree(aa);
    return err;
}

#define INVALIDATE_LOCKABLE(l) do { \
    (l)->type = EVFS_TYPE_INVALID; \
    (l)->object_id = 0; \
    (l)->exclusive = 0; \
} while(0)

static
struct evfs_lockable *
evfs_add_lockable(struct evfs_lockable * lk, int type, unsigned long id, 
                  int ex, unsigned long data)
{
    struct evfs_lockable * lkb = lk;

    while (lkb->type != EVFS_TYPE_INVALID) {
        
        /* we found a duplicate entry */
        if (lkb->type == type && lkb->object_id == id) {
            
            /* if not already exclusive, set it to exclusive */
            if (ex) {
                lkb->exclusive = 1;
            }
            
            if (lkb->data != data) {
                printk("evfs warning: duplicate object id with different "
                       " data in lock set\n");
            }
        
            return lkb;
        }
    
        lkb++;
    }
    
    /* cannot find duplicate entry, add it to the end */
    lkb->type = type;
    lkb->object_id = id;
    lkb->exclusive = ex;
    lkb->data = data;
    
    /* invalidate last entry */
    INVALIDATE_LOCKABLE(lkb + 1);
    return lkb;
}

static
long
evfs_add_inode_lockable(struct evfs_lockable * lk, int ex, void * arg)
{
    unsigned long ino_nr;
    long ret = copy_from_user(&ino_nr, (unsigned long __user *)arg, 
                         sizeof(unsigned long));
    if (ret != 0) {
        return -EFAULT;
    }
    
    evfs_add_lockable(lk, EVFS_TYPE_INODE, ino_nr, ex, 0);
    return 0;
}

static
struct evfs_lockable *
__evfs_add_extent_lockable(struct evfs_lockable * lk, int t, int ex, void * arg)
{
    struct evfs_extent extent;
    struct evfs_lockable *lkb;

    long ret = copy_from_user(&extent, (struct evfs_extent __user *)arg, 
                         sizeof(struct evfs_extent));
    if (ret != 0) {
	return ERR_PTR(-EFAULT);
    }

    lkb = evfs_add_lockable(lk, t, extent.addr, ex, extent.len);
    return lkb;
}

static
struct evfs_lockable *
evfs_add_extent_group_lockable(struct evfs_lockable * lk, int ex, void * arg)
{
    return __evfs_add_extent_lockable(lk, EVFS_TYPE_EXTENT_GROUP, ex, arg);
}

static
struct evfs_lockable *
evfs_add_extent_lockable(struct evfs_lockable * lk, int ex, void * arg)
{
    return __evfs_add_extent_lockable(lk, EVFS_TYPE_EXTENT, ex, arg);
}

static
long
evfs_new_lock_set(struct evfs_atomic_action * aa, struct evfs_lockable ** lp)
{
    int max_lockable = aa->nr_read + 2;
    int i;
    long ret = 0;
    
    struct evfs_lockable * lockable = kmalloc(
        max_lockable * sizeof(struct evfs_lockable), GFP_KERNEL | GFP_NOFS);
    
    if (!lockable) {
        return -ENOMEM;
    }
    
    /* last entry of the array is always invalid */
    INVALIDATE_LOCKABLE(&lockable[0]);

    for (i = 0; i < aa->param.count; i++) {
        struct evfs_opentry * entry = &aa->param.item[i];
	struct evfs_lockable * curr_lk;
        
        if (IS_EVFS_COMP_OP(entry->code)) {
            continue;
        }
        
        /* pre-check correctness of argument */
        ret = aa->fsop->prepare(aa, entry);
        if (ret < 0) {
            printk("evfs: operation %d failed during prepare.\n", entry->id);
            aa->param.errop = entry->id;
            goto fail;
        }
        
        ret = 0;
        switch (entry->code) {
            case EVFS_INODE_INFO:
            case EVFS_INODE_READ: 
            case EVFS_INODE_ACTIVE:
                ret = evfs_add_inode_lockable(lockable, 0, entry->data);
                break;
            case EVFS_SUPER_INFO:
                evfs_add_lockable(lockable, EVFS_TYPE_SUPER, 0, 0, 0);
                break;
            case EVFS_EXTENT_ACTIVE:
                /* TODO: do not need to lock here */
                break;
            case EVFS_EXTENT_READ:     
            case EVFS_EXTENT_WRITE:
                /* may need to lock evfs_my_extent for multithreaded app */
                break;  
            case EVFS_INODE_UPDATE:
            case EVFS_INODE_WRITE:
            case EVFS_INODE_MAP:
            case EVFS_INODE_FREE:
                ret = evfs_add_inode_lockable(lockable, 1, entry->data);
                break;
            case EVFS_SUPER_UPDATE:
                evfs_add_lockable(lockable, EVFS_TYPE_SUPER, 0, 0, 0);
                break;          
            case EVFS_EXTENT_ALLOC:
                curr_lk = evfs_add_extent_group_lockable(lockable, 1, entry->data);
		if (IS_ERR(curr_lk)) {
			ret = PTR_ERR(curr_lk);
		} else {
			entry->lkb = curr_lk;
			ret = 0;
		}
                break;
            case EVFS_EXTENT_FREE:
                curr_lk = evfs_add_extent_lockable(lockable, 1, entry->data);
		if (IS_ERR(curr_lk)) {
			ret = PTR_ERR(curr_lk);
		} else {
			entry->lkb = curr_lk;
			ret = 0;
		}
                break;    
            case EVFS_INODE_ALLOC:
            case EVFS_DIRENT_ADD:
            case EVFS_DIRENT_INFO:
            case EVFS_DIRENT_REMOVE:
            case EVFS_DIRENT_UPDATE:
            case EVFS_DIRENT_RENAME:
                ret = -ENOSYS;
                break;
            default:
                ret = -EINVAL;
        }
               
        if (ret < 0) {
            printk("evfs: operation %d failed during lock add.\n", entry->id);
            aa->param.errop = entry->id;
            goto fail;
        }
    }
    
    *lp = lockable;
    return 0;
fail:
    kfree(lockable);
    return ret;
}

static
long
evfs_get_inode_field_value(struct evfs_opentry * entry, int field, u64 * lhsp)
{
    struct evfs_inode inode;

	if (copy_from_user(&inode, entry->data, sizeof(struct evfs_inode)))
		return -EFAULT;

    switch (field)
    {
    case EVFS_INODE_MTIME_TV_SEC:
        *lhsp = inode.mtime.tv_sec;
        break;
    case EVFS_INODE_MTIME_TV_USEC:
        *lhsp = inode.mtime.tv_usec;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static
long
evfs_get_field_value(struct evfs_opentry * entry, int field, u64 * lhsp)
{
    switch (entry->code)
    {
    case EVFS_INODE_INFO:
        return evfs_get_inode_field_value(entry, field, lhsp);
    case EVFS_SUPER_INFO:
    case EVFS_DIRENT_INFO:
    case EVFS_INODE_ACTIVE:
    case EVFS_DIRENT_ACTIVE:
    case EVFS_EXTENT_ACTIVE:
        return -ENOSYS;
    default:
        // all other evfs operations cannot be used for comparison
        return -EINVAL;
    }
    
    return 0;
}

static
long
evfs_const_compare(struct evfs_atomic_action * aa, void __user * arg)
{
    struct evfs_const_comp comp;
    struct evfs_opentry * entry;
    u64 lhs;
    long ret;

    if (copy_from_user(&comp, arg, sizeof(struct evfs_const_comp)) != 0)
        return -EFAULT;
        
    if (comp.id <= 0 || comp.id > aa->param.count)
        return -EINVAL;
        
    entry = &aa->param.item[comp.id - 1];    
    ret = evfs_get_field_value(entry, comp.field, &lhs);
    if (ret < 0)
        return ret;
        
    return (lhs == comp.rhs) ? 1 : 0;
}

static
long
evfs_execute_compare(struct evfs_atomic_action * aa, struct evfs_opentry * op)
{
    int ret = 0;

    switch (op->code)
    {
    case EVFS_CONST_EQUAL:
        ret = evfs_const_compare(aa, op->data);
        break;
    case EVFS_FIELD_EQUAL:
        ret = -ENOSYS;
        break;
    default:
        ret = -EINVAL;
    }

    return ret;
}

long
evfs_run_atomic_action(struct file * filp, 
                       struct evfs_atomic_op *fop,
                       void * arg)
{
    struct inode *inode;
    struct evfs_atomic_action * aa;
    struct evfs_lockable * lk, * lkb;
    long ret;
    unsigned i, j, k;
    
    /* set up atomic action */
    ret = evfs_new_atomic_action(&aa, arg);
    if (ret < 0) {
        return ret;
    }
    
    aa->filp = filp;
    inode = file_inode(filp);
    aa->sb = inode->i_sb;
    aa->fsop = fop;
    
    //printk("atomic_action: %d read, %d comp, %d write\n", 
    //   aa->nr_read, aa->nr_comp, aa->write_op ? 1 : 0);
    
    ret = evfs_new_lock_set(aa, &lk);
    if (ret < 0) {
        printk("evfs: error while creating lock set\n");
        goto fail;
    }
    
    /* lock everything */
    i = 0;    
    lkb = lk;
    while (lkb->type != EVFS_TYPE_INVALID) {
        ret = fop->lock(aa, lkb);
        if (ret < 0) {
            printk("evfs warning: could not lock type = %u, id = %lu\n",
                   lkb->type, lkb->object_id);
            goto unlock;
        }
	printk("lkb->object_id = %lu\n", lkb->object_id);
        i++; lkb++;
    }
    
    // run read set first 
    for (k = 0; k < aa->nr_read; k++) {
        ret = fop->execute(aa, aa->read_set[k]);
        aa->read_set[k]->result = ret;
        if (ret < 0) {
            aa->param.errop = aa->read_set[k]->id;
            goto unlock;
        }
    }
    
    for (k = 0; k < aa->nr_comp; k++) {
        ret = evfs_execute_compare(aa, aa->comp_set[k]);
        aa->comp_set[k]->result = ret;
        // compare functions return 0 when comparison fails
        if (ret <= 0) {
            aa->param.errop = aa->comp_set[k]->id;
            goto unlock;
        }
    }
    
    // run write op last 
    if (aa->write_op) {
        ret = fop->execute(aa, aa->write_op);
        aa->write_op->result = ret;
        if (ret < 0) {
            aa->param.errop = aa->write_op->id;
            goto unlock;
        }
    }
    
    /* success */
    ret = 0;
    
unlock:
    /* have to be careful here because we may unlock due to failure.
       should only unlock as many as the ones successfully locked.   */
    j = 0; lkb = lk;
    while (lkb->type != EVFS_TYPE_INVALID && j < i) {
        fop->unlock(aa, lkb);
        //printk("unlocked: type = %u, id = %lu, exclusive = %d\n",
        //    lkb->type, lkb->object_id, lkb->exclusive); 
        j++; lkb++;
    } 
    
fail:
    /* assume success otherwise we are screwed anyways */
    copy_to_user((struct evfs_atomic_action_param __user *) arg, 
                 &aa->param, sizeof(struct evfs_atomic_action_param) + 
                 aa->param.count*sizeof(struct evfs_opentry));
    evfs_destroy_atomic_action(aa);
    return ret;
}

long evfs_imap_from_user(struct evfs_imap ** imptr, void __user * arg)
{
    u32 count;
    int imap_bytes;
    struct evfs_imap * imap;
    
    *imptr = NULL;
    if (copy_from_user(&count, arg, sizeof(u32)))
		return -EFAULT;
		
	imap_bytes = sizeof(struct evfs_imap) + count*sizeof(struct evfs_imentry);
    imap = kmalloc(imap_bytes, GFP_KERNEL | GFP_NOFS);
	if (!imap)
	    return -ENOMEM;	
		
	if (copy_from_user(imap, arg, imap_bytes))
		return -EFAULT;
		
	*imptr = imap;
    return 0;
}


static
long
evfs_imap_validate_entry(struct file * filp, struct evfs_imentry * entry)
{
    const struct evfs_extent * ext;
    
    // this is an unmap request
    if (entry->phy_addr == 0)
        return 0;
    
    ext = evfs_find_my_extent(filp, entry->phy_addr);
    if (!ext) {
        printk("evfs warning: cannot find extent %llu\n", entry->phy_addr);
        return -EINVAL;
    }
    
    // theoretically we should not make this check but it gets really
    // ugly if we have to break up extents in the tracker
    if (ext->len != entry->len) {
        printk("evfs warning: extent length mismatch. expect %llu, "
            "got %llu\n", ext->len, entry->len);
        return -EINVAL;
    }
    
    return 0;
}

long
evfs_prepare_inode_map(struct file * filp, void __user * arg)
{
    struct evfs_imap_op op;
    struct evfs_imap * imap;
    struct evfs_imentry * last, * this;
    long ret;
    unsigned i;
    
    if (copy_from_user(&op, arg, sizeof(struct evfs_imap_op)) != 0)
        return -EFAULT;
 
    ret = evfs_imap_from_user(&imap, op.imap);
    if (ret < 0)
        return ret;
  
    // validate entries
    last = &imap->entry[0];
    if ((ret = evfs_imap_validate_entry(filp, last)) < 0)
        goto fail;
    
    for (i = 1; i < imap->count; i++) {
        u64 end;
    
        this = &imap->entry[i];
        if ((ret = evfs_imap_validate_entry(filp, this)) < 0)
            goto fail;

        // check sortedness and no overlap
        end = last->log_addr + last->len;
        if (end > this->log_addr) {
            printk("evfs warning: imap is either not sorted or has overlaps. "
                "issue found at entry[%u] (la = %llu, pa = %llu, len = %llu)\n", 
                i, this->log_addr, this->phy_addr, this->len);
            ret = -EINVAL;
            goto fail;
        }
        
        last = this;
    }
    
    ret = 0;
fail:
    kfree(imap);
    return ret;
}

/*
 * evfs per-device extent/inode list
 *
 * keeps track of unmapped extents and unused inodes for clean-up 
 * upon close() or program termination.
 *
 * TODO: add locks to support multi-threaded evfs application
 *
 */
 
struct evfs_my_extent {
    struct rb_node node;
    struct evfs_extent extent;
};

struct evfs_my_inode {
    struct rb_node node;
    u64 ino_nr;
};

struct evfs {
    struct rb_root my_extents;
    struct rb_root my_inodes;
    struct evfs_op * op;
};



long evfs_open(struct file * filp, struct evfs_op * fop)
{
    struct evfs * evfs;

    if (filp->f_evfs != NULL)
        return -EINVAL;

    evfs = kmalloc(sizeof(struct evfs), GFP_KERNEL | GFP_NOFS);
    if (!evfs)
        return -ENOMEM;

    evfs->my_extents = RB_ROOT;
    evfs->my_inodes = RB_ROOT;
    evfs->op = fop;
    filp->f_evfs = evfs;

    return 0;
}

static 
void
evfs_free_my_extents(struct super_block * sb, struct evfs * evfs)
{
    struct evfs_my_extent * myex = NULL;
    struct rb_root *root = &evfs->my_extents;
    struct rb_node * node;
    
    
    while ((node = rb_last(root)) != NULL) {
        myex = rb_entry(node, struct evfs_my_extent, node);
        printk("evfs: removing addr = %llu, len = %llu\n", 
	           myex->extent.addr, myex->extent.len);
	    evfs->op->free_extent(sb, &myex->extent);
        rb_erase(node, root);
        kfree(myex);
    }
}

int evfs_release(struct inode * inode, struct file * filp)
{
    struct evfs * evfs = filp->f_evfs;

    if (evfs) {
        filp->f_evfs = NULL;
        evfs_free_my_extents(inode->i_sb, evfs);
        kfree(evfs);
    }
    
    return 0;
}

/* this function does exact match only */
static
struct evfs_my_extent *
__evfs_find_my_extent(struct evfs * evfs, u64 addr)
{
    struct rb_root * root;
    struct rb_node * node;
    struct evfs_my_extent * myex = NULL;
    
    root = &evfs->my_extents;
    node = root->rb_node;
    
    while (node) {
        myex = rb_entry(node, struct evfs_my_extent, node);
        
        if (addr == myex->extent.addr) {
            return myex;
        }
        else if (addr < myex->extent.addr) {
            node = node->rb_left;
        }
        else {
            node = node->rb_right;
        }
    }

    return NULL;
}

const 
struct evfs_extent *
evfs_find_my_extent(struct file * filp, u64 addr)
{
    struct evfs_my_extent * ret;
    struct evfs * evfs = filp->f_evfs;
    
    if (!evfs)
        return NULL;
    
    ret = __evfs_find_my_extent(evfs, addr);
    if (!ret)
        return NULL;
        
    return &ret->extent;
}

long 
evfs_remove_my_extent(struct file * filp, const struct evfs_extent * ext)
{
    struct evfs_my_extent * ret;
    struct evfs * evfs = filp->f_evfs;
    
    if (!evfs)
        return -EINVAL;
    
    ret = __evfs_find_my_extent(evfs, ext->addr);
    if (!ret)
        return 0;
    
    if (ret->extent.len != ext->len) {
        printk("evfs warning: length mismatch during remove_my_extent\n");
        return 0;
    }
    
    rb_erase(&ret->node, &evfs->my_extents);
    kfree(ret);
    return 1;    
}

long 
__evfs_extent_in_range(struct evfs * evfs, const struct evfs_extent * ext)
{
    struct rb_root * root;
    struct rb_node * node;
    struct evfs_my_extent * myex = NULL;
    unsigned long start, end, mystart, myend;
    
    root = &evfs->my_extents;
    node = root->rb_node;
    start = ext->addr;
    end = ext->addr + ext->len;
    
    while (node) {
        myex = rb_entry(node, struct evfs_my_extent, node);
        mystart = myex->extent.addr;
        myend = myex->extent.addr + myex->extent.len;        
  
        if (start >= mystart) {         
            // ext is contained within myex
            if (myend >= end) {
                printk("(%lu, %lu) in (%lu, %lu)? yes\n", 
                       start, end, mystart, myend);
                return 1;
            }
        
            node = node->rb_right;
        }
        else {
            node = node->rb_left;
        }
        
        printk("(%lu, %lu) in (%lu, %lu)? no\n", start, end, mystart, myend);
    }

    return 0;
}

long 
evfs_extent_in_range(struct file * filp, const struct evfs_extent * ext)
{
    struct evfs * evfs = filp->f_evfs;
    
    if (!evfs)
        return -EINVAL;
    
    return __evfs_extent_in_range(evfs, ext);
}

long 
evfs_add_my_extent(struct file * filp, const struct evfs_extent * ext)
{
    struct evfs * evfs = filp->f_evfs;
    struct rb_root * root;
    struct rb_node ** new, * parent = NULL;
    struct evfs_my_extent * myex = NULL;

    if (!evfs)
        return -EINVAL;
        
    root = &evfs->my_extents;
    new = &root->rb_node;

  	/* Figure out where to put new node */
  	while (*new) {
  		myex = container_of(*new, struct evfs_my_extent, node);

		parent = *new;
  		if (ext->addr < myex->extent.addr)
  			new = &((*new)->rb_left);
  		else if (ext->addr > myex->extent.addr)
  			new = &((*new)->rb_right);
  		else
  			return 0;
  	}

  	myex = kmalloc(sizeof(struct evfs_my_extent), GFP_KERNEL | GFP_NOFS);
  	if (!myex) {
  	    return -ENOMEM;
  	}
  	
  	/* Add new node and rebalance tree. */
  	myex->extent = *ext;
  	rb_link_node(&myex->node, parent, new);
  	rb_insert_color(&myex->node, root);

	return 1;
}

long evfs_list_my_extents(struct file * filp)
{
    struct evfs * evfs = filp->f_evfs;
    struct rb_root *root;
    struct rb_node *node;
    struct evfs_my_extent * myex;
    int count = 0;
    
    if (!evfs) {
        printk("evfs: not opened via evfs_open\n");
        return -EINVAL;
    }
    
    root = &evfs->my_extents;    
    for (node = rb_first(root); node; node = rb_next(node)) 
    {
        myex = rb_entry(node, struct evfs_my_extent, node);
        count++;
	    printk("%d: addr = %llu, len = %llu\n", 
	            count, myex->extent.addr, myex->extent.len);
	}
	
	printk("%d extents are owned by this evfs device\n", count);
	return 0;
}

/*
 * TODO: support tracking of inodes
 */

long 
evfs_add_my_inode(struct file * filp, u64 ino_nr)
{
    (void)filp;
    (void)ino_nr;
    return -ENOSYS;
}

long 
evfs_remove_my_inode(struct file * filp, u64 ino_nr)
{
    (void)filp;
    (void)ino_nr;
    return -ENOSYS;
}

