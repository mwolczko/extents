#include <stdlib.h>
#include "fail.h"

void *malloc_s(size_t size)
{
  void *res= malloc(size);
  if (res == NULL)
    fail("malloc failed!\n");
  return res;
}

void *calloc_s(size_t n, size_t size)
{
  void *res= calloc(n, size);
  if (res == NULL)
    fail("calloc failed!\n");
  return res;
}

  
