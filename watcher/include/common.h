#ifndef _TURINGWATCHER_COMMON_H
#define _TURINGWATCHER_COMMON_H
#include <slurm/slurmdb.h>
#include <slurm/slurm.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <stdexcept>

#include <sys/types.h>
#include <unistd.h>

#include <sqlite3.h>

#define ENABLE_DEBUGOUT 1
#if ENABLE_DEBUGOUT
#define DEBUGOUT(X) X
#else
#define DEBUGOUT(X) ;
#endif

#define EXPECT_EQUAL(EXPR, EXPECT) ((EXPR) == (EXPECT))
#define IS_SQLITE_OK(EXPR) EXPECT_EQUAL(EXPR, SQLITE_OK)
#define IS_SLURM_SUCCESS(EXPR) EXPECT_EQUAL(EXPR, SLURM_SUCCESS)
#endif
