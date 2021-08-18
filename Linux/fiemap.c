#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <linux/fiemap.h>
#include <linux/fs.h>

#include "../fail.h"

char *flags2str(int flags)
{
  char *s= malloc(1000); s[0]= '\0';
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

struct fiemap_extent *get_extents(int fd, int block_size)
{
  struct fiemap fm= { 0L, block_size, 0, 0, 0 };

  if (ioctl(fd, FS_IOC_FIEMAP, &fm) < 0)
    fail("Can't get extents : %s\n", strerror(errno));

  struct fiemap *fmp= malloc(sizeof(struct fiemap)
			      + fm.fm_mapped_extents * sizeof(struct fiemap_extent));
  if (fmp == NULL)
    fail("malloc failed!\n");

  fmp->fm_start= 0; fmp->fm_length= block_size; fmp->fm_flags= 0;
  fmp->fm_extent_count= fm.fm_mapped_extents;
  if (ioctl(fd, FS_IOC_FIEMAP, fmp) < 0)
    fail("Can't get list of extents : %s\n", strerror(errno));

  return &fmp->fm_extents[0];
}

