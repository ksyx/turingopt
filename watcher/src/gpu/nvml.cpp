#include "gpu/provider_nvml.h"

const gpu_measurement_source_t gpu_measurement_source = GPU_SOURCE_NVML;
static bool initialized;

bool init_gpu_measurement() {
  nvmlReturn_t ret = NVML_SUCCESS;
  if (!IS_NVML_SUCCESS(ret = nvmlInitWithFlags(NVML_INIT_FLAG_NO_GPUS))) {
    NVML_PERROR(ret, "InitWithFlags");
  } else {
    initialized = true;
  }
  return true;
}

void measure_gpu(measure_gpu_result_t &results) {
  if (!initialized) {
    return;
  }
  gpu_measurement_t record;
  unsigned int device_cnt;
  nvmlReturn_t ret_err = NVML_SUCCESS;
  unsigned int ret_int;
  if (!IS_NVML_SUCCESS(ret_err = nvmlDeviceGetCount_v2(&device_cnt))) {
    NVML_PERROR(ret_err, "DeviceGetCount_v2");
    return;
  }
  unsigned int proc_buf_size = 0;
  nvmlProcessInfo_t *proc_info = NULL;
  for (unsigned int i = 0; i < device_cnt; i++) {
    nvmlDevice_t device;
    if (!IS_NVML_SUCCESS(ret_err = nvmlDeviceGetHandleByIndex_v2(i, &device))) {
      if (ret_err != NVML_ERROR_NO_PERMISSION) {
        NVML_PERROR(ret_err, "DeviceGetHandleByIndex");
      }
      continue;
    }
    record.gpu_id = i;
    if (!IS_NVML_SUCCESS(ret_err = nvmlDeviceGetTemperature(
      device, NVML_TEMPERATURE_GPU, &ret_int))) {
      NVML_PERROR(ret_err, "DeviceGetTemperature");
      continue;
    }
    record.temp = ret_int;
    unsigned int process_cnt = 0;
    while ((ret_err = nvmlDeviceGetComputeRunningProcesses_v3(
      device, &proc_buf_size, proc_info)) == NVML_ERROR_INSUFFICIENT_SIZE) {
      proc_buf_size *= 2;
      proc_info
        = (nvmlProcessInfo_t *)malloc(proc_buf_size * sizeof(*proc_info));
    }
    if (!IS_NVML_SUCCESS(ret_err)) {
      NVML_PERROR(ret_err, "DeviceGetComputeRunningProcesses_v3");
      continue;
    }
    process_cnt = proc_buf_size;
    if (!process_cnt) {
      continue;
    }
    nvmlUtilization_t util;
    if (!IS_NVML_SUCCESS(nvmlDeviceGetUtilizationRates(device, &util))) {
      NVML_PERROR(ret_err, "DeviceGetUtilizationRates");
      continue;
    }
    record.util = util.gpu;
    if (!IS_NVML_SUCCESS(ret_err = nvmlDeviceGetClock(
      device, NVML_CLOCK_SM, NVML_CLOCK_ID_CURRENT, &ret_int))) {
      NVML_PERROR(ret_err, "DeviceGetClock");
      continue;
    }
    record.sm_clock = ret_int;
    {
      struct reason_mapping_t {
        unsigned long long nvml_val;
        gpu_clock_limit_reason_t rec_val;
      };
      const reason_mapping_t mapping[] = {
        {nvmlClocksThrottleReasonApplicationsClocksSetting, CLOCK_LIMIT_BY_APP},
        {nvmlClocksThrottleReasonGpuIdle, CLOCK_LIMIT_IDLE},
        {nvmlClocksThrottleReasonHwPowerBrakeSlowdown,
         CLOCK_LIMIT_EXTERNAL_POWER_LIMIT},
        {nvmlClocksThrottleReasonHwSlowdown, CLOCK_LIMIT_HARDWARE},
        {nvmlClocksThrottleReasonSwPowerCap, CLOCK_LIMIT_SOFTWARE},
        {nvmlClocksThrottleReasonSyncBoost, CLOCK_LIMIT_SYNC_WITH_OTHERS},
        {nvmlClocksThrottleReasonHwThermalSlowdown, CLOCK_LIMIT_TEMPERATURE},
        {nvmlClocksThrottleReasonSwThermalSlowdown, CLOCK_LIMIT_TEMPERATURE},
        {0, CLOCK_LIMIT_OTHER}
      };
      unsigned long long nvml_reason_mask;
      if (!IS_NVML_SUCCESS(
        nvmlDeviceGetCurrentClocksThrottleReasons(device, &nvml_reason_mask))) {
        NVML_PERROR(ret_err, "GetCurrentClocksThrottleReasons");
        continue;
      }
      const reason_mapping_t *cur = mapping;
      record.clock_limit_reason_mask = 0;
      if (nvml_reason_mask) {
        while (cur->nvml_val) {
          if (nvml_reason_mask & cur->nvml_val) {
            record.clock_limit_reason_mask |= cur->rec_val;
          }
          cur++;
        }
      }
    }
    DEBUGOUT(
      std::string reason
        = gpu_clock_limit_reason_to_str(record.clock_limit_reason_mask);
      fprintf(stderr, "[nvml] gpu=%d temp=%d sm_clock=%d util=%d reason=%s\n",
              record.gpu_id, record.temp, record.sm_clock, record.util,
              reason.c_str());
    );
    for (unsigned int p = 0; p < process_cnt; p++) {
      record.pid = proc_info[p].pid;
      results.push_back(record);
      DEBUGOUT(fprintf(stderr, "-- %d\n", proc_info[p].pid));
    }
  }
}

void finalize_gpu_measurement() {
  if (!initialized) {
    return;
  }
  nvmlReturn_t ret = NVML_SUCCESS;
  if (!IS_NVML_SUCCESS(ret = nvmlShutdown())) {
    NVML_PERROR(ret, "Shutdown");
  }
}
