#ifndef _TURING_AUDITLIB_H
#define _TURING_AUDITLIB_H
#include "interceptlib.h"
#include "dlmap.h"
#include "mm.h"

#include <link.h>
#include <assert.h>

LINKAGE unsigned int la_version(unsigned int version);
LINKAGE char *la_objsearch(const char *name, uintptr_t *cookie,
                           unsigned int flag);
LINKAGE unsigned int la_objopen(struct link_map *map, Lmid_t lmid,
                                uintptr_t *cookie);
LINKAGE uintptr_t la_symbind64(Elf64_Sym *sym, unsigned int ndx,
                               uintptr_t *refcook, uintptr_t *defcook,
                               unsigned int *flags, const char *symname);
LINKAGE void la_activity(uintptr_t *cookie, unsigned int flag);
LINKAGE ATTRCONSTRUCTOR void init(void);
#endif
