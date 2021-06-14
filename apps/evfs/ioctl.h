/*
 * ioctl.h
 *
 * Defines all evfs ioctls
 *
 */

#ifndef IOCTL_H
#define IOCTL_H

#include <linux/ioctl.h>

#define FS_IOC_EXTENT_ITERATE _IOR('f', 80, struct evfs_iter_ops)
#define FS_IOC_INODE_ITERATE _IOR('f', 81, struct evfs_iter_ops)
#define FS_IOC_ATOMIC_ACTION _IOWR('f', 96, struct atomic_action_param)

#ifdef NDEBUG
#define debug(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
#define debug(fmt, ...)
#endif

#endif

