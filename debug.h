#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdio.h>

#ifdef DEBUG_LIB
#define dprintf(...)   (void)fprintf(stderr, __VA_ARGS__)
#else
#define dprintf(...)
#endif

#endif
