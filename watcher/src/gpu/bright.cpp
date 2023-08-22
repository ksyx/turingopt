#include "gpu/provider_bright.h"

const gpu_measurement_source_t gpu_measurement_source = GPU_SOURCE_BRIGHT;
const bool gpu_provider_job_mapped = true;

static CURL *CURL_HANDLE_VAR;
static CURLcode CURLRET_VAR;

static CURL *curl_jsoncall_handle;
static curl_slist *jsoncall_header;

static char *buf;
static size_t buf_size = 512 * 1024;
static size_t buf_used;

static bool initialized;

static std::string bright_base;
static std::string gpu_monitoring_url;
static std::string gpu_job_mapping_url;

static size_t curl_write(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t len = size * nmemb;
  while (buf_used + len >= buf_size) {
    buf_size *= 2;
  }
  buf = (char *) realloc(buf, buf_size);
  memcpy(buf + buf_used, ptr, len);
  buf_used += len;
  return len;
}

static inline bool init_curl_jsoncall() {
  if (!(curl_jsoncall_handle = curl_easy_duphandle(CURL_HANDLE_VAR))) {
    fputs("curl_easy_duphandle(init_curl_jsoncall): failed\n", stderr);
    return false;
  }
  const std::string json_call_url = bright_base + std::string("/json");
  curl_easy_setopt(curl_jsoncall_handle, CURLOPT_POST, 1);
  curl_easy_setopt(curl_jsoncall_handle, CURLOPT_URL, json_call_url.c_str());
  CURL_LIST_APPEND(jsoncall_header, "Content-Type: application/json");
  curl_easy_setopt(curl_jsoncall_handle, CURLOPT_HTTPHEADER, jsoncall_header);
  return true;
}

static inline bool curl_perform_and_verify(CURL *handle) {
  buf_used = 0;
  curl_ret = curl_easy_perform(handle);
  buf[buf_used] = '\0';
  if (!IS_CURL_OK) {
    CURL_PERROR("perform");
    return false;
  }
  long http_code;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code != 200) {
    fprintf(stderr, "curl_easy_perform: HTTP status code %ld\n", http_code);
    return false;
  }
  DEBUGOUT_VERBOSE(fprintf(stderr, "Response Body:\n%s\n", buf));
  return true;
}

static inline bool has_env(const char *env_name) {
  const char *env = getenv(env_name);
  return env && strcmp(env, "default");
}

static inline bool get_bright_measurement(std::string name, std::string url) {
  CURL_SET_OPT(CURLOPT_URL, (bright_base + url + name).c_str());
  if (!curl_perform_and_verify(CURL_HANDLE_VAR)) {
    fprintf(stderr, "error: failed to fetch measurement %s\n", name.c_str());
    return false;
  }
  return true;
}

bool init_gpu_measurement() {
  if (!(CURL_HANDLE_VAR = curl_easy_init())) {
    fputs("curl_easy_init: failed\n", stderr);
    return false;
  }
  buf = (char *)calloc(buf_size, sizeof(char));
  const char *bright_base_env = getenv(BRIGHT_URL_BASE_ENV);
  {
    const char *err_msg = NULL;
    if (!bright_base_env) {
      err_msg = "error: environment %s is not specified\n";
    } else if (strncmp(bright_base_env, "http", 4)) {
      err_msg = "error: environment %s does not start with http or https";
    } else if (strchr(bright_base_env + 8, '/')) {
      // 7 = strlen("https://")
      err_msg = "error: environment %s cannot contain slash";
    }
    if (err_msg) {
      fprintf(stderr, err_msg, BRIGHT_URL_BASE_ENV);
      return false;
    }
  }
  std::string cert_str;
  std::string key_str;
  const bool has_cert = has_env(BRIGHT_CERT_PATH_ENV);
  const bool has_key = has_env(BRIGHT_KEY_PATH_ENV);
  if (!has_cert || !has_key) {
    std::string home_str;
    {
      const char *home_path;
      if (!(home_path = getenv("HOME"))) {
        home_path = getpwuid(getuid())->pw_dir;
      }
      home_str = std::string(home_path);
    }
    if (!has_cert) {
      cert_str = home_str + std::string("/.cm/cert.pem");
    } else {
      cert_str = std::string(getenv(BRIGHT_CERT_PATH_ENV));
    }
    if (!has_key) {
      key_str = home_str + std::string("/.cm/cert.key");
    } else {
      key_str = std::string(getenv(BRIGHT_KEY_PATH_ENV));
    }
  }
  DEBUGOUT_VERBOSE(CURL_SET_OPT(CURLOPT_VERBOSE, 1);)
  CURL_SET_OPT(CURLOPT_WRITEFUNCTION, curl_write);
  CURL_SET_OPT(CURLOPT_SSLCERT, cert_str.c_str());
  CURL_SET_OPT(CURLOPT_SSLKEY, key_str.c_str());
  if (has_env(NO_CHECK_SSL_CERT_ENV)) {
    CURL_SET_OPT(CURLOPT_SSL_VERIFYPEER, 0);
    CURL_SET_OPT(CURLOPT_SSL_VERIFYHOST, 0);
  }
  bright_base = std::string(bright_base_env);
  if (!init_curl_jsoncall()) {
    return false;
  }
  std::string monitoring_prefix = "/rest/v1/monitoring/latest?entity=";
  std::string monitoring_suffix = "&measurable=";
  gpu_monitoring_url
    = monitoring_prefix + std::string(worker.hostname) + monitoring_suffix;
  gpu_job_mapping_url = monitoring_prefix + monitoring_suffix;
  // Check for permission
  if (!get_bright_measurement("gpu_health_hostengine", gpu_monitoring_url)) {
    return false;
  }
  json_t result = json_t::parse(buf);
  initialized = result["data"].size();
  return true;
}


void measure_gpu(measure_gpu_result_t &results) {
  if (!initialized) {
    return;
  }
  if (!get_bright_measurement("job_gpu_utilization", gpu_job_mapping_url)) {
    return;
  }
  json_t result = json_t::parse(buf);
  //          gpu             updated    jobid
  std::map<uint32_t, std::pair<time_t, uint32_t>> gpu_job_mapping;
  for (auto &entry : result["data"]) {
    uint32_t jobid = -1;
    uint32_t gpu = -1;
    {
      std::string entity = entry["entity"];
      char *str = (char *) entity.c_str();
      DEBUGOUT_VERBOSE(fprintf(stderr, "Entry %s\n", str);)
      const char *start[2] = {str, NULL};
      bool is_value = 0;
      bool in_quote = 0;
      bool skip = 0;
      while (str) {
        const char c = *str;
        if (c == '\\') {
          str++;
        } else if (in_quote) {
          if (c == '"') {
            in_quote = 0;
            *str = '\0';
          }
        } else {
          if (c == ',' || c == '\0') {
            if (is_value) {
              const char *key = start[!is_value];
              const char *val = start[is_value];
              if (!strcmp(key, "hostname") && strcmp(val, worker.hostname)) {
                skip = 1;
                break;
              } else if (!strcmp(key, "job_id")) {
                jobid = atoi(val);
              } else if (!strcmp(key, "gpu")) {
                const char *cur = val;
                uint32_t val = 0;
                bool err = 0;
                while (*cur) {
                  if (!(*cur >= '0' && *cur <= '9')) {
                    err = 1;
                    break;
                  }
                  val = val * 10 + *(cur++) - '0';
                }
                if (err) {
                  skip = 1;
                  break;
                }
                gpu = val;
              }
            }
            if (c == ',') {
              is_value = 0;
              start[is_value] = str + 1;
            } else {
              break;
            }
          } else if (c == '=') {
            is_value = 1;
            start[is_value] = str + 1;
            *str = '\0';
          } else if (c == '"') {
            in_quote = 1;
            if (start[is_value] == str) {
              start[is_value]++;
            }
          }
        }
        str++;
      }
      DEBUGOUT_VERBOSE(
        fprintf(
          stderr, "=> %s %d %d\n", host.c_str(), jobid, gpu);
      )
      if (skip || gpu == -1U || jobid == -1U) {
        continue;
      }
    }
    time_t updated = entry["time"];
    if (gpu_job_mapping[gpu].first < updated) {
      gpu_job_mapping[gpu] = std::make_pair(updated, jobid);
    }
  }
  static const std::vector<std::string> metrics_to_fetch = {
    "gpu_power_usage",
    "gpu_temperature",
    "gpu_sm_clock",
    "gpu_utilization",
  };
  std::string entities = "";
  for (const auto &[gpu, _] : gpu_job_mapping) {
    for (const auto &metric : metrics_to_fetch) {
      entities +=
        metric + std::string(":gpu") + std::to_string(gpu) + std::string(",");
    }
  }
  if (!entities.size()) {
    return;
  }
  entities.pop_back();
  DEBUGOUT(fprintf(stderr, "entities: %s\n", entities.c_str());)
  if (!get_bright_measurement(entities, gpu_monitoring_url)) {
    return;
  }
  result = json_t::parse(buf);
  DEBUGOUT(fprintf(stderr, "-- %s", result.dump().c_str());)
  std::map<uint32_t, gpu_measurement_t> measurements;
  for (const auto &[gpu, pair] : gpu_job_mapping) {
    measurements[gpu].gpu_id = gpu;
    measurements[gpu].age = 0;
    measurements[gpu].step.job_id = pair.second;
  }
  for (const auto &entry : result["data"]) {
    const std::string measurable = entry["measurable"];
    std::string metric;
    {
      size_t colon_pos = 0;
      for (const char *cur = measurable.c_str(); *cur != ':'; cur++) {
        colon_pos++;
      }
      //              strlen("gpu_") = 4
      metric = measurable.substr(4, colon_pos - 4);
    }
    uint32_t gpu = 0;
    {
      uint32_t multiplier = 1;
      for (const char *cur = measurable.c_str() + measurable.size() - 1;
           *cur >= '0' && *cur <= '9'; cur++) {
        gpu += multiplier * (*cur - '0');
        multiplier *= 10;
      }
    }
    double val = entry["raw"];
    double age = entry["age"];
    DEBUGOUT(
      fprintf(stderr, "gpu=%d metric=%s val=%.2lf\n", gpu, metric.c_str(), val);
    )
    auto &measurement = measurements[gpu];
    measurement.age = std::max(measurement.age, (uint32_t)age);
    if (metric == "power_usage") {
      measurement.power_usage = val * 100;
    } else if (metric == "sm_clock") {
      measurement.sm_clock = val / 1e6;
    } else if (metric == "utilization") {
      measurement.util = val * 100;
    } else if (metric == "temperature") {
      measurement.temp = val;
    } else {
      fprintf(stderr, "error: unknown metric gpu_%s\n", metric.c_str());
    }
  }
  for (const auto &[_, result] : measurements) {
    results.push_back(result);
  }
}

void finalize_gpu_measurement() {
  curl_easy_cleanup(CURL_HANDLE_VAR);
  curl_easy_cleanup(curl_jsoncall_handle);
  curl_slist_free_all(jsoncall_header);
}
