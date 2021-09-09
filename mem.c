#include <stdlib.h>
#include "fail.h"

void *malloc_s(size_t size)
{
  void *res= malloc(size);
  if (res == NULL)
    fail("malloc failed!\n");
  return res;
}

void *alloca_s(size_t size)
{
  void *res= alloca(size);
  if (res == NULL)
    fail("alloca failed!\n");
  return res;
}

void *calloc_s(size_t n, size_t size)
{
  if (n == 0) return NULL;
  void *res= calloc(n, size);
  if (res == NULL)
    fail("calloc failed!\n");
  return res;
}

void *realloc_s(void *mem, size_t size)
{
    void *res= realloc(mem, size);
    if (res == NULL)
        fail("realloc failed!\n");
    return res;
}
