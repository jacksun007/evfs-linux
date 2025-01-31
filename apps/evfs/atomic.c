/*
 * atomic.c
 *
 * Implements all evfs atomic interface
 *
 */

#include <sys/ioctl.h> 
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include "evfslib.h"


static struct atomic_action * atomic_action_new(unsigned capacity) {
    const unsigned byte_size = sizeof(struct atomic_action) + 
                               sizeof(struct evfs_opentry)*capacity;
    struct atomic_action * aa = malloc(byte_size);
      
    if (aa != NULL) {
        aa->param.count = 0;
        aa->param.capacity = capacity;
        aa->param.errop = 0;
        aa->header.magic = ATOMIC_MAGIC;
        aa->header.atomic = 1;
    }
    
    return aa;
}

#define atomic_action_append(aab, opcode, data) \
    _atomic_action_append((aab), (opcode), &(data), sizeof(data))

static long _atomic_action_append(struct atomic_action * aab, int opcode, 
                                  void * data, size_t size)
{
    struct evfs_atomic_action_param * aa = &aab->param;
    void * realdata = data;

    if (aa == NULL) {
        return -EINVAL;
    }
    
    assert(aa->count <= aa->capacity);
    if (aa->count >= aa->capacity) {
        fprintf(stderr, "error: realloc for atomic action not implemented\n");
        return -ENOSYS;
    }
    
    if (aab->header.atomic) {
        if (!(realdata = malloc(size))) {
            return -ENOMEM;
        }
        
        assert(size != 0);
        memcpy(realdata, data, size);
    }
    
    aa->item[aa->count].code = opcode;
    aa->item[aa->count].id   = aa->count + 1;
    aa->item[aa->count].data = realdata;
    aa->item[aa->count].result = -ECANCELED;
    aa->count += 1;
    
    // this is the item id
    return aa->count;
}

static struct atomic_action * to_atomic_action(struct evfs_atomic * ea) {
    struct atomic_action * aa = (struct atomic_action *)ea;
    assert(aa != NULL);
    assert(aa->header.magic == ATOMIC_MAGIC);
    return aa;
}

long _evfs_operation(struct evfs_atomic * evfs, int opcode, 
                     void * data, size_t size)
{
    long ret;
    struct atomic_action * aa;

    if (evfs->atomic) {
        aa = to_atomic_action(evfs);
        ret = _atomic_action_append(aa, opcode, data, size);
    }
    else {
        if (!(aa = atomic_action_new(1))) 
            return -ENOMEM;
        
        aa->header.fd = evfs->fd;
        aa->header.atomic = 0;    /* this states that clean-up not required */
        ret = _atomic_action_append(aa, opcode, data, 0);
        
        // on successful append, execute the operation
        if (!(ret < 0)) {
            ret = atomic_execute(&aa->header);
            if (ret >= 0) {
                // on successful execute, return the result of the operation
                ret = aa->param.item[0].result;
            }
        }
            
        atomic_end(&aa->header);
    }
    
    return ret;
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


long atomic_execute(struct evfs_atomic * ea)
{
    struct atomic_action * aa = to_atomic_action(ea);

    /* on the kernel side, we do not send in the header */
    long ret = ioctl(ea->fd, FS_IOC_ATOMIC_ACTION, &aa->param);
    
    if (ret < 0) {
        return -errno;
    }
    
    return 0;    
}

void atomic_end(struct evfs_atomic * ea)
{
    if (ea->atomic) {
        struct atomic_action * aa = to_atomic_action(ea);
        int i;
        
        for (i = 0; i < aa->param.count; i++) {
            struct evfs_opentry * item = &aa->param.item[i];
            free(item->data);
        }
    }
    
    free(ea);
}


long atomic_const_equal(struct evfs_atomic * ea, int id, int field, u64 rhs)
{
    struct atomic_action * aa = to_atomic_action(ea);
    struct evfs_const_comp comp;

    comp.id = id;
    comp.field = field;
    comp.rhs = rhs;
    
    return atomic_action_append(aa, EVFS_CONST_EQUAL, comp);
}

long atomic_result(struct evfs_atomic * ea, int id)
{
    struct atomic_action * aa = to_atomic_action(ea);
    struct evfs_opentry * entry;

    if (id <= 0 || id > aa->param.count)
        return -EINVAL;
    
    entry = &aa->param.item[id - 1];
    assert(entry->id == id);
    return entry->id;
}

