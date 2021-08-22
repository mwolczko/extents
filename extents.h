#ifndef EXTENTS_H
#define EXTENTS_H

#include <sys/types.h>

typedef struct fileinfo fileinfo;
typedef struct extent extent;
typedef struct list list;

struct extent {
  fileinfo *info;  // the file this belongs to
  off_t l;         // logical offset
  off_t p;         // physical offset on device
  off_t len;
  int flags;
  extent *nxt_sh;  // link to other extents which share data
};

struct fileinfo {
  char *name;    // filename
  int argno;     // which arg (starting at 0)
  int fd;        // open fd
  off_t size;    // file size from stat(2)
  int n_exts;    // # extents
  extent **exts; // ptr to array of extents
  list *unsh; // unshared extents
};

struct list {
  unsigned nelems, max_sz;
  void *elems[];
};

#endif
