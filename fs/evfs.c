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
evfs_new_atomic_action(struct evfs_atomic_action ** aap, 
                       struct super_block * sb, void * arg)
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
    aa->sb = sb;
    aa->nr_read = 0;
    aa->nr_comp = 0;
    aa->write_op = NULL;
    aa->read_set = NULL;
    aa->comp_set = NULL;
    
    /* count number of comp/read ops */
    for (i = 0; i < param.count; i++) {
        struct evfs_opentry * entry = &aa->param.item[i];
        if (IS_EVFS_READ_OP(entry->code)) {
            aa->nr_read += 1;
        }
        else if (IS_EVFS_COMP_OP(entry->code)) {
            aa->nr_comp += 1;
        }
        else {
            BUG_ON(!IS_EVFS_WRITE_OP(entry->code));
            
            if (aa->write_op != NULL) {
                err = -EINVAL;
                goto fail;
            }
            
            aa->write_op = entry;
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
void
evfs_add_lockable(struct evfs_lockable * lk, int type, unsigned long id, int ex)
{
    struct evfs_lockable * lkb = lk;
    
    while (lkb->type != EVFS_TYPE_INVALID) {
        
        /* we found a duplicate entry */
        if (lkb->type == type && lkb->object_id == id) {
            
            /* if not already exclusive, set it to exclusive */
            if (ex) {
                lkb->exclusive = 1;
            }
        
            return;
        }
    
        lkb++;
    }
    
    /* cannot find duplicate entry, add it to the end */
    lkb->type = type;
    lkb->object_id = id;
    lkb->exclusive = ex;
    
    /* invalidate last entry */
    lkb++;
    INVALIDATE_LOCKABLE(lkb);
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
    
    evfs_add_lockable(lk, EVFS_TYPE_INODE, ino_nr, ex);
    return 0;
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
        
        if (IS_EVFS_COMP_OP(entry->code)) {
            continue;
        }
        
        /* update as we support more evfs operations */
        switch (entry->code) {
            case EVFS_INODE_INFO:
            case EVFS_INODE_READ: 
                ret = evfs_add_inode_lockable(lockable, 0, entry->data);
                break;
            case EVFS_SUPER_INFO:
                evfs_add_lockable(lockable, EVFS_TYPE_SUPER, 0, 0);
                break;     
            case EVFS_DIRENT_INFO:
            case EVFS_EXTENT_READ:
                ret = -ENOSYS;
                break;    
            case EVFS_INODE_UPDATE:
            case EVFS_INODE_WRITE:
            case EVFS_INODE_MAP:
                ret = evfs_add_inode_lockable(lockable, 1, entry->data);
                break;
            case EVFS_SUPER_UPDATE:
                evfs_add_lockable(lockable, EVFS_TYPE_SUPER, 0, 0);
                break;          
            case EVFS_EXTENT_ALLOC:
            case EVFS_INODE_ALLOC:
            case EVFS_EXTENT_WRITE:
            case EVFS_DIRENT_ADD:
            case EVFS_DIRENT_REMOVE:
            case EVFS_DIRENT_UPDATE:
            case EVFS_DIRENT_RENAME:
                ret = -ENOSYS;
                break;
            default:
                ret = -EINVAL;
                aa->param.errop = entry->id;
        }
        
        if (ret < 0) {
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
evfs_execute_compare(struct evfs_atomic_action * aa, struct evfs_opentry * op)
{
    // TODO: merge with refactored code
    (void)aa;
    (void)op;
    return 0;
}

long
evfs_run_atomic_action(struct super_block * sb, 
                       struct evfs_atomic_op *fop,
                       void * arg)
{
    struct evfs_atomic_action * aa;
    struct evfs_lockable * lk, * lkb;
    long ret;
    unsigned i, j, k;
    
    /* set up atomic action */
    ret = evfs_new_atomic_action(&aa, sb, arg);
    if (ret < 0) {
        return ret;
    }
    
    //printk("atomic_action: %d read, %d comp, %d write\n", 
    //    aa->nr_read, aa->nr_comp, aa->write_op ? 1 : 0);
    
    ret = evfs_new_lock_set(aa, &lk);
    if (ret < 0) {
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
             
        //printk("locked: type = %u, id = %lu, exclusive = %d\n",
        //    lkb->type, lkb->object_id, lkb->exclusive); 
        i++; lkb++;
    }
    
    // run read set first 
    for (k = 0; k < aa->nr_read; k++) {
        ret = fop->execute(aa, aa->read_set[k]);
        if (ret < 0) {
            aa->param.errop = aa->read_set[k]->id;
            goto unlock;
        }
    }
    
    for (k = 0; k < aa->nr_comp; k++) {
        ret = evfs_execute_compare(aa, aa->comp_set[k]);
        if (ret < 0) {
            aa->param.errop = aa->comp_set[k]->id;
            goto unlock;
        }
    }
    
    // run write op last 
    if (aa->write_op) {
        ret = fop->execute(aa, aa->write_op);
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
                 &aa->param, sizeof(struct evfs_atomic_action_param));
    evfs_destroy_atomic_action(aa);
    return ret;
}


