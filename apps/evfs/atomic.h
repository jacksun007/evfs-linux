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
    EVFS_CONST_EQUAL,   // compare field with a constant
    EVFS_FIELD_EQUAL,   // compare field with another field

    // read operations
    EVFS_SUPER_INFO,
    EVFS_INODE_INFO,
    EVFS_DIRENT_INFO,

    EVFS_EXTENT_READ,   // read raw data from extent
    EVFS_INODE_READ,    // same as posix read()

    // write operations
    EVFS_EXTENT_ALLOC,
    EVFS_INODE_ALLOC,

    EVFS_EXTENT_WRITE,
    EVFS_INODE_WRITE,

    // Note: the identifier for dirents is its filename + parent inode
    EVFS_DIRENT_ADD,
    EVFS_DIRENT_REMOVE,
    EVFS_DIRENT_UPDATE,
    EVFS_DIRENT_RENAME, // unlike update, this *keeps* content but changes id

    // inode-specific operations
    EVFS_INODE_MAP,
    EVFS_INODE_UPDATE,

    EVFS_SUPER_UPDATE,
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

struct atomic_action {
    struct evfs_atomic header;
    int count;
    int capacity;
    int errop;
    struct evfs_opentry item[];
};

struct evfs_const_comp {
    int id;
    int field;
    u64 rhs;
};

int evfs_operation(struct evfs_atomic * aa, int opcode, void * data);

#endif

