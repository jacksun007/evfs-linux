# Extended VFS (EVFS)
Online implementation of evfs interface.

Paper link: https://www.usenix.org/system/files/conference/hotstorage18/hotstorage18-paper-sun.pdf

## Setup

Clone the repository with the following command:

`git clone --single-branch --branch main git@github.com:jacksun007/evfs-linux.git`

The above will make sure you only clone our active branch. Otherwise it may
take a very long time cloning the entire history of Linux.

## Code Organization

`include/uapi/linux/evfs.h` - where all common evfs structures and ioctl
commands are defined

`apps` - contains user level evfs applications

`fs` - contains generic vfs implementation

`fs/ext4` - contains ext4 implementation

`fs/f2fs` - contains f2fs implementation

## VFS Documentation

Application Level File System Interface: https://www.gnu.org/software/libc/manual/html_node/File-System-Interface.html
Overview of the Linux Virtual File System: https://www.kernel.org/doc/html/latest/filesystems/vfs.html


