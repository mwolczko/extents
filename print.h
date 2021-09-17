#ifndef EXTENTS_PRINT_H
#define EXTENTS_PRINT_H

#include <stdbool.h>
#include <sys/types.h>

// scanf/printf format for off_t
#ifdef linux
#define OFF_T "ld"
#else
#define OFF_T "lld"
#endif
#define FIELD "%" OFF_T


extern void print_extents_by_file();
extern void print_shared_extents();
extern void print_shared_extents_no_header();
extern void print_self_shared_extents();
extern void print_unshared_extents();
extern void print_cmp(off_t start, off_t len);
extern char *flag_pr(unsigned flags, bool sharing);
extern void print_file_key();

#endif //EXTENTS_PRINT_H
