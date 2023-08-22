#ifndef _TURINGWATCHER_GPU_PROVIDER_BRIGHT_H
#define _TURINGWATCHER_GPU_PROVIDER_BRIGHT_H
#include "gpu/interface.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
using json_t = nlohmann::json;

#define CURL_HANDLE_VAR curl_handle
#define CURLRET_VAR curl_ret
#define CURL_LIST_APPEND(LIST, VAL) LIST = curl_slist_append(LIST, VAL)
#define CURL_SET_OPT(OPT, VAL) curl_easy_setopt(CURL_HANDLE_VAR, OPT, VAL)
#define IS_CURL_OK EXPECT_EQUAL(CURLRET_VAR, CURLE_OK)
#define CURL_PERROR(OP) \
  fprintf(stderr, "curl_easy_%s: %s\n", OP, curl_easy_strerror(CURLRET_VAR));
#endif
