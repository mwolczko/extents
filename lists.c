// a generic list-of-pointer-to-something

#include <assert.h>

#include "lists.h"
#include "mem.h"

unsigned n_elems(list *ps) { return ps->nelems; }

#define GET(ps, i) ((ps)->elems[i])

void *get(list *ps, unsigned i) { assert(n_elems(ps) > i); return GET(ps, i); }

void put(list *ps, unsigned i, void *e) { assert(n_elems(ps) > i); GET(ps, i)= e; }

bool is_empty(list *ps)     { return n_elems(ps) == 0; }

bool is_singleton(list *ps) { return n_elems(ps) == 1; }

bool is_multiple(list *ps)  { return n_elems(ps) > 1; }

void *first(list *ps) { assert(!is_empty(ps)); return GET(ps, 0); }

void *only(list *ps) { assert(is_singleton(ps)); return first(ps); }

void *last(list *ps) { return GET(ps, n_elems(ps) - 1); }

// -ve max_sz means growable, abs value is initial size
// Caution: if growable, elems[] can be realloc'ed when growing, so don't keep pointers into the array.
list *new_list(int max_sz) {
    assert(max_sz != 0);
    unsigned max_sz_abs= max_sz < 0 ? -max_sz : max_sz;
    list *ps= malloc_s(sizeof(list));
    ps->nelems= 0;
    ps->max_sz= max_sz;
    ps->elems= calloc_s(max_sz_abs, sizeof(void *));
    return ps;
}

list *append(list *ps, void *e) {
    assert(ps->max_sz < 0 || n_elems(ps) < ps->max_sz);
    if (ps->max_sz < 0 && n_elems(ps) == -ps->max_sz) {
        ps->max_sz *= 2;
        ps->elems= realloc_s(ps->elems, (-ps->max_sz) * sizeof(void *));
    }
    put(ps, ps->nelems++, e);
    return ps;
}
