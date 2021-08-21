#ifndef EXTENTS_H
#define EXTENTS_H

#include <sys/types.h>

typedef struct fileinfo fileinfo;
typedef struct extent extent;

struct extent {
  fileinfo *info;  // the file this belongs to
  off_t l;         // logical offset
  off_t p;         // physical offset on device
  off_t len;
  int flags;
  //extent *nxt;   // next extent for this file
  extent *nxt_sh;  // link to other extents which share data
  extent *nxt_tmp; // link used for scanning
};

struct fileinfo {
  char *name;    // filename
  int fd;        // open fd
  off_t size;    // file size from stat(2)
  int nexts;     // # extents
  extent **exts; // ptr to array of extents
};

#endif
