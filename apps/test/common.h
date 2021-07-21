#ifndef COMMON_H_
#define COMMON_H_

// NOTE: must include <evfs.h> first

const char * timevalstr(struct evfs_timeval * tv);
void print_inode(struct evfs_inode * inode);
void print_imap(struct evfs_imap *imap);
int create_data_file(const char * dir, const char * name);

#endif

