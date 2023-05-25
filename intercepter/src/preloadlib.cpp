#include "dlmap.h"

dlmap_t *_dlmap;
knownpath_map_t *_knownpath_map;

ATTRCONSTRUCTOR void init(void) {
  if (is_buggy_glibc()) {
    auto fd = shm_open(get_map_addr_filename().c_str(), O_RDONLY, 0);
    MAP_ADDR_FILE_SEQ(read);
    close(fd);
    dlmap_add_searchpath();
  }
}
