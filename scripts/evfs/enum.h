enum evfs_inode_property{
INODE_PROPERTY_INVALID_FIELD,

/*auto-generated enums*/
INODE_PROPERTY_BYTESIZE,
INODE_PROPERTY_REFCOUNT,
INODE_PROPERTY_INLINED,
INODE_PROPERTY_BLOCKCOUNT
/*end of auto-generated enums*/
};

enum evfs_super_block{
SUPER_BLOCK_INVALID_FIELD,

/*auto-generated enums*/
SUPER_BLOCK_PAGE_SIZE,
SUPER_BLOCK_MAX_EXTENT,
SUPER_BLOCK_ROOT_INO,
SUPER_BLOCK_MAX_BYTES
/*end of auto-generated enums*/
};

enum evfs_dirent{
DIRENT_INVALID_FIELD,

/*auto-generated enums*/
DIRENT_DIR_NR,
DIRENT_INO_NR,
DIRENT_FILE_TYPE,
DIRENT_NAME_LEN,
DIRENT_NAME[EVFS_MAX_NAME_LEN]
/*end of auto-generated enums*/
};

enum evfs_inode{
INODE_INVALID_FIELD,

/*auto-generated enums*/
INODE_PROP_BLOCKCOUNT,
INODE_OTIME_TV_SEC,
INODE_MODE,
INODE_PROP_INLINED,
INODE_INO_NR,
INODE_GID,
INODE__PROP_REFCOUNT,
INODE_MTIME_TV_USEC,
INODE_MTIME_TV_SEC,
INODE__PROP_BLOCKCOUNT,
INODE_CTIME_TV_USEC,
INODE_PROP_BYTESIZE,
INODE_CTIME_TV_SEC,
INODE__PROP_INLINED,
INODE_ATIME_TV_SEC,
INODE_ATIME_TV_USEC,
INODE_PROP_REFCOUNT,
INODE_FLAGS,
INODE_OTIME_TV_USEC,
INODE_UID,
INODE__PROP_BYTESIZE
/*end of auto-generated enums*/
};

enum evfs_extent{
EXTENT_INVALID_FIELD,

/*auto-generated enums*/
EXTENT_START,
EXTENT_INO_NR,
EXTENT_LENGTH
/*end of auto-generated enums*/
};

enum evfs_timeval{
TIMEVAL_INVALID_FIELD,

/*auto-generated enums*/
TIMEVAL_TV_SEC,
TIMEVAL_TV_USEC
/*end of auto-generated enums*/
};

enum evfs_metadata{
METADATA_INVALID_FIELD,

/*auto-generated enums*/
METADATA_MD_ID,
METADATA_START,
METADATA_LENGTH
/*end of auto-generated enums*/
};

