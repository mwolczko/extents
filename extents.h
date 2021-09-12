#ifndef EXTENTS_H
#define EXTENTS_H

#include <sys/types.h>
#include <stdbool.h>
#include "lists.h"

typedef struct fileinfo fileinfo;
typedef struct extent extent; 

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
    off_t skip;         // # of bytes to skip over
};

#define min(a,b) ({                                   \
      __typeof__ (a) _a= (a); __typeof__ (b) _b= (b); \
      _a < _b ? _a : _b; })

#define max(a,b) ({                                   \
      __typeof__ (a) _a= (a); __typeof__ (b) _b= (b); \
     _a > _b ? _a : _b; })

extern blksize_t blk_sz;

extern unsigned nfiles;
extern fileinfo *info; // ptr to array of files' info of size nfiles
extern unsigned n_ext; // # of extents in all files

extern list *extents; // list of all extent* from all files

extern off_t end_l(extent *e);

extern void check_all_extents_are_sane();

#endif
