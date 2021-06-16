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

struct evfs_atomic {
    int fd;
    int atomic;
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


int evfs_operation(struct evfs_atomic * aa, int opcode, void * data);


#endif // EVFSLIB_H_

