// allocator wrappers which fail() if allocation fails

extern void *malloc_s(size_t size);
extern void *calloc_s(size_t n, size_t size);
extern void *alloca_s(size_t size);
