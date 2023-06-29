#ifndef _TURING_DLMAP_H
#define _TURING_DLMAP_H
#include "interceptlib.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <gnu/libc-version.h>
#if !DISCARD_AUDITLIB
typedef std::map<std::string, bool> knownpath_map_t;
typedef std::map<std::string, std::string> dlmap_t;

extern dlmap_t *_dlmap;
extern knownpath_map_t *_knownpath_map;

#define SETUP_DLMAP dlmap_t &dlmap = *_dlmap;
#define SETUP_KNOWNPATH_MAP knownpath_map_t &knownpath_map = *_knownpath_map;

void dlmap_cache_path (const std::string &path);
void dlmap_add_ld_cache (void);
void dlmap_add_searchpath (void);
bool is_buggy_glibc (void);
void dlmap_cache_fp (FILE *fp, bool isdir);

#define PERROR_MAP_ADDR_FILE \
  perror(("While opening " + get_map_addr_filename()).c_str());

#define MAP_ADDR_FILE_SINGLEOP(OP, NAME) OP(fd, NAME, sizeof(NAME))
#define MAP_ADDR_FILE_SEQ(OP) \
  MAP_ADDR_FILE_SINGLEOP(OP, &_dlmap); \
  MAP_ADDR_FILE_SINGLEOP(OP, &_knownpath_map);

inline std::string get_map_addr_filename() {
  static std::string path = std::to_string(getpid()) + ".turingopt.addr";
  return path;
}
#endif
#endif
