#ifndef _TURING_INTERCEPTLIB_H
#define _TURING_INTERCEPTLIB_H
#define ENABLE_DEBUGOUT 1
#define ENABLE_PRINTPATH 0
#if ENABLE_DEBUGOUT
#define DEBUGOUT(X) X
#else
#define DEBUGOUT(X) ;
#endif

#if ENABLE_PRINTPATH
#define DEBUGPATH(X) DEBUGOUT(X)
#else
#define DEBUGPATH(X) ;
#endif

#include <unistd.h>
#include <stdint.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <map>
#include <string>

#include <sys/types.h>

#define LINKAGE extern "C"
#define ATTRCONSTRUCTOR __attribute__ ((constructor))
#define UNUSED(x) (void)(x)

#endif
