# Extended VFS (EVFS)
Online implementation of evfs interface.

Paper link: https://www.usenix.org/system/files/conference/hotstorage18/hotstorage18-paper-sun.pdf

## Setup

Clone the repository with the following command:

`git clone --depth 1 --single-branch --branch main git@github.com:jacksun007/evfs-linux.git`

The above will make sure you only clone our active branch. Otherwise it may
take a very long time cloning the entire history of Linux.

Next, run `make menuconfig` to set up the kernel build. You will need to 
make F2FS "included" (it is modularized by default). You can do this by
going under File Systems, select F2FS and press "Y". You should see the
option go from `<M>` to `<*>`. It may be helpful to add a local release name. My personal choice is "-evfs". 

Then, compile the kernel. You may want to use as many CPUs as you have available
to expedite the build. e.g.,

```
make -j 4 && make -j 4 modules
```

Assuming the build was successful. Install the modules and the kernel.

```
sudo make modules_install
sudo make install 
```

Reboot and run the kernel that you just built. Run `uname -r` to make sure you are now running the kernel with evfs support.

## Code Organization

`include/uapi/linux/evfs.h` - where all common evfs structures and ioctl
commands are defined

`apps` - contains user level evfs applications

`fs` - contains generic vfs implementation

`fs/ext4` - contains ext4 implementation

`fs/f2fs` - contains f2fs implementation

## VFS Documentation

* Application Level File System Interface: https://www.gnu.org/software/libc/manual/html_node/File-System-Interface.html
* Overview of the Linux Virtual File System: https://www.kernel.org/doc/html/latest/filesystems/vfs.html


