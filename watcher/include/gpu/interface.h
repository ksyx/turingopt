#ifndef _TURINGWATCHER_GPU_INTERFACE_H
#define _TURINGWATCHER_GPU_INTERFACE_H
#include "common.h"
enum gpu_clock_limit_reason_t {
  CLOCK_LIMIT_BY_APP = 0x01,
  CLOCK_LIMIT_IDLE = 0x02,
  CLOCK_LIMIT_EXTERNAL_POWER_LIMIT = 0x04,
  CLOCK_LIMIT_HARDWARE = 0x08,
  // e.g. nvmlDeviceSetPowerManagementLimit
  CLOCK_LIMIT_SOFTWARE = 0x10,
  CLOCK_LIMIT_SYNC_WITH_OTHERS = 0x20,
  CLOCK_LIMIT_TEMPERATURE = 0x40,
  CLOCK_LIMIT_OTHER = 0x1000
};

enum gpu_measurement_source_t {
  GPU_SOURCE_NONE = 0,
  GPU_SOURCE_NVML = 1,
};

struct gpu_clock_limit_reason_mapping_t {
  gpu_clock_limit_reason_t id;
  const char *str;
};

typedef
std::vector<gpu_clock_limit_reason_mapping_t> gpu_clock_limit_reason_table_t;

extern const gpu_clock_limit_reason_table_t gpu_clock_limit_reason_table;
extern const gpu_measurement_source_t gpu_measurement_source;
extern std::map<gpu_measurement_source_t, const char *>
  gpu_measurement_source_str_table;

struct gpu_measurement_t {
  uint32_t gpu_id;
  pid_t pid;
  uint32_t temp;
  uint32_t sm_clock; // MHz
  uint32_t util;
  uint32_t clock_limit_reason_mask;

  slurm_step_id_t step;

  const uint32_t source = gpu_measurement_source;
};

typedef std::vector<gpu_measurement_t> measure_gpu_result_t;

uint32_t gpu_clock_limit_reason_to_mask(const char *str);
std::string gpu_clock_limit_reason_to_str(uint32_t mask);
void measure_gpu(measure_gpu_result_t &results);
bool init_gpu_measurement();
void finalize_gpu_measurement();
#endif
