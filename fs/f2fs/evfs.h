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


#endif /* F2FS_EVFS_H_ */

