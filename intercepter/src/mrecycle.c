#include "mrecycle.h"

static _Atomic struct mrecycle_info_t recycle_pool[RECYCLE_POOL_SIZE];
static recycle_pool_ptr_t pool_ptr = 0;
static const size_t RECYCLE_POOL_NFLAGBITS = 1;
static const size_t RECYCLE_POOL_IN_USE_BIT = 0x1;

bool recycle(void *addr, size_t size, size_t alignment) {
  if (size < enter_pool_threshold) return false;
  struct mrecycle_info_t newinfo = {
    .addr = addr,
    .metadata = {
      .size = size,
      .flags = alignment << RECYCLE_POOL_NFLAGBITS,
    },
  };
  recycle_pool_ptr_t slot =
    atomic_fetch_add_explicit(&pool_ptr, 1, memory_order_relaxed);
  newinfo = atomic_exchange_explicit(
    &recycle_pool[slot],
    newinfo,
    memory_order_relaxed);
  if (newinfo.addr && !(newinfo.metadata.flags & RECYCLE_POOL_IN_USE_BIT)) {
    __libc_free(newinfo.addr);
  }
  return true;
}

void *reuse(size_t size, size_t alignment) {
  if (size < enter_pool_threshold) return NULL;
  struct mrecycle_info_t match = {
    .metadata = {
      .size = size,
      .flags = alignment << RECYCLE_POOL_NFLAGBITS,
    },
    .addr = NULL,
  };
  struct mrecycle_info_t replacement = match;
  replacement.metadata.flags |= RECYCLE_POOL_IN_USE_BIT;
  recycle_pool_ptr_t start =
    atomic_load_explicit(&pool_ptr, memory_order_relaxed);
  recycle_pool_ptr_t cur = start - 1;
  for (; cur != start; cur--) {
    retry:;
    struct mrecycle_info_t info =
      atomic_load_explicit(&recycle_pool[cur], memory_order_relaxed);
    if (info.metadata.size == match.metadata.size
        && info.metadata.flags == match.metadata.flags) {
      match.addr = replacement.addr = info.addr;
      if (atomic_compare_exchange_strong_explicit(
        &recycle_pool[cur],
        &match,
        replacement,
        memory_order_relaxed,
        memory_order_relaxed
      )) {
        return info.addr;
      } else {
        goto retry;
      }
    } else if (!info.addr) {
      break;
    }
  }
  return NULL;
}
