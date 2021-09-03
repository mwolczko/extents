#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "fail.h"

bool fail_silently= false;

void fail(const char *fmt, ...) {
    if (!fail_silently) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
    exit(1);
}
