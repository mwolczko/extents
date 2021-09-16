// Functions to return extent info from the filesystem.

// Mario Wolczko, Aug 2021

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <linux/fiemap.h>
#include <linux/fs.h>

#include "fail.h"
#include "extents.h"
#include "mem.h"
#include "fiemap.h"

void flags2str(unsigned flags, char *s, size_t n, bool sharing) {
    // This list copied from <fiemap.h>
    static unsigned f[] = {FIEMAP_EXTENT_LAST,
                           FIEMAP_EXTENT_UNKNOWN,
                           FIEMAP_EXTENT_DELALLOC,
                           FIEMAP_EXTENT_ENCODED,
                           FIEMAP_EXTENT_DATA_ENCRYPTED,
                           FIEMAP_EXTENT_NOT_ALIGNED,
                           FIEMAP_EXTENT_DATA_INLINE,
                           FIEMAP_EXTENT_DATA_TAIL,
                           FIEMAP_EXTENT_UNWRITTEN,
                           FIEMAP_EXTENT_MERGED,
                           FIEMAP_EXTENT_SHARED};
    static char *nm[] = {"LAST",
                         "UNKNOWN",
                         "DELALLOC",
                         "ENCODED",
                         "DATA_ENCRYPTED",
                         "NOT_ALIGNED",
                         "DATA_INLINE",
                         "DATA_TAIL",
                         "UNWRITTEN",
                         "MERGED",
                         "SHARED"};
    s[0]= '\0';
    if (sharing) flags &= ~FIEMAP_EXTENT_LAST;
    for (unsigned i= 0; i < sizeof(f) / sizeof(unsigned); ++i)
        if (flags & f[i]) {
            strncat(s, nm[i], n);
            n= max(n - strlen(nm[i]), 0);
            if (n > 0) strncat(s, " ", n--);
        }
    long l= strlen(s) - 1;
    if (l >= 0 && s[l] == ' ') s[l]= '\0';
}

bool flags_are_sane(unsigned flags) {
  return 0 == (flags & (FIEMAP_EXTENT_UNKNOWN
		      | FIEMAP_EXTENT_DELALLOC
		      | FIEMAP_EXTENT_ENCODED
		      | FIEMAP_EXTENT_NOT_ALIGNED
		      | FIEMAP_EXTENT_DATA_INLINE
		      | FIEMAP_EXTENT_DATA_TAIL
		      | FIEMAP_EXTENT_UNWRITTEN));
}

void get_extents(fileinfo *pfi, off_t max_len) {
    off_t start= roundDown(pfi->skip, blk_sz);
    off_t len= max_len > 0 ? max_len : pfi->size - pfi->skip;;
    struct fiemap fm= { (__u64)start, (__u64)len, 0L, 0L, 0 };
    if (ioctl((int) pfi->fd, FS_IOC_FIEMAP, &fm) < 0)
        fail("Can't get extents : %s\n", strerror(errno));
    unsigned n= fm.fm_mapped_extents;
    struct fiemap *pfm= malloc_s(sizeof(struct fiemap) + n * sizeof(struct fiemap_extent));
    pfm->fm_start= (__u64)start;
    pfm->fm_length= (__u64)len;
    pfm->fm_flags= (__u32)0;
    pfm->fm_extent_count= (__u32)n;
    if (ioctl((int)pfi->fd, FS_IOC_FIEMAP, pfm) < 0)
        fail("Can't get list of extents : %s\n", strerror(errno));
    if (pfm->fm_mapped_extents != n)
        fail("file is changing: %s; number of extents changed\n", pfi->name);
    extent *pe= calloc_s(n, sizeof(extent));
    pfi->n_exts= n;
    pfi->exts=  pe;
    struct fiemap_extent *pfe= &pfm->fm_extents[0];
    while (n-- > 0) {
        pe->info=  pfi;
        pe->l=     (off_t) pfe->fe_logical ;
        pe->p=     (off_t) pfe->fe_physical;
        pe->len=   (off_t) pfe->fe_length;
        pe->flags= pfe->fe_flags;
        ++pe; ++pfe;
    }
    free(pfm);
}

