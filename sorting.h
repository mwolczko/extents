
#ifndef EXTENTS_SORTING_H
#define EXTENTS_SORTING_H

#include "lists.h"
#include "extents.h"
#include "sharing.h"

extern void log_sort(list *l);

int extent_list_cmp_phys(extent **pa, extent **pb);

extern void phys_sort_extents();

void fileno_sort(list *ps);

#endif //EXTENTS_SORTING_H
