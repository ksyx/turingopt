#ifndef _TURING_MM_H
#define _TURING_MM_H
#include "interceptlib.h"
#include <sys/mman.h>
#include <dlfcn.h>

#ifdef AUDITLIB
#define OVERRIDEN_FUNC(NAME) intercepted_##NAME
#else
#define OVERRIDEN_FUNC(NAME) NAME
#endif

typedef void *(*mmap_t)(void *, size_t, int, int, int, off_t);

extern mmap_t mmap_orig;

/*LINKAGE
  void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
    throw();*/

LINKAGE size_t malloc_usable_size(void *);

#define OVERRIDE(RETTYPE, NAME, ...) \
  LINKAGE RETTYPE OVERRIDEN_FUNC(NAME)(__VA_ARGS__) throw(); \
  typedef RETTYPE(*NAME##_t)(__VA_ARGS__); \
  extern NAME##_t NAME##_orig;

#ifndef AUDITLIB
  #define OVERRIDE_NOSAMENAME(RETTYPE, NAME, ...) \
    LINKAGE RETTYPE __libc_##NAME(__VA_ARGS__); \
    OVERRIDE(RETTYPE, NAME, __VA_ARGS__);
#else
  #define OVERRIDE_NOSAMENAME(RETTYPE, NAME, ...) \
    OVERRIDE(RETTYPE, NAME, __VA_ARGS__);
#endif

OVERRIDE(int, munmap, void *addr, size_t len);
OVERRIDE_NOSAMENAME(void *, malloc, size_t len);
OVERRIDE_NOSAMENAME(void *, calloc, size_t nmemb, size_t len);
OVERRIDE_NOSAMENAME(void *, realloc, void *addr, size_t len);
OVERRIDE(void *, aligned_alloc, size_t alignment, size_t len);
OVERRIDE(int, posix_memalign, void **memptr, size_t alignment, size_t len);
OVERRIDE_NOSAMENAME(void *, memalign, size_t alignment, size_t len);
OVERRIDE_NOSAMENAME(void *, valloc, size_t len);
OVERRIDE_NOSAMENAME(void *, pvalloc, size_t len);
OVERRIDE_NOSAMENAME(void, free, void *ptr);

#undef OVERRIDE
#undef OVERRIDE_NOSAMENAME
#endif
