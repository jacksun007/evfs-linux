/*
 * atomic.h
 *
 * Defines all evfs atomic interface
 *
 */
 
#ifndef ATOMIC_H
#define ATOMIC_H

enum evfs_opcode {
    EVFS_OPCODE_INVALID = 0,

    // compare operations
    EVFS_COMP_OP_BEGIN,
    
    EVFS_CONST_EQUAL = EVFS_COMP_OP_BEGIN, // compare field with a constant
    EVFS_FIELD_EQUAL,                      // compare field with another field
    
    EVFS_COMP_OP_END,
    
    // read operations
    EVFS_READ_OP_BEGIN = EVFS_COMP_OP_END,
 
    EVFS_INODE_INFO = EVFS_READ_OP_BEGIN,
    EVFS_SUPER_INFO,
    EVFS_DIRENT_INFO,

    EVFS_EXTENT_READ,   // read raw data from extent
    EVFS_INODE_READ,    // same as posix read()

    EVFS_READ_OP_END,

    // write operations
    EVFS_WRITE_OP_BEGIN = EVFS_READ_OP_END,
    
    EVFS_INODE_UPDATE = EVFS_WRITE_OP_BEGIN,
    EVFS_SUPER_UPDATE,
    EVFS_DIRENT_UPDATE,
    
    EVFS_EXTENT_ALLOC,
    EVFS_INODE_ALLOC,

    EVFS_EXTENT_WRITE,
    EVFS_INODE_WRITE,

    // Note: the identifier for dirents is its filename + parent inode
    EVFS_DIRENT_ADD,
    EVFS_DIRENT_REMOVE,
    EVFS_DIRENT_RENAME, // unlike update, this *keeps* content but changes id

    // inode-specific operations
    EVFS_INODE_MAP,

    EVFS_WRITE_OP_END,
};

struct evfs_atomic {
    int fd;
    int atomic;
};

struct evfs_opentry {
    int code;
    int id;
    void * data;
};

struct atomic_action_param {
    int count;
    int capacity;
    int errop;
    struct evfs_opentry item[];
};

struct atomic_action {
    struct evfs_atomic header;
    struct atomic_action_param param;
};

struct evfs_const_comp {
    int id;
    int field;
    u64 rhs;
};

int evfs_operation(struct evfs_atomic * aa, int opcode, void * data);

#endif

