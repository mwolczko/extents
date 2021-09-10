// Functions to return extent info from the filesystem.
// Extents are faked on macOS from runs of contiguous blocks

// Mario Wolczko, Aug 2021

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>

#include "fail.h"
#include "extents.h"
#include "mem.h"
#include "fiemap.h"

void flags2str(unsigned flags, char *s, size_t n) { s[0]= '\0'; }

static off_t l2p(unsigned fd, off_t off) {
  struct log2phys ph= { 0, 0, off };
  if (fcntl((int)fd, F_LOG2PHYS_EXT, &ph) >= 0)
    return ph.l2p_devoffset;
  if (errno == ERANGE)
    return -1; // hole
  fail("fcntl failed! %s\n", strerror(errno));
  return -2; // never reached
}

static void new_extent(fileinfo *pfi, off_t l, off_t p, off_t len)
{
  pfi->n_exts++;
  pfi->exts= pfi->exts == NULL ? malloc_s(sizeof(extent)) : realloc(pfi->exts, pfi->n_exts * sizeof(extent));
  extent *new= &pfi->exts[pfi->n_exts - 1];
  new->info= pfi;
  new->l=      l;
  new->p=      p;
  new->len=  len;
  new->flags=  0;
}

void get_extents(fileinfo *pfi, off_t max_len)
{
  bool in= false;
  off_t l, p;
  off_t limit= max_len > 0 ? pfi->skip + max_len : pfi->size;
  for (off_t off= roundDown(pfi->skip, blk_sz) ;  off < limit;  off += blk_sz) {
    off_t ph= l2p(pfi->fd, off);
    if (in) {
      off_t len= off - l;
      if (ph < 0 || ph != p + len) {
    	in= false;
	    new_extent(pfi, l, p, len);
      }
    }
    if (!in && ph >= 0) {
      l= off;  p= ph;  in= true;
    }
  }
  if (in) new_extent(pfi, l, p, limit - l);
}

bool flags_are_sane(unsigned flags) {
    return true; // no flags, no insanity
}