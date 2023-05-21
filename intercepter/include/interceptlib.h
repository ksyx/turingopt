#ifndef _TURING_INTERCEPTLIB_H
#define _TURING_INTERCEPTLIB_H
#define ENABLE_WATCHPOINT 1
#define DEBUGOUT 1

#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>

#include <sys/stat.h>
#if ENABLE_WATCHPOINT
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <linux/sched.h>
#include <sys/prctl.h>
#include <atomic>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <condition_variable>
#include <mutex>
#include <map>
#include <string>

#if ENABLE_WATCHPOINT
#include "debugreg.h"
extern "C" {
// #include "procmaps.h"
void _dl_debug_state(void);
}
#endif

#define LINKAGE extern "C"
#define ATTRCONSTRUCTOR __attribute__ ((constructor))
#define UNUSED(x) (void)(x)

typedef std::map<std::string, bool> knownpath_map_t;
typedef std::map<std::string, std::string> dlopen_map_t;
LINKAGE unsigned int la_version(unsigned int version);
LINKAGE char *la_objsearch(const char *name, uintptr_t *cookie,
                            unsigned int flag);
LINKAGE ATTRCONSTRUCTOR void init(void);
#endif