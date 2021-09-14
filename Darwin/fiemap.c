// Functions to return extent info from the filesystem.
// Extents are faked on macOS from runs of contiguous blocks

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>

#include "fail.h"
#include "extents.h"
#include "mem.h"
#include "fiemap.h"

void flags2str(unsigned flags, char *s, size_t n, bool sharing) { s[0]= '\0'; }

static off_t l2p(unsigned fd, off_t off, off_t max, off_t *pcontig) {
    struct log2phys ph = {0, max, off};
    if (fcntl((int) fd, F_LOG2PHYS_EXT, &ph) >= 0) {
        if (pcontig != NULL) *pcontig = ph.l2p_contigbytes;
        return ph.l2p_devoffset;
    }
    if (errno == ERANGE)
        return -1; // hole
    fail("fcntl failed! %s\n", strerror(errno));
    return -2; // never reached
}

static void new_extent(fileinfo *pfi, off_t l, off_t p, off_t len) {
    pfi->n_exts++;
    pfi->exts= pfi->exts == NULL ? malloc_s(sizeof(extent)) : realloc(pfi->exts, pfi->n_exts * sizeof(extent));
    extent *new= &pfi->exts[pfi->n_exts - 1];
    new->info= pfi;
    new->l= l;
    new->p= p;
    new->len= len;
    new->flags= 0;
}

void get_extents(fileinfo *pfi, off_t max_len) {
    off_t limit= max_len > 0 ? pfi->skip + max_len : pfi->size;
    off_t off= roundDown(pfi->skip, blk_sz);
    while (off < limit) {
        off_t contig;
        off_t ph= l2p(pfi->fd, off, limit - off, &contig);
        if (ph >= 0) {
            if (contig <= 0) fail("contig not positive: %d\n", contig);
            new_extent(pfi, off, ph, contig);
            off += contig;
        } else { // skip over hole
            off= lseek((int) pfi->fd, off, SEEK_DATA);
            if (off < 0) fail("lseek failed: %d\n", strerror(errno));
        }
    }
}

bool flags_are_sane(unsigned flags) {
    return true; // no flags, no insanity
}
