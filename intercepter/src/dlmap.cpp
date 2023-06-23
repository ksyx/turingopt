#include "dlmap.h"

inline void dlopen_cache_path(const std::string &path) {
  SETUP_DLMAP;
  SETUP_KNOWNPATH_MAP;
  DEBUGPATH(fprintf(stderr, "%s\n", path.c_str()));
  if (knownpath_map.count(path)) {
    return;
  }
  DIR *dir;
  dirent *entry;
  dir = opendir(path.c_str());
  if (!dir) {
    fprintf(stderr, "While reading %s: ", path.c_str());
    perror("opendir");
    return;
  }
  while ((entry = readdir(dir))) {
    const char *name = entry->d_name;
    bool isdir = entry->d_type == DT_DIR;
    const std::string strname(name);
    if (entry->d_type) {
      struct stat buf;
      lstat((path + "/" + strname).c_str(), &buf);
      isdir = S_ISDIR(buf.st_mode);
    }
    if (!isdir) {
      dlmap.try_emplace(strname, path + "/" + strname);
    }
  }
  closedir(dir);
}

bool is_buggy_glibc(void) {
  constexpr int versionlevel = 2;
  constexpr int versions[versionlevel] = {2, 34};
  auto verstr = gnu_get_libc_version();
  int curlevel = 0, curver = 0;
  while (verstr) {
    fflush(stdout);
    if (*verstr >= '0' && *verstr <= '9') {
      curver = curver * 10 + *verstr - '0';
    } else {
      if (curver != versions[curlevel]) {
        return curver < versions[curlevel];
      }
      curlevel++;
      curver = 0;
      if (!*verstr || curlevel == versionlevel) {
        return true;
      }
    }
    verstr++;
  }
  fprintf(stderr,
          "Result for whether glibc %s is buggy is undeterminable.",
          gnu_get_libc_version());
  exit(1);
}

void dlmap_add_searchpath(void) {
  SETUP_KNOWNPATH_MAP;
  DEBUGOUT(fputs("Starting up intercepter...\n", stderr));
  auto dl_handle = dlopen(NULL, RTLD_LAZY);
  DEBUGOUT(fputs("Setup...\n", stderr); fflush(stderr));
  Dl_serinfo dl_search_size;
  if (dlinfo(dl_handle, RTLD_DI_SERINFOSIZE, &dl_search_size)) {
    fprintf(stderr, "dlinfo: %s while populating search info size\n",
            dlerror());
    return;
  }
  Dl_serinfo *dl_search_info = (Dl_serinfo *)malloc(dl_search_size.dls_size);
  if (!dl_search_info) {
    perror("malloc");
    return;
  }
  if (dlinfo(dl_handle, RTLD_DI_SERINFOSIZE, dl_search_info)) {
    fprintf(stderr, "dlinfo: %s while populating search info size again\n",
            dlerror());
    return;
  }
  if (dlinfo(dl_handle, RTLD_DI_SERINFO, dl_search_info)) {
    fprintf(stderr, "dlinfo: %s while populating search info entries\n",
            dlerror());
    return;
  }
  std::map<std::string, bool> checked;
  for (unsigned int i = 0; i < dl_search_info->dls_cnt; i++) {
    std::string strpath(dl_search_info->dls_serpath[i].dls_name);
    DEBUGPATH(fputs((strpath + "\n").c_str(), stderr));
    dlopen_cache_path(strpath);
    knownpath_map[strpath] = true;
  }
  DEBUGOUT(fputs("INITIALIZED\n", stderr));
  free(dl_search_info);
}

void dlmap_cache_fp(FILE *fp, bool isdir) {
  SETUP_KNOWNPATH_MAP;
  char *buf = NULL;
  size_t bufsize = 0; ssize_t totread;
  while ((totread = getline(&buf, &bufsize, fp)) != -1) {
    buf[totread - 1] = '\0';
    if (isdir) {
      dlopen_cache_path(std::string(buf));
      DEBUGPATH(fprintf(stderr, "cache: %s\n", buf));
      knownpath_map[buf] = true;
    } else {
      SETUP_DLMAP;
      DEBUGPATH(fprintf(stderr, "cache: %s -> %s\n", basename(buf), buf));
      dlmap.try_emplace(std::string(basename(buf)), std::string(buf));
    }
  }
}

void dlmap_add_ld_cache(void) {
  // Someone spent hours here figuring out why init being called tons of times
  setenv("LD_PRELOAD__COPY__", getenv("LD_PRELOAD"), 1);
  setenv("LD_AUDIT__COPY__", getenv("LD_AUDIT"), 1);
  unsetenv("LD_PRELOAD");
  unsetenv("LD_AUDIT");
  // List directories in ldd cache
  auto fp = popen("ldconfig -v 2> /dev/null "
                  "| grep '^[^ ].*:' | cut -d: -f1", "r");
  if (!fp) {
    perror("popen: ");
    return;
  }
  dlmap_cache_fp(fp, true);
  setenv("LD_PRELOAD", getenv("LD_PRELOAD__COPY__"), 1);
  setenv("LD_AUDIT", getenv("LD_AUDIT__COPY__"), 1);
}
