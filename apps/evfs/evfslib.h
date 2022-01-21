/*
 * evfslib.h
 *
 * Userspace Evfs Library internal API
 *
 */ 

#ifndef EVFSLIB_H_
#define EVFSLIB_H_

#include <evfs.h>
#include <linux/ioctl.h>    // used by evfsctl.h
#include "evfsctl.h"

//
// define APIs ONLY used internally by Evfs userspace library
//

#ifdef NDEBUG
#define debug(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
#define debug(fmt, ...)
#endif

#define ATOMIC_MAGIC 0xECE326

struct evfs_atomic {
    int fd;
    struct {
        unsigned atomic : 8;
        unsigned magic  : 27;
    };
};

struct atomic_action {
    struct evfs_atomic header;
    
    /* found in evfsctl.h */
    struct evfs_atomic_action_param param;
};

typedef struct evfs_iter_s {
    evfs_t * evfs;
    int type;
    int flags;
    unsigned long count;
    unsigned long next_req;
    struct evfs_iter_ops op;
} evfs_iter_t;

#define evfs_operation(aa, opcode, data) \
    _evfs_operation(aa, opcode, &(data), sizeof(data))    
    
long _evfs_operation(struct evfs_atomic * aa, int opcode, 
                     void * data, size_t size);


#endif // EVFSLIB_H_

