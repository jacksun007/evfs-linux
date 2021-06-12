/*
 * evfs.c
 *
 * Just a bunch of scaffolds right now. Need to merge with refactored
 * Evfs kernel API
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stropts.h>
#include <evfs.h>
#include "ioctl.h"
#include "struct.h"

evfs_t * evfs_open(const char * dev)
{
    evfs_t * evfs = malloc(sizeof(evfs_t));
    
    if (!evfs) 
        return NULL;
        
    evfs->atomic = 0;
    evfs->fd = open(dev, O_RDONLY);
	if (evfs->fd < 0) {
		perror("open device");
		free(evfs);
		evfs = NULL;
	}
	
    return evfs;
}

void evfs_close(evfs_t * evfs)
{
    if (evfs != NULL) {
        close(evfs->fd);
        free(evfs);
    }
}

/*
 * TODO (kyokeun): Need to respect user-provided flag
 */
evfs_iter_t * inode_iter(evfs_t * evfs, int flags)
{
    evfs_iter_t *iter = malloc(sizeof(evfs_iter_t));
    iter->evfs = evfs;
    iter->type = EVFS_TYPE_INODE;
    iter->flags = flags;
    iter->count = 0;
    iter->next_req = 0;
    
    if (ioctl(evfs->fd, FS_IOC_INODE_ITERATE, &iter->op) < 0) {
        free(iter);
        return NULL;
    }

    return iter;
}

evfs_iter_t * extent_iter(evfs_t * evfs, int flags)
{
    (void)evfs;
    (void)flags;
    return NULL;
}

u64 inode_next(evfs_iter_t * it)
{
    struct __evfs_ino_iter_param *param;

    if (it->type != EVFS_TYPE_INODE)
        return -1;

    /* If we have iterated through everything in the buffer, get more */
    if (it->op.count < it->count) {
        it->op.start_from = it->next_req;
        if (ioctl(it->evfs->fd, FS_IOC_INODE_ITERATE, &it->op) <= 0)
            return 0;
        it->count = 0;
    }

    param = ((struct __evfs_ino_iter_param *) it->op.buffer) + it->count;
    ++it->count;
    it->next_req = param->ino_nr + 1;
    
    return param->ino_nr;
}

void iter_end(evfs_iter_t * it)
{
    free(it);
}

int super_info(evfs_t * evfs, struct evfs_super_block * sb)
{
    (void)evfs;
    (void)sb;
    return -EINVAL;
}

int extent_alloc(evfs_t * evfs, u64 pa, u64 len, int flags)
{
    (void)evfs;
    (void)pa;
    (void)len;
    (void)flags;
    return -EINVAL;
}

int extent_free(evfs_t * evfs, u64 pa)
{
    (void)evfs;
    (void)pa;
    return -EINVAL;
}

int extent_write(evfs_t * evfs, u64 pa, u64 off, char * buf, u64 len)
{
    (void)evfs;
    (void)pa;
    (void)off;
    (void)buf;
    (void)len;
    return -EINVAL;
}

int inode_info(evfs_t * evfs, struct evfs_inode * inode)
{
    (void)evfs;
    (void)inode;
    return -EINVAL;
}

int inode_read(evfs_t * evfs, u64 ino_nr, u64 off, char * buf, u64 len)
{
    (void)evfs;
    (void)ino_nr;
    (void)off;
    (void)buf;
    (void)len;
    return -EINVAL;
}

int inode_map(evfs_t * evfs, u64 ino_nr, struct evfs_imap * imap)
{
    (void)evfs;
    (void)ino_nr;
    (void)imap;
    return -EINVAL;
}

struct evfs_imap * imap_new(evfs_t * evfs)
{
    const int default_capacity = 64;
    struct evfs_imap * imap = malloc(sizeof(struct evfs_imap) + 
                              sizeof(struct evfs_imentry)*default_capacity);
    
    if (imap != NULL) {
        imap->count = 0;                          
        imap->capacity = default_capacity;
    }
    
    (void)evfs;
    return imap;
}

void imap_free(evfs_t * evfs, struct evfs_imap * imap, int nofree)
{
    unsigned i;
    
    if (imap != NULL && !nofree) {
        // free all physical extents
        for (i = 0; i < imap->count; i++) {
            if (imap->entry[i].phy_addr > 0) {
                extent_free(evfs, imap->entry[i].phy_addr);
            }   
        }

        free(imap);
    }
}

struct evfs_imap * imap_info(evfs_t * evfs, u64 ino_nr)
{
    struct evfs_imap * imap = imap_new(evfs);
    
    // TODO: implement when we need it
    
    (void)ino_nr;
    return imap;
}

int imap_append(struct evfs_imap * imap, u64 la, u64 pa, u64 len)
{
    if (!imap) {
        return -EINVAL;
    }
    
    // here, we want to check that the new entry cannot overlap or
    // come positionally before the last entry
    if (imap->count > 0) {
        struct evfs_imentry * last = &imap->entry[imap->count-1];
        if (la < last->log_addr + last->len) {
            return -EINVAL;
        }
    }
    
    // grow the imap structure if it is full
    assert(imap->count <= imap->capacity);
    if (imap->count >= imap->capacity) {
        const unsigned f = 2;
        imap = realloc(imap, sizeof(struct evfs_imap) + 
                             sizeof(struct evfs_imentry)*imap->capacity*f);
        if (imap == NULL)
            return -ENOMEM;
        
        imap->capacity *= f;
    }
    
    imap->entry[imap->count].inlined = 0;
    imap->entry[imap->count].log_addr = la;
    imap->entry[imap->count].phy_addr = pa;
    imap->entry[imap->count].len = len;
    imap->count += 1;
    
    return 0;
}

// TODO (jsun): finalize
// remove an entry at log_addr
// the shift flag determines whether to shift all subsequent entries forward
// by default will create a hole
int imap_remove(struct evfs_imap * imap, u64 log_addr, int shift)
{
    (void)imap;
    (void)log_addr;
    (void)shift;
    return -EINVAL;
}

struct evfs_op {
    int code;
    void * data;
};

struct atomic_action {
    struct evfs_atomic header;  // must start the same as evfs_t
    int count;
    int capacity;
    int err;
    int errop;
    struct evfs_op item[];
};

struct evfs_const_comp {
    int id;
    int field;
    u64 rhs;
};

static struct atomic_action * atomic_action_new(unsigned capacity) {
    struct atomic_action * aa = malloc(sizeof(struct atomic_action) + 
        sizeof(struct evfs_op)*capacity);
    
    if (aa != NULL) {
        aa->count = 0;
        aa->capacity = capacity;
        aa->err = 0;
        aa->errop = 0;
    }
    
    return aa;
}

static int 
atomic_action_append(struct atomic_action * aa, int opcode, void * data)
{
    if (aa == NULL) {
        return -EINVAL;
    }
    
    assert(aa->count <= aa->capacity);
    if (aa->count >= aa->capacity) {
        const unsigned f = 2;
        aa = realloc(aa, sizeof(struct atomic_action) + 
                         sizeof(struct evfs_op)*aa->capacity*f);
        if (aa == NULL)
            return -ENOMEM;
        
        aa->capacity *= f;
    }
    
    aa->item[aa->count].code = opcode;
    aa->item[aa->count].data = data;
    aa->count += 1;
    
    // this is the item id
    return aa->count;
}

struct evfs_atomic * atomic_begin(evfs_t * evfs) {
    const unsigned default_capacity = 8;
    struct atomic_action * aa = atomic_action_new(default_capacity);
    
    if (aa == NULL) {
        return NULL;
    }
    
    aa->header.fd = evfs->fd;
    aa->header.atomic = 1;
    return &aa->header;
}

struct atomic_action * to_atomic_action(struct evfs_atomic * ea) {
    struct atomic_action * aa = (struct atomic_action *)ea;
    assert(aa != NULL);
    assert(aa->header.atomic);
    return aa;
}

int atomic_const_equal(struct evfs_atomic * ea, int id, int field, u64 rhs)
{
    struct atomic_action * aa = to_atomic_action(ea);
    struct evfs_const_comp * comp = malloc(sizeof(struct evfs_const_comp));
    int ret;
    
    if (!comp) {
        return -ENOMEM;
    }
    
    comp->id = id;
    comp->field = field;
    comp->rhs = rhs;
    ret = atomic_action_append(aa, EVFS_CONST_EQUAL, comp);
    if (!ret) {
        free(comp);
        return ret;
    }
    
    return 0;
}

int atomic_execute(struct evfs_atomic * ea)
{
    struct atomic_action * aa = to_atomic_action(ea);
    
    // call atomic action interface
    
    (void)aa;
    return 0;    
}

void atomic_end(struct evfs_atomic * ea)
{
    int i;
    struct atomic_action * aa = to_atomic_action(ea);

    for (i = 0; i < aa->count; i++) {
        free(aa->item[i].data);
    }
    
    free(aa);
}
