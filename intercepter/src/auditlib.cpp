#include "auditlib.h"
#if !DISCARD_AUDITLIB
// USE WITHOUT PRECEDING UNDERSCORE
dlmap_t *_dlmap;
knownpath_map_t *_knownpath_map;
size_t pagesize;

unsigned int la_version(unsigned int version) {
  return LAV_CURRENT;
}

char *la_objsearch(const char *name, uintptr_t *cookie, unsigned int flag) {
  const char *&file = name;
  if (file && !strchr(file, '/')) {
    SETUP_DLMAP;
    std::string strfile(file);
    if (dlmap.count(strfile)) {
      DEBUGPATH(fprintf(stderr, "Adding magic to locating library %s\n", file));
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

unsigned int la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie) {
  #define LIBC_SO_PREFIX "libc"
  const char *libc_basename_prefix = LIBC_SO_PREFIX;
  const char *lib_basename = basename(map->l_name);
  DEBUGOUT(fprintf(stderr, "lib=%s cookie=%p\n", lib_basename, cookie););
  while (*libc_basename_prefix && *lib_basename) {
    if (*libc_basename_prefix != *lib_basename) {
      break;
    }
    libc_basename_prefix++;
    lib_basename++;
  }
  if (!*libc_basename_prefix && (*lib_basename == '-' || *lib_basename == '.')) {
    DEBUGOUT(fputs("Registering libc binding callback\n", stderr));
    return LA_FLG_BINDTO | LA_FLG_BINDFROM;
  } else {
    return LA_FLG_BINDFROM;
  }
}

uintptr_t la_symbind64(Elf64_Sym *sym, unsigned int ndx,
                       uintptr_t *refcook, uintptr_t *defcook,
                       unsigned int *flags, const char *symname) {
  DEBUGOUT(fprintf(stderr, "Redirecting %s\n", symname));
  if (*flags & LA_SYMB_ALTVALUE) {
    return sym->st_value;
  }
  while (*symname == '_') {
    symname++;
  }
  auto ret = sym->st_value;
  const auto hash = [](const char *str) -> uint64_t {
    uint64_t hash = 0;
    while (*str && hash <= (uint64_t) 7e16) {
      // holds 6 characters with no possibility of collison
      hash = hash * 257 + *(str++);
    }
    if (*str) {
      return 0;
    }
    return hash;
  };
  #define HASHNAME(NAME) hash_##NAME
  #define PREPMATCH(NAME) \
    constexpr auto HASHNAME(NAME) = hash(#NAME); \
    static_assert(HASHNAME(NAME) != 0);
  PREPMATCH(munmap);
  PREPMATCH(malloc);
  PREPMATCH(calloc);
  PREPMATCH(realloc);
  PREPMATCH(free);
  PREPMATCH(memalign);
  PREPMATCH(valloc);
  PREPMATCH(pvalloc);

  #define TARGET_FUNC(NAME) (Elf64_Addr)(OVERRIDEN_FUNC(NAME))
  #define MATCH(NAME) \
    case HASHNAME(NAME): \
    if (!NAME##_orig) { \
      /* Use value provided here to be the libc in normal linking namespace */\
      NAME##_orig = (NAME##_t) sym->st_value; \
    } \
    ret = TARGET_FUNC(NAME); \
    DEBUGOUT(fprintf(stderr, "-- Redirected %s from %p\n", symname, defcook)); \
    break;
  // switch block also checks for duplications.
  switch (hash(symname)) {
    case 0: break;
    MATCH(malloc);
    MATCH(calloc);
    MATCH(realloc);
    MATCH(munmap);
    MATCH(free);
    MATCH(memalign);
    MATCH(valloc);
    MATCH(pvalloc);
  }
  return ret;
  #undef PREPMATCH
  #undef MATCH
  #undef TARGET_FUNC
  #undef HASHNAME
}

ATTRCONSTRUCTOR void init(void) {
  _dlmap = new dlmap_t;
  _knownpath_map = new knownpath_map_t;
  pagesize = sysconf(_SC_PAGE_SIZE);
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
#endif
