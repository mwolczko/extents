
#include <stdlib.h>

#include "sorting.h"

#ifndef linux
typedef int (* _Nonnull __compar_fn_t)(const void *, const void *);
#endif

static int sh_ext_list_cmp_log(sh_ext **a, sh_ext **b) {
    extent *fa= first((*a)->owners), *fb= first((*b)->owners);
    return fa->l > fb->l ? 1
         : fa->l < fb->l ? -1
         : 0;
}

void log_sort(list *l) {
    qsort(l->elems, l->nelems, sizeof(void *), (__compar_fn_t) &sh_ext_list_cmp_log);
}

int extent_list_cmp_phys(extent **pa, extent **pb)
{
    extent *a= *pa, *b= *pb;
    return a->p > b->p ? 1
         : a->p < b->p ? -1
         : a->len > b->len ? 1
         : a->len < b->len ? -1
         : 0;
}

void phys_sort() {
    qsort(&GET(extents, 0), n_ext, sizeof(extent *), (__compar_fn_t) &extent_list_cmp_phys);
}

static int extent_list_cmp_fileno(extent **a, extent **b) {
    return (*a)->info->argno > (*b)->info->argno ?  1
         : (*a)->info->argno < (*b)->info->argno ? -1
         : 0;
}

void fileno_sort(list *ps) {
    qsort(ps->elems, ps->nelems, sizeof(void *), (__compar_fn_t) &extent_list_cmp_fileno);
}
