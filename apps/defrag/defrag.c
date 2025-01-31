/*
 * defrag.c
 *
 * Generic defragmentation tool built using Evfs API
 *
 */

#include <sys/stat.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <popt.h>
#include <evfs.h>

// turn on for debugging
// #define ALWAYS_DEFRAG

#define VERBOSE

#define eprintf(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

// return value of defragment function
#define NOT_FRAGMENTED 1
#define INODE_BUSY 2
#define NOT_REGULAR 3
#define NOT_FOUND 4
#define NOT_CHECKED 5
#define HAS_ERROR 6

#define CEILING(x,y) (((x) + (y) - 1) / (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

enum { OUT_OF_ORDER = 1, SMALL_EXTENT = 2 };

struct args {
    const char * devname;
    const char * filename;  // list of inode numbers
    int ino_nr;
    short algo_nr;
    short dry_run;
    time_t start_time;
} args = { NULL, NULL, 0, OUT_OF_ORDER, 0, 0 };

static long
atomic_inode_map(evfs_t * evfs, long ino_nr, struct evfs_imap * imap,
                 struct evfs_timeval * mtime);

static int
usage(poptContext optCon, int exitcode, char *error, char *addl) 
{
    poptPrintUsage(optCon, stderr, 0);
    if (error) fprintf(stderr, "%s: %s\n", error, addl);
    exit(exitcode);
    return 0;
}

static
long
check_out_of_order(struct evfs_imap * imap, struct evfs_super_block * sb, 
                   struct evfs_inode * iptr)
{ 
    int ret = 0;
    int num_out_of_order = 0;
    u64 end = 0;
    unsigned i;

    // current algorithm just checks if there are any extents that are not
    // in monotonically increasing order
    for (i = 0; i < imap->count; i++) {
        struct evfs_imentry * e = &imap->entry[i];     
        
        // do not defrag any file with inlined data
        if (e->inlined) {
            return 0;
        }
        
        if (end > e->phy_addr)
            num_out_of_order++;
        
        end = e->phy_addr + e->len;
    }

#ifdef ALWAYS_DEFRAG
    ret = 1;
#else    
    if (num_out_of_order > 0)
        ret = 1;
#endif
    
    // don't need to consult either here
    (void)iptr;
    (void)sb;
    return ret;
}

static
long
check_small_extents(struct evfs_imap * imap, struct evfs_super_block * sb, 
                    struct evfs_inode * iptr)
{
    int ret = 0;
    unsigned i;
    u64 max_extent_bytesize = sb->max_extent_size * sb->block_size;
    u64 min_num_extents = CEILING(iptr->bytesize, max_extent_bytesize);

    //printf("filesize: %lu, maxsize: %lu, min: %lu, count: %u\n",
    //    iptr->bytesize, max_extent_bytesize, min_num_extents, imap->count);

    if (imap->count > min_num_extents) {
        ret = 1;
        
        for (i = 0; i < imap->count; i++) {
            struct evfs_imentry * e = &imap->entry[i];     
            
            // do not defrag any file with inlined data
            if (e->inlined) {
                ret = 0;
                break;
            }
        }
    }
    else if (iptr->bytesize > max_extent_bytesize) {
        int small_count = 0;
        ret = 0;
    
        for (i = 0; i < imap->count; i++) {
            struct evfs_imentry * e = &imap->entry[i];     
            
            // this shouldn't be possible except for ReiserFS
            if (e->inlined) {
                break;
            }
            
            // if more than 1 extent is smaller than a segment
            // we will defragment this file
            if (e->len < sb->max_extent_size) {
                printf("large file %lu, small extent %lu block(s)\n", 
                    iptr->bytesize, e->len);
                       
                if (++small_count > 1) {
                    ret = 1;
                    break;
                }
            }
        }
    }

    return ret;
}


// 1 for yes, 0 for no
static
long 
should_defragment(evfs_t * evfs, struct evfs_super_block * sb, struct evfs_inode * iptr)
{
    struct evfs_imap * imap;
    long ret = 0;
    
    imap = imap_info(evfs, iptr->ino_nr);
    if (!imap) {
        eprintf("warning: imap_info failed on inode %lu\n", iptr->ino_nr);
        return 0;
    } 
    
    switch (args.algo_nr) {
    case OUT_OF_ORDER:
        ret = check_out_of_order(imap, sb, iptr);
        break;
    case SMALL_EXTENT:
        ret = check_small_extents(imap, sb, iptr);
        break;
    default:
        eprintf("warning: unknown algorithm #%d\n", args.algo_nr);
        ret = 0;
        break;
    }

    imap_free(imap);
    return ret;
}

static double alloc_time = 0.;
static double copy_time = 0.;
static double map_time = 0.;

static double timespec_subtract(struct timespec * a, struct timespec * b)
{
    double x = a->tv_sec + (double)a->tv_nsec / 1000000000;
    double y = b->tv_sec + (double)b->tv_nsec / 1000000000;
    return x - y;
}

long defragment(evfs_t * evfs, struct evfs_super_block * sb, struct evfs_inode * inode)
{
    char * data = NULL;
    long ret;
    u64 poff, loff = 0;
    u64 nr_blocks;
    struct evfs_imap * imap = NULL;
    u64 extent_size;
    u64 byte_size;
    struct timespec start, end;
          
#ifdef VERBOSE
    printf("Defragmenting inode %lu, size = %lu\n", inode->ino_nr, inode->bytesize); 
#endif   
    
    // calculate how many blocks need to be allocated
    imap = imap_new(evfs);
    nr_blocks = CEILING(inode->bytesize, sb->block_size);       
    extent_size = MIN(nr_blocks, sb->max_extent_size);
    byte_size = extent_size * sb->block_size;
    
    // set up buffer for copying data to new extents   
    data = malloc(byte_size);
    if (!data) {
        ret = -ENOMEM;
        eprintf("malloc(%lu) failed during defrag\n", byte_size);
        goto done;
    }      
    
    // start allocating new contiguous extents. we have to deal with the
    // possibility that the file system has a maximum contiguous extent limit 
    while (nr_blocks > 0) {
        do {
            clock_gettime(CLOCK_REALTIME, &start);
            ret = extent_alloc(evfs, 0, extent_size, 0);
            poff = (u64)ret;
            clock_gettime(CLOCK_REALTIME, &end);
            alloc_time += timespec_subtract(&end, &start);
            
            if (ret == 0 || ret == -ENOSPC) {
                eprintf("warning: extent_alloc could not allocate %lu blocks\n",
                        extent_size);
                    
                ret = 0;
                extent_size /= 2;
                byte_size /= 2;
            
                if (extent_size <= 0) {
                    ret = -ENOSPC;
                    goto done;
                }
            }
            else if (ret == -ENOSYS) {
                ret = HAS_ERROR;
                goto done;
            }
            else if (ret < 0) {
                eprintf("extent_alloc: %s\n", strerror(-ret));
                goto done;
            }
        } while (ret == 0);
        
        clock_gettime(CLOCK_REALTIME, &start);
        ret = imap_append(&imap, loff, poff, extent_size);
        if (ret < 0) {
            eprintf("imap_append: %s\n", strerror(-ret));
            goto done;
        }
        
        ret = inode_read(evfs, inode->ino_nr, loff * sb->block_size, data, byte_size);
        if (ret < 0) {
            eprintf("inode_read: %s\n", strerror(-ret));
            goto done;
        }

        const u64 intra_block_offset = 0;
        ret = extent_write(evfs, poff, intra_block_offset, data, byte_size);
        if (ret < 0) {
            eprintf("extent_write: %s\n", strerror(-ret));
            goto done;
        }
        
        clock_gettime(CLOCK_REALTIME, &end);
        copy_time += timespec_subtract(&end, &start);
      
        // prepare for next iteration  
        nr_blocks -= extent_size;
        loff += extent_size;
        extent_size = MIN(nr_blocks, sb->max_extent_size);
        byte_size = extent_size * sb->block_size;
    } 
    
    // atomic_execute returns positive value if atomic action is cancelled 
    // due to failed comparison
    clock_gettime(CLOCK_REALTIME, &start);
    ret = atomic_inode_map(evfs, inode->ino_nr, imap, &inode->mtime);
    if (ret > 0)
        ret = INODE_BUSY;
    clock_gettime(CLOCK_REALTIME, &end);
    map_time += timespec_subtract(&end, &start);

done:    
    free(data);
    imap_free(imap);
    return ret;
}


long try_defragment(evfs_t * evfs, struct evfs_super_block * sb, unsigned long ino_nr)
{
    struct evfs_inode inode;
    int ret;

    inode.ino_nr = ino_nr;
    if ((ret = inode_info(evfs, &inode)) < 0) {
        if (ret == -ENOENT)
            return NOT_FOUND;
        return ret;
    }

    // do not defrag anything created after start time
    if (inode.ctime.tv_sec > (u64)args.start_time)
        return NOT_CHECKED;

    // TODO: need this for now because f2fs does not support directory fiemap
    if (!S_ISREG(inode.mode) || inode.prop.inlined_bytes) {
        return NOT_REGULAR;
    }

    if (!should_defragment(evfs, sb, &inode)) {
        return NOT_FRAGMENTED;
    }
    
    if (args.dry_run) {
        return 0;
    }
    
    return defragment(evfs, sb, &inode);  
}

long always_defragment(evfs_t * evfs, struct evfs_super_block * sb, unsigned long ino_nr)
{
    struct evfs_inode inode;
    int ret;

    inode.ino_nr = ino_nr;
    if ((ret = inode_info(evfs, &inode)) < 0) {
        if (ret == -ENOENT)
            return NOT_FOUND;
        return ret;
    }

    return defragment(evfs, sb, &inode);  
}

long defragment_all(evfs_t * evfs, struct evfs_super_block * sb)
{
    evfs_iter_t * it = inode_iter(evfs, 0);
    int ret = 0, cnt = 0, nf = 0, ib = 0, total = 0, nc = 0, err = 0;
    unsigned long ino_nr;
    
    while ((ino_nr = inode_next(it)) > 0) {
        ret = try_defragment(evfs, sb, ino_nr);
        total++;

        if (ret < 0) {
            eprintf("error while defragmenting inode %ld, %s\n", ino_nr,
                strerror(-ret));
            break;
        }
        else if (ret == 0)
            cnt++;
        else if (ret == NOT_FRAGMENTED)
            nf++;
        else if (ret == INODE_BUSY)
            ib++;
        else if (ret == NOT_REGULAR)
            total--;
        else if (ret == NOT_CHECKED)
            nc++;
        else if (ret == HAS_ERROR) {
            eprintf("warning: could not defragment inode %ld\n", ino_nr);
            err++;
        }
        else if (ret == NOT_FOUND) {
            eprintf("warning: inode removed between inode_iter and inode_info\n");
        }
        else {
            eprintf("error: unknown return value from defragment()\n");
            ret = -EINVAL;
            break;
        }
    }
    
    iter_end(it);
    printf("%d inode(s) scanned. %d defragmented, %d not fragmented, %d busy, "
        "%d ignored, %d error\n", total, cnt, nf, ib, nc, err);
    return ret;
}

long defragment_some(evfs_t * evfs, struct evfs_super_block * sb, FILE * fp)
{
    char * line = NULL;
    size_t len = 0;
    int ret = 0, total = 0;
    
    while (getline(&line, &len, fp) != -1) {
        int ino_nr = atoi(line);
        
        if (ino_nr <= 0) {
            fprintf(stderr, "invalid inode number %s\n", line);
        }
        
        ret = always_defragment(evfs, sb, ino_nr);
        total++;

        if (ret < 0) {
            fprintf(stderr, "error while defragmenting inode %d\n", ino_nr);
            break;
        }
    }
    
    if (line) {
        free(line);
    }
    
    printf("%d inode(s) defragmented.\n", total);
    return ret;
}

int main(int argc, const char * argv[])
{
    evfs_t * evfs = NULL;
    struct evfs_super_block sb;
    const char * temp;
    long ret;
    int c;

    poptContext optcon;   /* context for parsing command-line options */
    struct poptOption opttab[] = {
        { "out-of-order", 'o', 0, 0, 'o', "use out-of-order algorithm", NULL },
        { "small-extent", 's', 0, 0, 's', "use small extent algorithm", NULL },
        { "dry-run", 'd', 0, 0, 'd', "do not actually defragment", NULL },
        { NULL,  'f', POPT_ARG_STRING, &args.filename, 0, 
                 "specify file with inode numbers", NULL},
        POPT_AUTOHELP
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };
    optcon = poptGetContext(NULL, argc, argv, opttab, 0);
    poptSetOtherOptionHelp(optcon, "DEV [NUM]");
    
    if (argc < 2) {
	    return usage(optcon, 1, NULL, NULL);
    }
    
    while ((c = poptGetNextOpt(optcon)) >= 0) {
        switch (c) {
        case 'o':
            args.algo_nr = OUT_OF_ORDER;
            break;
        case 's':
            args.algo_nr = SMALL_EXTENT;
            break;
        case 'd':
            printf("%s: dry run activated\n", argv[0]);
            args.dry_run = 1;
            break;
        default:
            break;
        }
    } 
    
    if ((args.devname = poptGetArg(optcon)) == NULL) {
        return usage(optcon, 1, "Must specify mount point", "e.g., ~/my-disk");
    }
    
    if ((temp = poptGetArg(optcon)) != NULL) {
        args.ino_nr = atoi(temp);
    }    
    
    args.start_time = time(NULL);
    evfs = evfs_open(args.devname);
    if (evfs == NULL) {
        eprintf("Error: evfs_open failed\n");
        goto done;
    }
    
    if ((ret = super_info(evfs, &sb)) < 0) {
        eprintf("Error: could not retrieve super block info: %s\n",
		strerror(-ret));
        goto done;
    }
    
    if (args.filename != NULL) {
        FILE * f = fopen(args.filename, "rt");
        
        if (!f) {
            eprintf("Error: could not open %s\n", args.filename);
        }
        else {
            ret = defragment_some(evfs, &sb, f);
            fclose(f);
        }
    }
    else if (args.ino_nr == 0) {
        ret = defragment_all(evfs, &sb);
    }
    else {
        ret = always_defragment(evfs, &sb, args.ino_nr);
    }
    
    printf("alloc_time: %lf\ncopy_time: %lf\nmap_time: %lf\n",
        alloc_time, copy_time, map_time);
    
done:    
    evfs_close(evfs);
    return (int)ret;
}

static long 
atomic_inode_map(evfs_t * evfs, long ino_nr, struct evfs_imap * imap,
                 struct evfs_timeval * mtime)
{
    long ret, id;
    struct evfs_atomic * aa = atomic_begin(evfs);
    struct evfs_inode inode; 
    
    if (aa == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    
    inode.ino_nr = ino_nr;
    id = inode_info(aa, &inode);
    if (id < 0) {
        ret = id;
        eprintf("inode_info (atomic): %s\n", strerror(-ret));
        goto fail;
    }
    
    ret = atomic_const_equal(aa, id, EVFS_INODE_MTIME_TV_SEC, mtime->tv_sec);
    if (ret < 0) {
        goto fail;
    }
    
    ret = atomic_const_equal(aa, id, EVFS_INODE_MTIME_TV_USEC, mtime->tv_usec);
    if (ret < 0) {
        goto fail;
    }

    ret = inode_map(aa, ino_nr, imap);
    if (ret < 0) {
        goto fail;
    }
    
    ret = atomic_execute(aa);
    if (ret < 0) {
        eprintf("atomic_execute: %s\n", strerror(-ret));
    }
    
fail:
    atomic_end(aa);
    return ret;
}


