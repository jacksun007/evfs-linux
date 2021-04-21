#include <linux/fs.h>
#include <linux/evfs.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/uio.h>
#include <linux/swap.h>
#include <linux/gfp.h>

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

static
long
evfs_get_user_write_op(struct evfs_write_op ** woptr, void * arg)
{
    const size_t MSIZE = sizeof(struct evfs_write_op);
    struct evfs_write_op * wop = kmalloc(MSIZE, GFP_KERNEL | GFP_NOFS);
    long err;
    
    if (copy_from_user(wop, (struct evfs_write_op __user *) arg,
				sizeof(struct evfs_write_op))) {
		err = -EFAULT;
	    goto fail;
	}

    *woptr = wop;
    return 0;
    
fail:
    kfree(wop);
    *woptr = NULL;
    return err;
}

void 
evfs_destroy_atomic_action(struct evfs_atomic_action * aa)
{
    // TODO: this may need to be updated if write_op contains dynamically
    //       allocated memory
    if (aa->write_op)
        kfree(aa->write_op);
}


long
evfs_get_user_atomic_action(struct evfs_atomic_action * aout, void * arg)
{
    long err;
    
    if (copy_from_user(aout, (struct evfs_atomic_action __user *) arg,
				sizeof(struct evfs_atomic_action)))
	    return -EFAULT;

	// TODO: support read and comp operations later
	aout->read_set = NULL;
	aout->comp_set = NULL;

	err = evfs_get_user_write_op(&aout->write_op, aout->write_op);
	if (err)
	    return err;
	
	aout->err = 0;
    aout->errop = 0;
    
    return 0;
}

