/*
 * Determine extent sharing -- find_shares()
 *
 * This algorithm takes a single list of all extents (variable: extents), and sorts it by physical address.
 * It then works through the list, comparing the current sh_ext (expressed in terms of its components) with the
 * next extent.  This can either 
 * (a) cause the current sh_ext to be finished (if it completely precedes the next extent),
 * (b) if the current sh_ext overlaps the next extent then the current is split into the part which precedes it and the 
 *     remainders; the remainders are put back in the work list, 
 * (c) if the current sh_ext begins at the same offset as the next extent but is shorter, the next extent is split and
 *     the first part merged into the current sh_ext, or 
 * (d) the current sh_ext and next extent are the same, and the next is merged into the current.
 */

#include <assert.h>
#include <string.h>

#include "extents.h"
#include "lists.h"
#include "mem.h"
#include "sharing.h"
#include "opts.h"
#include "sorting.h"

// components of the current shared extent being processed
static off_t start, len, end;
static list *owners; // list of extent*
static extent *cur_e;

list *shared;

unsigned total_unshared= 0;

static void append_owner(extent *e) { append(owners, e); }

static void new_owner(extent *e) {
    owners= new_list(-4);
    append_owner(e);
}

// next extent under consideration
static unsigned ei;
static extent *nxt_e; // == get(extents, ei), or NULL if at end

static void next_extent() {
    nxt_e= ++ei < n_elems(extents) ? get(extents, ei) : NULL;
}

static void begin_next() {
    cur_e= get(extents, ei);
    new_owner(cur_e);
    start= cur_e->p;
    len= cur_e->len;
    end= start + len;
    next_extent();
}

static sh_ext *new_sh_ext() {
    sh_ext *res= malloc_s(sizeof(sh_ext));
    res->p= start;
    res->len= len;
    res->owners= owners;
    return res;
}

static void add_to_unshared(sh_ext *sh) {
    if (is_singleton(sh->owners)) {
        extent *owner= only(sh->owners);
        append(owner->info->unsh, sh);
        total_unshared++;
    }
}

static void process_current() {
    assert(!is_empty(owners));
    sh_ext *s= new_sh_ext();
    bool is_sing= is_singleton(owners);
    if (is_sing || (cmp_output && skip1 != skip2))
        add_to_unshared(s);
    if (!is_sing)
        append(shared, s);
    if (ei < n_elems(extents)) begin_next();
}

static void swap_e(extent **a, extent **b) { extent *t= *a; *a= *b; *b= t; }

// add a new extent to the list in the right place
static void insert(extent *e) {
    unsigned i, n= n_elems(extents);
    for (i= ei; i < n && extent_list_cmp_phys(&e, (extent **) &GET(extents, i)) > 0; ++i)
        ;
    if (i == n) append(extents, e);
    else {
        extent *lst= last(extents);
        memmove(&GET(extents, i + 1), &GET(extents, i), (n - i - 1) * sizeof(extent *));
        put(extents, i, e);
        append(extents, lst);
    }
}

// the extent at ei has changed; move it to the right place to maintain sort order
static void re_sort() {
    extent **a, **b;
    for (unsigned i= ei;
         i < n_elems(extents) - 1
         && (a= (extent **) &GET(extents, i), b= (extent **) &GET(extents, i + 1), extent_list_cmp_phys(a, b) > 0);
         ++i)
        swap_e(a, b);
}

static extent *new_extent(fileinfo *pfi, off_t l, off_t p, off_t len, unsigned flags) {
    extent *res= malloc_s(sizeof(extent));
    res->info=    pfi;
    res->l=         l;
    res->p=         p;
    res->len=     len;
    res->flags= flags;
    return res;
}

void find_shares() {
    check_all_extents_are_sane();
    phys_sort_extents();
    shared= new_list(-10); // SWAG
    if (n_ext == 0) return;
    ei= 0;
    begin_next();
    while (nxt_e != NULL) {
        off_t start_nxt= nxt_e->p;
        if (start < start_nxt) {
            if (end > start_nxt) {
                len= start_nxt - start;
                off_t tail_len= end - start_nxt;
                // more efficient to insert all at once, since they all go at the same place. XXX
                ITER(owners, extent*, owner, {
                        extent *e= new_extent(owner->info, owner->l + len, start_nxt, tail_len, owner->flags);
                        insert(e);
                })
            }
            process_current();
        } else { // same start
            append_owner(nxt_e);
            off_t len_nxt= nxt_e->len;
            if (len < len_nxt) {
                len_nxt -= len;
                nxt_e->l += len;
                nxt_e->p += len;
                nxt_e->len= len_nxt;
                re_sort();
                nxt_e= get(extents, ei);
            } else  // same len
                next_extent();
        }
    }
    process_current();
}

extent *find_owner(sh_ext *s, unsigned i) {
    ITER(s->owners, extent*, e, {
        if (e->info->argno == i)
            return e;
    })
    return NULL;
}

