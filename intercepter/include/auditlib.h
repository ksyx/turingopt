#ifndef _TURING_AUDITLIB_H
#define _TURING_AUDITLIB_H
#include "interceptlib.h"
#include "dlmap.h"

#include <link.h>

LINKAGE unsigned int la_version(unsigned int version);
LINKAGE char *la_objsearch(const char *name, uintptr_t *cookie,
                            unsigned int flag);
LINKAGE void la_activity(uintptr_t *cookie, unsigned int flag);
LINKAGE ATTRCONSTRUCTOR void init(void);
#endif
