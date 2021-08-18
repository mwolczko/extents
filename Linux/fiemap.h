#include <linux/fiemap.h>

extern char *flags2str(int flags);
extern struct fiemap_extent *get_extents(int fd, int block_size);
