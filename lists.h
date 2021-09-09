// a generic list-of-pointer-to-something

#ifndef EXTENTS_LISTS_H
#define EXTENTS_LISTS_H

#include <stdbool.h>

typedef struct list list;

struct list {
    unsigned nelems;
    int max_sz; // negative means growable
    void **elems;
};

#define ITER(l, EL_T, elem, stmt) {             \
  list *_l= (l);                                \
  for (unsigned _i= 0; _i < _l->nelems; ++_i) { \
    EL_T (elem)= get(_l, _i);                   \
    do { stmt; } while (0);                     \
  }}

extern unsigned n_elems(list *ps);

// use this if you need the l-value of an element
#define GET(ps, i) ((ps)->elems[i])

extern void *get(list *ps, unsigned i);

extern void put(list *ps, unsigned i, void *e);

extern bool is_empty(list *ps);

extern bool is_singleton(list *ps);

extern bool is_multiple(list *ps);

extern void *first(list *ps);

extern void *only(list *ps);

extern void *last(list *ps);

// -ve max_sz means growable, abs value is initial size
// Caution: if growable, elems[] can be realloc'ed when growing, so don't keep pointers into the array.
extern list *new_list(int max_sz);

extern list *append(list *ps, void *e);

#endif //EXTENTS_LISTS_H
