#include "interceptlib.h"

// USE WITHOUT PRECEDING UNDERSCORE
static dlopen_map_t *_dlopen_map;
static knownpath_map_t *_knownpath_map;
#define SETUP_DLOPEN_MAP dlopen_map_t &dlopen_map = *_dlopen_map;
#define SETUP_KNOWNPATH_MAP knownpath_map_t &knownpath_map = *_knownpath_map;

#if ENABLE_WATCHPOINT
constexpr size_t stasize = 8192;
extern const dr7_t dr7_exec_on_dr0, dr7_nothing;
static bool dr7_cleared;
static pid_t ptracerid;
static std::mutex maplock;

const char *GLRO_addr = (char *)_dl_debug_state;
#endif

unsigned int la_version(unsigned int version) {
  return LAV_CURRENT;
}

char *la_objsearch(const char *name, uintptr_t *cookie, unsigned int flag) {
//  printf("%s\n",name);
  return (char *)name;
  const char *&file = name;
  if (file && !strchr(file, '/')) {
    SETUP_DLOPEN_MAP;
    std::string strfile(file);
    if (dlopen_map.count(strfile)) {
      #if DEBUGOUT
      fprintf(stderr, "Adding magic to locating library %s\n", file);
      #endif
      file = dlopen_map[strfile].c_str();
    } else {
      #if DEBUGOUT
      fprintf(stderr, "Oops, where is %s\n", file);
      #endif
    }
  }
  return (char *)file;
}

inline void dlopen_cache_path (const std::string &path) {
  SETUP_DLOPEN_MAP;
  SETUP_KNOWNPATH_MAP;
  #if DEBUGOUT
  fprintf(stderr, "%s\n", path.c_str());
  #endif
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
      dlopen_map.try_emplace(strname, path + "/" + strname);
    }
  }
  closedir(dir);
}

void initfollowup() {
  static std::mutex mu;
  if (!mu.try_lock()) {
    return;
  }
  #if ENABLE_WATCHPOINT
  maplock.lock();
  #endif
  SETUP_KNOWNPATH_MAP;
  fputs("Starting up intercepter...\n", stderr);
  auto dl_handle = dlopen(NULL, RTLD_LAZY);
  fputs("Setup...\n", stderr); fflush(stderr);
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
    dlopen_cache_path(strpath);
    knownpath_map[strpath] = true;
  }
  fprintf(stderr, "INITIALIZED");
  free(dl_search_info);
  #if ENABLE_WATCHPOINT
  maplock.unlock();
  #endif
  // DO NOT FREE LOCK mu
}

#if ENABLE_WATCHPOINT
void initfollowupworker(int sig, siginfo_t *info, void *uctx) {
  static bool first = 1;
  printf("Watchpointing\n"); fflush(stdout);
  if (first) {
    first = 0;
  } else {
    static std::mutex mu;
    if (!mu.try_lock()) {
      return;
    }
    printf("%d\n",ptracerid);
    kill(ptracerid, SIGUSR1);
    while(!dr7_cleared) {
      sched_yield();
    }
    kill(ptracerid, SIGKILL);
    initfollowup();
    // DO NOT FREE THE LOCK
  }
}

#if DEBUGOUT
#define PERROR(x) perror(x)
#else
#define PERROR(x) ;
#endif
int ptracer(void *childpid) {
  auto child = (pid_t *)childpid;
  *child = getpid();
  pid_t pid = getppid();
  printf("PPID = %d %d\n", *child, pid);
  while(*child);
  errno = 0;
  ptrace(PTRACE_ATTACH, pid, NULL, NULL);
  PERROR("ptrace_attach");
  ptrace(PTRACE_POKEUSER, pid, DR_OFFSET(0), GLRO_addr);
  PERROR("ptrace_setwatchaddr");
  ptrace(PTRACE_POKEUSER, pid, DR_OFFSET(7), dr7_exec_on_dr0);
  PERROR("ptrace_setwatchattr");
  ptrace(PTRACE_POKEUSER, pid, DR_OFFSET(6), (void *)0);
  PERROR("ptrace_clearwatchresult");
  ptrace(PTRACE_DETACH, pid, NULL, NULL);
  PERROR("ptrace_detach");
  *child = 1;
  pause();
  while(!dr7_cleared) {
    sched_yield();
  }
  return 0;
}

void ptracerfollowup(int _) {
  UNUSED(_);
  pid_t pid = getppid();
  errno = 0;
  ptrace(PTRACE_ATTACH, pid, NULL, NULL);
  PERROR("ptrace_attach_clear");
  ptrace(PTRACE_POKEUSER, pid, DR_OFFSET(7), dr7_nothing);
  PERROR("ptrace_clearwatchattr");
  ptrace(PTRACE_DETACH, pid, NULL, NULL);
  PERROR("ptrace_detach_clear");
  dr7_cleared = 1;
  exit(0);
}
#undef PERROR
#endif

void *initworker(void *_) {
  UNUSED(_);
  SETUP_KNOWNPATH_MAP;
  #if ENABLE_WATCHPOINT
  maplock.lock();
  #endif
  // List directories in ldd cache
  // Someone spent hours here figuring out why init being called tons of times
  unsetenv("LD_PRELOAD");
  unsetenv("LD_AUDIT");
  auto fp = popen("ldconfig -v 2> /dev/null "
                  "| grep '^[^ ].*:' | cut -d: -f1", "r");
  char *buf = NULL;
  size_t bufsize = 0; ssize_t totread;
  while(int c = fgetc(fp) > 0) {
    printf("%c",c);
  }
  if (!fp) {
    perror("popen: ");
    goto watchpointing;
  }
  while ((totread = getline(&buf, &bufsize, fp)) != -1) {
    buf[totread - 1] = '\0';
    #if DEBUGOUT
    fprintf(stderr, "ld cache: %s\n", buf);
    #endif
    dlopen_cache_path(std::string(buf));
    knownpath_map[buf] = true;
  }
  #if ENABLE_WATCHPOINT
  maplock.unlock();
  #endif
  pclose(fp);
  free(buf);
watchpointing:;
  // Wait for GLRO(dl_init_all_dirs) to be nonnull
  #if !ENABLE_WATCHPOINT
  sleep(3);
  initfollowup();
  #endif
  return NULL;
}

ATTRCONSTRUCTOR void init(void) {
  /*
  hr_procmaps **procmap = contruct_procmaps(0);
  auto procmaphead = procmap;
  while (const auto cur = *procmap){
    printf("%p %p %p %p %s\n", GLRO, cur->addr_begin, cur->offset, cur->offset + (cur->addr_end - cur->addr_begin), cur->pathname);
    if (GLRO >= cur->offset
        && GLRO <= cur->offset + (cur->addr_end - cur->addr_begin)
        && cur->pathname && strstr(cur->pathname, "/ld-")) {
      GLRO_addr = (char *)(cur->addr_begin + GLRO - cur->offset + GLRO_OFFSET);
      break;
    }
    procmap++;
	}
  printf("GLROADDR: %p\n", GLRO_addr);
  destroy_procmaps(procmaphead);
  */
  _dlopen_map = new dlopen_map_t;
  _knownpath_map = new knownpath_map_t;
  #if ENABLE_WATCHPOINT
  static struct sigaction siga;
  static char sta[stasize];
  static pid_t child = 0;
  signal(SIGUSR1, ptracerfollowup);
  clone(ptracer,
        (void *)(sta + stasize - 1),
        CLONE_FILES | CLONE_FS | CLONE_IO | CLONE_VM,
        &child);
  signal(SIGUSR1, SIG_DFL);
  siga.sa_sigaction = initfollowupworker;
  siga.sa_flags = SA_SIGINFO;
  sigaction(SIGTRAP, &siga, NULL);
  while(!child) {
    sched_yield();
  }
  ptracerid = child;
  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY /*ptracerid*/);
  child = 0;
  while(!child) {
    sched_yield();
  }
  printf("Child = %d\n", child);
  #endif
  pthread_t thread;
  pthread_create(&thread, NULL, initworker, NULL);
}