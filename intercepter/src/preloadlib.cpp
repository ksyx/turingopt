#include "preloadlib.h"

#if !DISCARD_AUDITLIB
dlmap_t *_dlmap;
knownpath_map_t *_knownpath_map;
#endif
size_t pagesize;

int __munmap(void *addr, size_t len) throw() {
  return munmap(addr, len);
}

ATTRCONSTRUCTOR void init(void) {
  pagesize = sysconf(_SC_PAGE_SIZE);
  #if !DISCARD_AUDITLIB
  if (is_buggy_glibc()) {
    auto fd = shm_open(get_map_addr_filename().c_str(), O_RDONLY, 0);
    MAP_ADDR_FILE_SEQ(read);
    close(fd);
    dlmap_add_searchpath();
  }
  #endif
}
