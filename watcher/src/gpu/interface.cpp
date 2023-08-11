#include "gpu/interface.h"

const char *empty_reason = "-";

const gpu_clock_limit_reason_table_t gpu_clock_limit_reason_table = {
  { CLOCK_LIMIT_BY_APP, "app" },
  { CLOCK_LIMIT_IDLE, "idle" },
  { CLOCK_LIMIT_EXTERNAL_POWER_LIMIT, "external_power_limit" },
  { CLOCK_LIMIT_HARDWARE, "hardware" },
  { CLOCK_LIMIT_SOFTWARE, "software" },
  { CLOCK_LIMIT_SYNC_WITH_OTHERS, "sync" },
  { CLOCK_LIMIT_TEMPERATURE, "temp" },
  { CLOCK_LIMIT_OTHER, "other" },
};

std::map<gpu_measurement_source_t, const char *>
gpu_measurement_source_str_table = {
  { GPU_SOURCE_NONE, "none" },
  { GPU_SOURCE_NVML, "nvml" },
};

uint32_t gpu_clock_limit_reason_to_mask(const char *str) {
  if (!strcmp(str, empty_reason)) {
    return 0;
  }
  std::string str_copy;
  uint32_t ret_val = 0;
  for (auto &c : str_copy) {
    if (c == ',') {
      c = '\0';
    }
  }
  for (const auto &reason : gpu_clock_limit_reason_table) {
    const auto str_copy_ptr = str_copy.c_str();
    if (auto ret = (const char *) memmem(str_copy_ptr, str_copy.length() + 1,
                                         reason.str, strlen(reason.str) + 1)) {
      if (ret == str_copy_ptr || *(ret - 1) == '\0') {
        ret_val |= reason.id;
      }
    }
  }
  return ret_val;
}

std::string gpu_clock_limit_reason_to_str(uint32_t mask) {
  if (!mask) {
    return empty_reason;
  }
  std::string ret;
  bool first = 1;
  for (const auto &reason : gpu_clock_limit_reason_table) {
    if (mask & reason.id) {
      ret += std::string(first ? "" : ",") + std::string(reason.str);
    }
  }
  return ret;
}
