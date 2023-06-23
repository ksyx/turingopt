#ifndef _TURING_PRELOADLIB_H
#define _TURING_PRELOADLIB_H
#include "dlmap.h"
#include "mm.h"

LINKAGE int __munmap(void *addr, size_t len) throw();
#endif
