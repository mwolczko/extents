// Functions to return extent info from the filesystem.

#include <stdbool.h>
#include "extents.h"

#define roundDown(a, b) ((a) / (b) * (b))

extern void flags2str(unsigned flags, char *s, size_t n, bool sharing);
extern void get_extents(fileinfo *ip, off_t max_len);
extern bool flags_are_sane(unsigned flags);
