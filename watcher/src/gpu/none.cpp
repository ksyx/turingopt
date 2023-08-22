#include "gpu/interface.h"

const gpu_measurement_source_t gpu_measurement_source = GPU_SOURCE_NONE;
const bool gpu_provider_job_mapped = false;

bool init_gpu_measurement() {
  return true;
}

void finalize_gpu_measurement() {}

void measure_gpu(measure_gpu_result_t &results) {}
