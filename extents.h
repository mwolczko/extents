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
  unsigned flags;
  extent *nxt_sh;  // link to other extents which share data
};

struct fileinfo {
  char *name;    // filename
  unsigned argno;     // which arg (starting at 0)
  unsigned fd;        // open fd
  off_t size;    // file size from stat(2)
  unsigned n_exts;    // # extents
  extent **exts; // ptr to array of extents
  list *unsh; // unshared extents
};

struct list {
  unsigned nelems;
  int max_sz; // negative means growable
  void *elems[];
};

#endif
