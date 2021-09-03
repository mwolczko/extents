#ifndef EXTENTS_H
#define EXTENTS_H

#include <sys/types.h>

typedef struct fileinfo fileinfo;
typedef struct extent extent; 
typedef struct list list;

// a raw extent from a specific file
struct extent {
  fileinfo *info;  // the file this belongs to
  off_t l;         // logical offset
  off_t p;         // physical offset on device
  off_t len;
  unsigned flags;
};

// description of file
struct fileinfo {
  char *name;         // filename
  unsigned argno;     // which file arg (starting at 0)
  unsigned fd;        // open fd during read
  off_t size;         // file size from stat(2)
  unsigned n_exts;    // # extents
  extent *exts;       // ptr to first extent in array of size n_exts
  list *unsh;         // unshared extents, list of sh_ext* (solely owned by this file)
  off_t skip;
};

struct list {
  unsigned nelems;
  int max_sz; // negative means growable
  void **elems;
};

#define min(a,b) ({                                     \
      __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); \
      _a < _b ? _a : _b; })

#define max(a,b) ({                                     \
      __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

extern blksize_t blk_sz;

#endif
