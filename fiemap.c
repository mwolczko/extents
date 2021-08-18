
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fiemap.h>
#include <linux/fs.h>

void fail(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  exit(1);
}

char *flags2str(int flags)
{
  char *s = malloc(1000); s[0] = '\0';
  static int f[] = { FIEMAP_EXTENT_LAST,
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
  static char *nm[] = { "LAST",
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
  for (int i = 0; i < sizeof(f)/sizeof(int); ++i) {
    if (flags & f[i]) {
      strcat(s, nm[i]); strcat(s, " ");
    }
  }
  return s;
}


int main(int argc, char *argv[])
{
  char *fn = argv[1];

  int fd = open(fn, O_RDONLY);
  if (fd < 0)
    fail("open\n");

  struct stat sb;
  int statres = fstat(fd, &sb);
    
  struct fiemap fm = { 0L, sb.st_size, 0, 0, 0 };

  int fieres = ioctl(fd, FS_IOC_FIEMAP, &fm);
  if (fieres < 0)
    fail("fiemap %s\n", strerror(errno));

  struct fiemap *fmp = malloc(sizeof(struct fiemap)
			      + fm.fm_mapped_extents * sizeof(struct fiemap_extent));
  if (fmp == NULL)
    fail("malloc\n");

  fmp->fm_start = 0; fmp->fm_length = sb.st_size; fmp->fm_flags = 0;
  fmp->fm_extent_count = fm.fm_mapped_extents;
  int fieres2 = ioctl(fd, FS_IOC_FIEMAP, fmp);
  if (fieres2 < 0)
    fail("fiemap %s\n", strerror(errno));
  
  struct fiemap_extent *fme = &fmp->fm_extents[0];
  int e = 0;
  do {
    __u64 l = fme->fe_logical;
    __u64 p = fme->fe_physical;
    __u64 len = fme->fe_length;
    printf("%d %12lld..%12lld %12lld..%12lld %12lld %s\n", e++,
	   l, l+len-1, p, p+len-1, len, flags2str(fme->fe_flags));
  } while ((fme++->fe_flags & FIEMAP_EXTENT_LAST) == 0);

  return 0;
}
