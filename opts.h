// Program options

#ifndef EXTENTS_OPTS_H
#define EXTENTS_OPTS_H

#include <stdbool.h>

extern bool
        print_flags,
        print_extents_only,
        print_shared_only,
        print_unshared_only,
        no_headers,
        print_phys_addr,
        cmp_output;

extern off_t max_cmp, skip1, skip2;

extern void args(int argc, char *argv[]);

#endif //EXTENTS_OPTS_H
