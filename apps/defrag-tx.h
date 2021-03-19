#include <stddef.h>
#include <stdint.h>

// Defnitions for eVFS-FS mounting options
#define EVFS_MNT_RDONLY 0
#define EVFS_MNT_WRITE 1
#define EVFS_MNT_RDWRITE 2

// Definitions of evfs_tx_read types
#define EVFS_INODE 0

// Definitions of eVFS TX comparators
#define EVFS_INT_EQ 0

// Definitions of eVFS TX inode properties
#define EVFS_I_SIZE 0

#define EVFS_FIELD(id, field) 0
#define EVFS_INT(x) x

struct evfs_mount {
	char *name;
	int mode;
};

struct evfs {};

// Minitransaction
struct evfs_tx {};

// Struct to reference FS structures
struct evfs_super {
	/* Ported from evfs.h */
	uint64_t max_extent; /* maximum allowed size of a given extent */
	uint64_t max_bytes; /* max file size */
	uint64_t page_size; /* page size used for extent granularity */
	uint64_t root_ino; /* root inode number */
};

/* TODO: compact this structure */
struct evfs_inode_property {
    int inlined;        // does inode have inlined data
    int refcount;       // link count
    long blockcount;    // number of blocks used
    long bytesize;      // size of inode, in bytes
};

/* TODO: replace with kernel's timespec? */
struct evfs_timeval {
    long tv_sec;
    long tv_usec;
};

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

struct evfs_dirent {};

// Struct used for data struct iteration
struct evfs_extent_iter {
	long (*cb)(struct evfs *fs, uint64_t log_blk_nr, uint64_t phy_blk_nr,
			uint64_t length, void * priv);
};

struct evfs_inode_iter {
	long (*cb)(struct evfs *fs, uint64_t ino_nr, struct evfs_inode *i,
			void * priv);
};

struct evfs_dirent_iter {};

/*
 * Definition of all minitransaction-based eVFS API.
 *
 * Below functions are described from Jack Sun's revised online eVFS paper.
 *
 * TODO (kyokeun): These are currently assumed to be a 'convenience function'
 *                 (refer to online eVFS draft paper).
 */

struct evfs * evfs_open(struct evfs_mount * mnt) { return NULL; }
int fs_close(struct evfs * evfs) { return 0; }

// Super block operations
int super_make(struct evfs * fs, struct evfs_super * sup) { return 0; }
int super_get(struct evfs * fs, struct evfs_super * sup) { return 0; }
int super_set(struct evfs * fs, const struct evfs_super * sup) { return 0; }

// Extent operations
uint64_t extent_alloc(struct evfs * fs, uint64_t addr, uint64_t len, int flags) { return 0; }
int extent_free(struct evfs * fs, uint64_t addr, uint64_t len) { return 0; }
int extent_active(struct evfs * fs, uint64_t addr, uint64_t len) { return 0; }
int extent_read(struct evfs * fs, uint64_t addr, uint64_t len, char * data, uint64_t size) { return 0; }
int extent_write(struct evfs * fs, uint64_t addr, uint64_t len) { return 0; }
int freesp_iterate(struct evfs * fs, struct evfs_extent_iter * eit) { return 0; }
int extent_iterate(struct evfs * fs, struct evfs_extent_iter * eit) { return 0; }

// Inode operations
int64_t inode_alloc(struct evfs * fs, int64_t ino_nr, struct evfs_inode * ino) { return 0; }
int inode_free(struct evfs * fs, int64_t ino_nr) { return 0; }
int inode_active(struct evfs * fs, int64_t ino_nr) { return 0; }
int inode_get(struct evfs * fs, int64_t ino_nr, struct evfs_inode * ino) { return 0; }
int inode_set(struct evfs * fs, int64_t ino_nr,
		const struct evfs_inode * ino) { return 0; }
int inode_read(struct evfs * fs, int64_t ino_nr, int64_t ofs,
		char * data, uint64_t size) { return 0; }
int inode_iterate(struct evfs * fs, struct evfs_inode_iter * iit) { return 0; }

// Dirent operations
int dirent_add(struct evfs * fs, int64_t dir_nr, struct evfs_dirent * d) { return 0; }
int dirent_remove(struct evfs * fs, int64_t dir_nr, const char * name) { return 0; }
int dirent_update(struct evfs * fs, int64_t dir_nr, struct evfs_dirent * d) { return 0; }
struct evfs_dirent_iter * dirent_iterate(struct evfs * fs, struct evfs_dirent_iter * dit) { return 0; }

/*
 * Below functions are not described yet, but found necessary in order to
 * correctly implement defrag tool with eVFS interface
 */

/*
 * Minitransaction related functions.
 *
 * Not described within eVFS API, but we expect user to fully leverage such
 * functionality, hence it should be exposed to the user.
 */
struct evfs_tx * evfs_new_tx(struct evfs * fs) { return NULL; }
int evfs_tx_read(struct evfs_tx * tx, int type, int value) { return 0; }
int evfs_tx_result(struct evfs_tx * tx, int wid) { return 0; }
int evfs_tx_compare(struct evfs_tx * tx, int comparator, int field1, int field2) { return 0; }
int evfs_tx_commit(struct evfs_tx * tx) { return 0; }
void evfs_tx_free(struct evfs_tx * tx) {}
void evfs_tx_inode_map(struct evfs_tx * tx, uint64_t ino_nr, uint64_t log_blk_nr,
		uint64_t phy_blk_nr, uint64_t len) { return 0; }

// transactional variant of the provided API (?)
void evfs_tx_inode_read(struct evfs_tx * tx, uint64_t ino_nr, uint64_t ofs,
		char * data, uint64_t len) { return 0; }
void evfs_tx_inode_unmap(struct evfs_tx * tx, uint64_t ino_nr,
		uint64_t log_blk_nr, uint64_t len) { return 0; }
void evfs_tx_extent_write(struct evfs_tx * tx, uint64_t blk_nr, uint64_t len,
		char * data) { return 0; }
