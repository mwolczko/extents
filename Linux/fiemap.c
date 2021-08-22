#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <linux/fiemap.h>
#include <linux/fs.h>

#include "fail.h"
#include "extents.h"
#include "mem.h"

char *flags2str(int flags)
{
  char *s= malloc_s(1000); s[0]= '\0';
  // This list copied from <fiemap.h>
  static int f[]= { FIEMAP_EXTENT_LAST,
                    FIEMAP_EXTENT_UNKNOWN,
		    FIEMAP_EXTENT_DELALLOC,
		    FIEMAP_EXTENT_ENCODED,
		    FIEMAP_EXTENT_DATA_ENCRYPTED,
		    FIEMAP_EXTENT_NOT_ALIGNED,
		    FIEMAP_EXTENT_DATA_INLINE,
		    FIEMAP_EXTENT_DATA_TAIL,
		    FIEMAP_EXTENT_UNWRITTEN,
		    FIEMAP_EXTENT_MERGED,
		    FIEMAP_EXTENT_SHARED };
  static char *nm[]= { "LAST",
                       "UNKNOWN",
		       "DELALLOC",
		       "ENCODED",
		       "DATA_ENCRYPTED",
		       "NOT_ALIGNED",
		       "DATA_INLINE",
		       "DATA_TAIL",
		       "UNWRITTEN",
		       "MERGED",
		       "SHARED" };
  for (int i= 0; i < sizeof(f)/sizeof(int); ++i) {
    if (flags & f[i]) {
      strcat(s, nm[i]); strcat(s, " ");
    }
  }
  return s;
}

static struct fiemap_extent *get_linux_extents(int fd, int size, int *n)
{
  struct fiemap fm= { 0L, size, 0, 0, 0 };

  if (ioctl(fd, FS_IOC_FIEMAP, &fm) < 0)
    fail("Can't get extents : %s\n", strerror(errno));

  struct fiemap *fmp= malloc_s(sizeof(struct fiemap)
			      + fm.fm_mapped_extents * sizeof(struct fiemap_extent));
  fmp->fm_start= 0; fmp->fm_length= size; fmp->fm_flags= 0;
  fmp->fm_extent_count= fm.fm_mapped_extents;
  if (ioctl(fd, FS_IOC_FIEMAP, fmp) < 0)
    fail("Can't get list of extents : %s\n", strerror(errno));
  if (n != NULL)
    *n= fm.fm_mapped_extents;
  return &fmp->fm_extents[0];
}

extent *copy_extent(struct fiemap_extent *fe, fileinfo *ip)
{
  extent *e= malloc_s(sizeof(extent));
  e->info= ip;
  e->l= fe->fe_logical;
  e->p= fe->fe_physical;
  e->len= fe->fe_length;
  e->flags= fe->fe_flags;
  e->nxt_sh= NULL;
  return e;
}

void get_extents(fileinfo *ip)
{
  struct fiemap_extent *e= get_linux_extents(ip->fd, ip->size, &ip->n_exts);
  ip->exts = calloc_s(ip->n_exts, sizeof(extent *));
  for (int i= 0; i < ip->n_exts; ++i)
    ip->exts[i]= copy_extent(&e[i], ip);
}
