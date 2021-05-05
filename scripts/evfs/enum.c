#include <stdio.h>
#include <stdlib.h>
#include <enum.h>
#include <struct_parser.h>

void main(){

/* auto-generated enum size arrays */

int inode_property_sizes[]={
sizeof(long),
sizeof(int),
sizeof(int),
sizeof(long)
};

int super_block_sizes[]={
sizeof(unsigned long),
sizeof(unsigned long),
sizeof(unsigned long),
sizeof(unsigned long)
};

int dirent_sizes[]={
sizeof(long),
sizeof(long),
sizeof(int),
sizeof(int),
sizeof(char)
};

int inode_sizes[]={
sizeof(long),
sizeof(long),
sizeof(unsigned short),
sizeof(int),
sizeof(unsigned long),
sizeof(int),
sizeof(int),
sizeof(long),
sizeof(long),
sizeof(long),
sizeof(long),
sizeof(long),
sizeof(long),
sizeof(int),
sizeof(long),
sizeof(long),
sizeof(int),
sizeof(unsigned int),
sizeof(long),
sizeof(int),
sizeof(long)
};

int extent_sizes[]={
sizeof(unsigned long),
sizeof(unsigned long),
sizeof(unsigned long)
};

int timeval_sizes[]={
sizeof(long),
sizeof(long)
};

int metadata_sizes[]={
sizeof(unsigned long),
sizeof(unsigned long),
sizeof(unsigned long)
};

int inode_property_enum_to_field(int ev, struct evfs_inode_property *inode_property, void **fieldptr, unsigned *size) {
    switch(ev) {
    case INODE_PROPERTY_BYTESIZE:
        *fieldptr = &inode_property->BYTESIZE;
        *size = inode_property_sizes [INODE_PROPERTY_BYTESIZE];
        break;
    case INODE_PROPERTY_REFCOUNT:
        *fieldptr = &inode_property->REFCOUNT;
        *size = inode_property_sizes [INODE_PROPERTY_REFCOUNT];
        break;
    case INODE_PROPERTY_INLINED:
        *fieldptr = &inode_property->INLINED;
        *size = inode_property_sizes [INODE_PROPERTY_INLINED];
        break;
    case INODE_PROPERTY_BLOCKCOUNT:
        *fieldptr = &inode_property->BLOCKCOUNT;
        *size = inode_property_sizes [INODE_PROPERTY_BLOCKCOUNT];
        break;
    default:
        return -EINVAL;
    }
    return 0;
    }

int super_block_enum_to_field(int ev, struct evfs_super_block *super_block, void **fieldptr, unsigned *size) {
    switch(ev) {
    case SUPER_BLOCK_PAGE_SIZE:
        *fieldptr = &super_block->PAGE_SIZE;
        *size = super_block_sizes [SUPER_BLOCK_PAGE_SIZE];
        break;
    case SUPER_BLOCK_MAX_EXTENT:
        *fieldptr = &super_block->MAX_EXTENT;
        *size = super_block_sizes [SUPER_BLOCK_MAX_EXTENT];
        break;
    case SUPER_BLOCK_ROOT_INO:
        *fieldptr = &super_block->ROOT_INO;
        *size = super_block_sizes [SUPER_BLOCK_ROOT_INO];
        break;
    case SUPER_BLOCK_MAX_BYTES:
        *fieldptr = &super_block->MAX_BYTES;
        *size = super_block_sizes [SUPER_BLOCK_MAX_BYTES];
        break;
    default:
        return -EINVAL;
    }
    return 0;
    }

int dirent_enum_to_field(int ev, struct evfs_dirent *dirent, void **fieldptr, unsigned *size) {
    switch(ev) {
    case DIRENT_DIR_NR:
        *fieldptr = &dirent->DIR_NR;
        *size = dirent_sizes [DIRENT_DIR_NR];
        break;
    case DIRENT_INO_NR:
        *fieldptr = &dirent->INO_NR;
        *size = dirent_sizes [DIRENT_INO_NR];
        break;
    case DIRENT_FILE_TYPE:
        *fieldptr = &dirent->FILE_TYPE;
        *size = dirent_sizes [DIRENT_FILE_TYPE];
        break;
    case DIRENT_NAME_LEN:
        *fieldptr = &dirent->NAME_LEN;
        *size = dirent_sizes [DIRENT_NAME_LEN];
        break;
    case DIRENT_NAME[EVFS_MAX_NAME_LEN]:
        *fieldptr = &dirent->NAME[EVFS_MAX_NAME_LEN];
        *size = dirent_sizes [DIRENT_NAME[EVFS_MAX_NAME_LEN]];
        break;
    default:
        return -EINVAL;
    }
    return 0;
    }

int inode_enum_to_field(int ev, struct evfs_inode *inode, void **fieldptr, unsigned *size) {
    switch(ev) {
    case INODE_PROP_BLOCKCOUNT:
        *fieldptr = &inode->PROP_BLOCKCOUNT;
        *size = inode_sizes [INODE_PROP_BLOCKCOUNT];
        break;
    case INODE_OTIME_TV_SEC:
        *fieldptr = &inode->OTIME_TV_SEC;
        *size = inode_sizes [INODE_OTIME_TV_SEC];
        break;
    case INODE_MODE:
        *fieldptr = &inode->MODE;
        *size = inode_sizes [INODE_MODE];
        break;
    case INODE_PROP_INLINED:
        *fieldptr = &inode->PROP_INLINED;
        *size = inode_sizes [INODE_PROP_INLINED];
        break;
    case INODE_INO_NR:
        *fieldptr = &inode->INO_NR;
        *size = inode_sizes [INODE_INO_NR];
        break;
    case INODE_GID:
        *fieldptr = &inode->GID;
        *size = inode_sizes [INODE_GID];
        break;
    case INODE__PROP_REFCOUNT:
        *fieldptr = &inode->_PROP_REFCOUNT;
        *size = inode_sizes [INODE__PROP_REFCOUNT];
        break;
    case INODE_MTIME_TV_USEC:
        *fieldptr = &inode->MTIME_TV_USEC;
        *size = inode_sizes [INODE_MTIME_TV_USEC];
        break;
    case INODE_MTIME_TV_SEC:
        *fieldptr = &inode->MTIME_TV_SEC;
        *size = inode_sizes [INODE_MTIME_TV_SEC];
        break;
    case INODE__PROP_BLOCKCOUNT:
        *fieldptr = &inode->_PROP_BLOCKCOUNT;
        *size = inode_sizes [INODE__PROP_BLOCKCOUNT];
        break;
    case INODE_CTIME_TV_USEC:
        *fieldptr = &inode->CTIME_TV_USEC;
        *size = inode_sizes [INODE_CTIME_TV_USEC];
        break;
    case INODE_PROP_BYTESIZE:
        *fieldptr = &inode->PROP_BYTESIZE;
        *size = inode_sizes [INODE_PROP_BYTESIZE];
        break;
    case INODE_CTIME_TV_SEC:
        *fieldptr = &inode->CTIME_TV_SEC;
        *size = inode_sizes [INODE_CTIME_TV_SEC];
        break;
    case INODE__PROP_INLINED:
        *fieldptr = &inode->_PROP_INLINED;
        *size = inode_sizes [INODE__PROP_INLINED];
        break;
    case INODE_ATIME_TV_SEC:
        *fieldptr = &inode->ATIME_TV_SEC;
        *size = inode_sizes [INODE_ATIME_TV_SEC];
        break;
    case INODE_ATIME_TV_USEC:
        *fieldptr = &inode->ATIME_TV_USEC;
        *size = inode_sizes [INODE_ATIME_TV_USEC];
        break;
    case INODE_PROP_REFCOUNT:
        *fieldptr = &inode->PROP_REFCOUNT;
        *size = inode_sizes [INODE_PROP_REFCOUNT];
        break;
    case INODE_FLAGS:
        *fieldptr = &inode->FLAGS;
        *size = inode_sizes [INODE_FLAGS];
        break;
    case INODE_OTIME_TV_USEC:
        *fieldptr = &inode->OTIME_TV_USEC;
        *size = inode_sizes [INODE_OTIME_TV_USEC];
        break;
    case INODE_UID:
        *fieldptr = &inode->UID;
        *size = inode_sizes [INODE_UID];
        break;
    case INODE__PROP_BYTESIZE:
        *fieldptr = &inode->_PROP_BYTESIZE;
        *size = inode_sizes [INODE__PROP_BYTESIZE];
        break;
    default:
        return -EINVAL;
    }
    return 0;
    }

int extent_enum_to_field(int ev, struct evfs_extent *extent, void **fieldptr, unsigned *size) {
    switch(ev) {
    case EXTENT_START:
        *fieldptr = &extent->START;
        *size = extent_sizes [EXTENT_START];
        break;
    case EXTENT_INO_NR:
        *fieldptr = &extent->INO_NR;
        *size = extent_sizes [EXTENT_INO_NR];
        break;
    case EXTENT_LENGTH:
        *fieldptr = &extent->LENGTH;
        *size = extent_sizes [EXTENT_LENGTH];
        break;
    default:
        return -EINVAL;
    }
    return 0;
    }

int timeval_enum_to_field(int ev, struct evfs_timeval *timeval, void **fieldptr, unsigned *size) {
    switch(ev) {
    case TIMEVAL_TV_SEC:
        *fieldptr = &timeval->TV_SEC;
        *size = timeval_sizes [TIMEVAL_TV_SEC];
        break;
    case TIMEVAL_TV_USEC:
        *fieldptr = &timeval->TV_USEC;
        *size = timeval_sizes [TIMEVAL_TV_USEC];
        break;
    default:
        return -EINVAL;
    }
    return 0;
    }

int metadata_enum_to_field(int ev, struct evfs_metadata *metadata, void **fieldptr, unsigned *size) {
    switch(ev) {
    case METADATA_MD_ID:
        *fieldptr = &metadata->MD_ID;
        *size = metadata_sizes [METADATA_MD_ID];
        break;
    case METADATA_START:
        *fieldptr = &metadata->START;
        *size = metadata_sizes [METADATA_START];
        break;
    case METADATA_LENGTH:
        *fieldptr = &metadata->LENGTH;
        *size = metadata_sizes [METADATA_LENGTH];
        break;
    default:
        return -EINVAL;
    }
    return 0;
    }

return 0;
}