#ifndef _TURINGWATCHER_GPU_PROVIDER_NVML_H
#define _TURINGWATCHER_GPU_PROVIDER_NVML_H
#include "gpu/interface.h"
#include <nvml.h>

#define IS_NVML_SUCCESS(EXPR) EXPECT_EQUAL(EXPR, NVML_SUCCESS)
#define NVML_PERROR(ERRCODE, PREFIX) \
  fprintf(stderr, "nvml%s: %s\n", PREFIX, nvmlErrorString(ERRCODE));
#endif
