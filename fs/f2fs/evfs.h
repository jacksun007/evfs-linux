/* 
 * fs/f2fs/evfs.h
 *
 * Implementation of the Evfs operations for F2FS.
 *
 * Copyright (c) 2021 University of Toronto
 *                    Kyo-Keun Park
 *                    Kuei (Jack) Sun
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef F2FS_EVFS_H_
#define F2FS_EVFS_H_

// segment.c
void update_sit_entry(struct f2fs_sb_info *sbi, block_t blkaddr, int del);
void __invalidate_blocks(struct f2fs_sb_info *sbi, block_t addr);
void change_curseg(struct f2fs_sb_info *sbi, int type, bool reuse);

// only dynamically-allocated metadata are captured here
typedef enum {
    F2FS_EVFS_DATA = 0,     // must always be 0
    F2FS_EVFS_INODE = 1,
    F2FS_EVFS_SIND = 2,     // single indirect (called direct in f2fs)
    F2FS_EVFS_DIND = 3,     // double indirect (called indirect in f2fs)
    F2FS_EVFS_TIND = 4,     // triple indirect (called double indirect)
    F2Fs_EVFS_DIR_BLOCK = 5,
} f2fs_evfs_type_t;

// for f2fs, we embed the temperature and the type together

static
inline
int f2fs_evfs_temperature(int type)
{
    return (type >> 8) & 0x000F;
}

static
inline
int f2fs_evfs_type(int type)
{
    return type & 0x000F;
}

#endif /* F2FS_EVFS_H_ */

