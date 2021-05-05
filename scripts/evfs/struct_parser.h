struct evfs_inode {
    unsigned long ino_nr;

    struct evfs_timeval atime;
    struct evfs_timeval ctime;
    struct evfs_timeval mtime;
    struct evfs_timeval otime;

    int /* kuid_t */ uid;
    int /* kgid_t */ gid;
    unsigned short /* umode_t */ mode;
    unsigned int flags;
    union {
        const struct evfs_inode_property prop;  /* users read this */
        struct evfs_inode_property _prop;       /* kernel modify this */
    };
};

struct evfs_metadata {
    unsigned long md_id; /* id of metadata */
    unsigned long start;    // block_nr
    unsigned long length; // number of blocks
};

struct evfs_super_block {
    unsigned long max_extent; /* maximum allowed size of a given extent */
    unsigned long max_bytes; /* max file size */
    unsigned long page_size;
    unsigned long root_ino; /* root inode number */
	
};

struct evfs_extent {
    unsigned long start;     // if set to zero, means allocate any of length
    unsigned long length;
    unsigned long ino_nr;
};

struct evfs_dirent {
    long dir_nr;
    long ino_nr;
    int name_len;
    int file_type;
    char name[EVFS_MAX_NAME_LEN];
};

struct evfs_inode_property {
    int inlined;        // does inode have inlined data
    int refcount;       // link count
    long blockcount;    // number of blocks used
    long bytesize;      // size of inode, in bytes
};

struct evfs_timeval {
    long tv_sec;
    long tv_usec;

};
