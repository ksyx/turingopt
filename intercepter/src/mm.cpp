#include "mm.h"

int OVERRIDEN_FUNC(munmap)(void *addr, size_t len) throw() {
  DEBUGOUT(fprintf(stderr, "munmap: %p %ld\n", addr, len));
  return madvise(addr, len, MADV_DONTNEED);
}

#define SETUP(NAME) NAME##_t NAME##_orig;

SETUP(mmap);
SETUP(munmap);
SETUP(free);
SETUP(malloc);
SETUP(calloc);
SETUP(realloc);
SETUP(valloc);
SETUP(pvalloc);
SETUP(memalign);
SETUP(posix_memalign);

#ifndef AUDITLIB
#define SETUP(NAME) \
  if (!NAME##_orig) { \
    NAME##_orig = __libc_##NAME; \
  }
#else
#define SETUP(NAME) ;
#endif

constexpr size_t FLAGSMASK = 0b111;
constexpr auto NODIFF_BIT = 0x1;
constexpr auto MMAP_BIT = 0x2;
constexpr auto COMPRESSED_BIT = 0x4;
constexpr size_t MMAPCHUNK_HEADERSIZE = 2 * sizeof(size_t);

static inline bool is_real_mmaped_chunk(void *chunk_start) {
  auto *head_ptr = (const size_t *) chunk_start;
  const auto size = *(head_ptr - 1);
  return size & MMAP_BIT &&
    (size & ~FLAGSMASK) - malloc_usable_size(chunk_start) == MMAPCHUNK_HEADERSIZE;
}

struct implant_advice {
  uint8_t size_diff;
  void *addr;
};

/*
@returns ret
  ret.size_diff
    When nonzero, there is less than sizeof(size_t) bytes of space to store
    requested size but had at least one byte of space at the specified location.
    Store the diff = ret.mmap_size - user_len instead, since 2^(diff*8) should
    be greater than sizeof(size_t) and even one extra byte is enough for
    restoring requested size.
  ret.addr
    chunk_start - Set ((uint8_t *)chunk_start) - 1 to true, indicating that the
                  requested size is ret.mmap_size - 2 * sizeof(size_t), in which
                  case there is no place to implant such information
    NULL - Not suitable for implantation and no action should be taken
    otherwise - Implant at returned address. Store user_len when ret.size_diff
                is zero and size_diff otherwise, with their respective types.
@notes
  The implant address would be at either start or tail of the mmaped region to
  help locating the data.
*/
static inline
implant_advice get_chunk_implant_advice(void *chunk_start, size_t user_len) {
  auto *head_ptr = (const size_t *) chunk_start;
  if (is_real_mmaped_chunk(chunk_start)) {
    const size_t *size_ptr = head_ptr - 1;
    const size_t *padding_size_ptr = size_ptr - 1;
    // size = mmaped_size - (padding_size below)
    const size_t size = *size_ptr & ~FLAGSMASK;
    const size_t padding_size = *padding_size_ptr;
    const size_t diff = size - (user_len + MMAPCHUNK_HEADERSIZE);
    if (!diff) {
      return {0, chunk_start};
    }
    if (padding_size) {
      // Priortize this place to avoid yet another page fault
      const auto mmap_start = (void *) padding_size_ptr - padding_size;
      if (padding_size >= sizeof(size_t)) {
        return {0, mmap_start};
      } else if (diff < 256) {
        return {diff, mmap_start};
      }
    }
    const auto mmap_tail = chunk_start + user_len + diff;
    if (diff < sizeof(size_t)) {
      return {diff, mmap_tail - sizeof(uint8_t)};
    } else {
      return {0, mmap_tail - sizeof(size_t)};
    }
  } else {
    return {0, NULL};
  }
}

static inline void implant_chunk(void *chunk_start, size_t user_len) {
  const auto advice = get_chunk_implant_advice(chunk_start, user_len);
  DEBUGOUT(
    fprintf(stderr, "implant_chunk: %p %ld => [%d %p]\n",
      chunk_start, user_len, advice.size_diff, advice.addr);
  );
  if (!advice.addr) {
    return;
  }
  if (advice.size_diff) {
    *(uint8_t *)advice.addr = advice.size_diff;
    *((uint8_t *) chunk_start - 1) |= COMPRESSED_BIT;
  } else if (advice.addr != chunk_start) {
    *(size_t *) advice.addr = user_len;
  } else {
    *((uint8_t *) chunk_start - 1) |= NODIFF_BIT;
  }
}

static inline size_t fetch_implanted(void *chunk_start) {
  auto *head_ptr = (const size_t *) chunk_start;
  if (chunk_start && is_real_mmaped_chunk(chunk_start)) {
    const size_t *size_ptr = head_ptr - 1;
    const size_t *padding_size_ptr = size_ptr - 1;
    const size_t flags = *size_ptr & FLAGSMASK;
    // size = mmaped_size - (padding_size below)
    const size_t size = *size_ptr & ~FLAGSMASK;
    const size_t padding_size = *padding_size_ptr;
    const auto mmap_start = (void *) padding_size_ptr - padding_size;
    const auto mmap_tail = mmap_start + padding_size + size;
    size_t diff = 0;
    if (padding_size >= sizeof(size_t)) {
      DEBUGOUT(fprintf(stderr, "fetch_from_padding: %p\n", chunk_start));
      return *(size_t *) mmap_start;
    } else if (padding_size) {
      DEBUGOUT(fprintf(stderr, "fetch_from_padding_compress: %p\n", chunk_start));
      diff = *(uint8_t *) mmap_start;
    } else if (flags & COMPRESSED_BIT) {
      DEBUGOUT(fprintf(stderr, "fetch_from_tail_compress: %p\n", chunk_start));
      diff = *((uint8_t *) mmap_tail - 1);
    } else if (!(flags & NODIFF_BIT)) {
      DEBUGOUT(fprintf(stderr, "fetch_from_tail: %p\n", chunk_start));
      return *((size_t *) mmap_tail - 1);
    } else {
      DEBUGOUT(fprintf(stderr, "fetch_exact: %p\n", chunk_start));
    }
    return size - diff - MMAPCHUNK_HEADERSIZE;
  } else {
    return -1;
  }
}

void *OVERRIDEN_FUNC(malloc)(size_t len) throw() {
  SETUP(malloc);
  auto ret = malloc_orig(len);
  DEBUGOUT(fprintf(stderr, "malloc: %ld %p\n", len, ret));
  implant_chunk(ret, len);
  return ret;
}

void *OVERRIDEN_FUNC(calloc)(size_t nmemb, size_t len) throw() {
  SETUP(calloc);
  auto ret = calloc_orig(nmemb, len);
  DEBUGOUT(fprintf(stderr, "calloc: %ld %p\n", len * nmemb, ret));
  implant_chunk(ret, len);
  return ret;
}

void *OVERRIDEN_FUNC(realloc)(void *addr, size_t len) throw() {
  SETUP(realloc);
  auto ret = realloc_orig(addr, len);
  DEBUGOUT(fprintf(stderr, "realloc: %p %ld %p\n", addr, len, ret));
  implant_chunk(ret, len);
  return ret;
}

void *OVERRIDEN_FUNC(memalign)(size_t alignment, size_t len) throw() {
  SETUP(memalign);
  auto ret = memalign_orig(alignment, len);
  DEBUGOUT(fprintf(stderr, "memalign: %ld %ld %p\n", alignment, len, ret));
  implant_chunk(ret, len);
  return ret;
}

void *OVERRIDEN_FUNC(aligned_alloc)(size_t alignment, size_t len) throw() {
  return OVERRIDEN_FUNC(memalign)(alignment, len);
}

int OVERRIDEN_FUNC(posix_memalign)(void **memptr, size_t alignment, size_t len)
  throw() {
  if (!posix_memalign_orig) {
    posix_memalign_orig = (posix_memalign_t)dlsym(RTLD_NEXT, "posix_memalign");
  }
  auto ret = posix_memalign_orig(memptr, alignment, len);
  DEBUGOUT(
    fprintf(stderr, "memalign: %p %ld %ld %d\n", memptr, alignment, len, ret);
  );
  implant_chunk(*memptr, len);
  return ret;
}

void *OVERRIDEN_FUNC(valloc)(size_t len) throw() {
  SETUP(valloc);
  auto ret = valloc_orig(len);
  DEBUGOUT(fprintf(stderr, "valloc: %ld %p\n", len, ret));
  implant_chunk(ret, len);
  return ret;
}

void *OVERRIDEN_FUNC(pvalloc)(size_t len) throw() {
  SETUP(pvalloc);
  auto ret = pvalloc_orig(len);
  DEBUGOUT(fprintf(stderr, "pvalloc: %ld %p\n", len, ret));
  implant_chunk(ret, len);
  return ret;
}

void OVERRIDEN_FUNC(free)(void *addr) throw() {
  SETUP(free);
  auto fetch_result = fetch_implanted(addr);
  if (fetch_result > 0) {
    DEBUGOUT(fprintf(stderr, "free: %p %ld\n", addr, fetch_result));
  }
  return free_orig(addr);
}
