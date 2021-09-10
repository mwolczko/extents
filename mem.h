// allocator wrappers which fail() if allocation fails

#include <sys/types.h>

extern void *malloc_s(size_t size);
extern void *calloc_s(size_t n, size_t size);
extern void *realloc_s(void *m, size_t size);
