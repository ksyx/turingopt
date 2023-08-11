#ifndef _TURINGWATCHER_MESSAGING_H
#define _TURINGWATCHER_MESSAGING_H
#include "common.h"
#include "sql.h"

#include <atomic>

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

constexpr protocol_version_t protocol_version = 1;

/*
  APPEND ONLY. DO NOT MODIFY/REMOVE OLD PROTOCOL DESCRIPTION ONCE IN PRODUCTION.

  ALL STRUCTURES ARE DEFINED AS IN THE LAST COMMIT BEFORE PROTOCOL VERSION
  CHANGES; BLAME THE PROTOCOL VERSION LINE TO IDENTIFY THE COMMIT

  VERTICAL BARS SEPARATE SENDS

  ======================== PROTOCOL VERSION 1    BEGIN =========================

  CLIENT_MAGIC  = 0xAC1DBEEF
  SERVER_MAGIC  = 0xEAC1CAFE
  CONFIRM_MAGIC = 0x9A11SED9

  ... After exchanging and verifing magic values in uint32_t type ...
  [header_t][hostname_str][scrape_result_t] ... continues next line ...
                          ~~ *result_cnt ~~
  ... continues here ... [application_usage_t(1)][uint32_t app_len(2)][app]
                         ~~~~~~~~~~~~~~~~~   *usage_cnt   ~~~~~~~~~~~~~~~~~

 ... Server sends CONFIRM_MAGIC ...

  (1) the pointer const char *app should be ignored and overwritten to the
      first byte immediately following the structure.
  (2) length includes terminating \0, guaranteed to be less than INIT_BUF_SIZE

  ======================== PROTOCOL VERSION 1    END   =========================
*/

#define TASK_COMM_LEN 32

#define INIT_BUF_SIZE 4096
#define DEFAULT_PORT 3755
#define SOCK_MAX_CONN 32

#define SOCK_FAMILY PF_INET
#define SOCK_TYPE SOCK_STREAM
#define SOCK_PROTOCOL IPPROTO_TCP

typedef uint32_t turing_watch_comm_magic_t;
const turing_watch_comm_magic_t client_magic = 0xAC1DBEEF;
const turing_watch_comm_magic_t server_magic = 0xEAC1CAFE;
const turing_watch_comm_magic_t confirmation_magic = 0x9A115ED9; // 9 ALLSET 9

struct header_t {
  const protocol_version_t protocol_ver = protocol_version;
  const protocol_version_t schema_ver = schema_version;
  uint32_t result_cnt;
  uint32_t usage_cnt;
  // Length includes terminating \0
  uint32_t hostname_len;
  worker_info_t worker;
};

struct scrape_result_t {
  slurm_step_id_t step;
  char comm[TASK_COMM_LEN + 1];
  pid_t pid;
  size_t res;

  size_t minor_pagefault;
  size_t cminor_pagefault;

  time_t utime;
  time_t cutime;
  time_t stime;
  time_t cstime;

  /* Privileged info, -1 == NULL */
  size_t rchar;
  size_t wchar;

  void print(bool report_child = 1) const {
    fprintf(stderr, "%s pid=%d res=%ld minor=%ld",
      comm, pid, res, minor_pagefault);
    if (report_child) {
      fprintf(stderr, " cminor=%ld", cminor_pagefault);
    }
    fprintf(stderr, " utime=%ld stime=%ld", utime, stime);
    if (report_child) {
      fprintf(stderr, " cutime=%ld cstime=%ld", cutime, cstime);
    }
    fprintf(stderr, " rchar=%ld wchar=%ld\n", rchar, wchar);
  }
};

struct application_usage_t {
  slurm_step_id_t step;
  const char *app;
};

struct result_group_t {
  worker_info_t worker;
  char **buf;
  std::queue<scrape_result_t> scrape_results;
  std::queue<application_usage_t> usages;
};

void build_socket();
void stage_message(scrape_result_t result, int queue_id = -1);
void stage_message(application_usage_t usage, int queue_id = -1);
void freeze_queue();
bool recombine_queue(result_group_t &result);
void sendout();
void *conn_mgr(void *arg);
// In case of sendout fail / crash with unprocessed queue element
void dump_message();
void collect_dumped_messages();
#endif