#include "auditlib.h"

// USE WITHOUT PRECEDING UNDERSCORE
dlmap_t *_dlmap;
knownpath_map_t *_knownpath_map;

unsigned int la_version(unsigned int version) {
  return LAV_CURRENT;
}

char *la_objsearch(const char *name, uintptr_t *cookie, unsigned int flag) {
  const char *&file = name;
  if (file && !strchr(file, '/')) {
    SETUP_DLMAP;
    std::string strfile(file);
    if (dlmap.count(strfile)) {
      DEBUGOUT(fprintf(stderr, "Adding magic to locating library %s\n", file));
      file = dlmap[strfile].c_str();
    } else {
      DEBUGOUT(fprintf(stderr, "Oops, where is %s\n", file));
    }
  }
  return (char *)file;
}

void la_activity(uintptr_t *cookie, unsigned int flag) {
  static bool first, second;
  if (second) {
    // beyond second
    return;
  } else if (first) {
    // second
    remove(get_map_addr_filename().c_str());
    second = 1;
  } else {
    // first
    first = 1;
  }
}

ATTRCONSTRUCTOR void init(void) {
  _dlmap = new dlmap_t;
  _knownpath_map = new knownpath_map_t;
  dlmap_add_ld_cache();
  if (auto fp = fopen(".turingopt.cache", "r")) {
    dlmap_cache_fp(fp, false);
    fclose(fp);
  }
  if (is_buggy_glibc()) {
    auto fd = shm_open(get_map_addr_filename().c_str(),
                       O_CREAT | O_RDWR | O_TRUNC,
                       S_IRUSR | S_IWUSR);
    MAP_ADDR_FILE_SEQ(write);
    close(fd);
  } else {
    dlmap_add_searchpath();
  }
}
