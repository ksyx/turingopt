#ifndef _TURING_MRECYCLE_H
#define _TURING_MRECYCLE_H
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct mrecycle_meta_t {
  atomic_size_t size;
  // flags = [0] in use [1-31] alignment
  atomic_size_t flags;
};

typedef struct mrecycle_info_t {
  void *addr;
  _Atomic struct mrecycle_meta_t metadata;
};

#define RECYCLE_POOL_SIZE_BITS 8
#define DO_TYPEDEF(BITS) \
  typedef atomic_uint_fast##BITS##_t recycle_pool_ptr_t;
#define DO_TYPEDEF_EXPAND(BITS) DO_TYPEDEF(BITS)
DO_TYPEDEF_EXPAND(RECYCLE_POOL_SIZE_BITS);
#undef DO_TYPEDEF
#undef DO_TYPEDEF_EXPAND
#define RECYCLE_POOL_SIZE 1 << RECYCLE_POOL_SIZE_BITS
const atomic_size_t enter_pool_threshold = 128 * 1024;

void __libc_free(void *ptr);
#include "mrecycle_interface.h"
#endif
