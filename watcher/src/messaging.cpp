#include "messaging.h"

static std::queue<scrape_result_t> scrape_result_queue[2];
static std::queue<application_usage_t> application_usage_queue[2];
static std::queue<header_t> header_queue[2];
static char *buf;
static size_t buf_size;
static size_t buf_used;

int sock;
bool deduplicate;

std::atomic<bool> in_flip;
bool cur;
static thread_local bool is_my_flip;

void build_socket() {
  if ((sock = socket(SOCK_FAMILY, SOCK_TYPE, SOCK_PROTOCOL)) < 0) {
    perror("socket");
    exit(1);
  }
  sockaddr_in socket_addr;
  memset(&socket_addr, 0, sizeof(socket_addr));
  socket_addr.sin_family = AF_INET;
  socket_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (!getenv(DB_HOST_ENV)) {
    is_server = 1;
    const char *port = getenv(PORT_ENV);
    socket_addr.sin_port = htons(port ? atoi(port) : DEFAULT_PORT);
    if (bind(sock, (sockaddr *) &socket_addr, sizeof(socket_addr))) {
      perror("bind");
      exit(1);
    }
    if (listen(sock, SOCK_MAX_CONN)) {
      perror("listen");
      exit(1);
    }
  } else {
    if (!getenv(PORT_ENV)) {
      fputs("error: no port specified for database host\n", stderr);
      exit(1);
    }
  }
}

static inline void ensure_unused_size(size_t size) {
  bool updated = 0;
  while (buf_used + size > buf_size) {
    updated = 1;
    buf_size *= 2;
  }
  if (updated) {
    buf = (char *)realloc(buf, buf_size);
    memset(buf + buf_used, 0, buf_size - buf_used);
  }
}

static inline char *stage_str(const char *str) {
  if (!str) {
    return NULL;
  }
  if (!buf) {
    buf_size = INIT_BUF_SIZE * sizeof(char);
    buf = (char *)malloc(buf_size);
    memset(buf, 0, buf_size);
  }
  int len = strlen(str) + 1;
  if (deduplicate) {
    if (auto dest = (char *)memmem(buf, buf_used, str, len)) {
      return (char *)(dest - buf);
    }
  }
  ensure_unused_size(len);
  char *dest = buf + buf_used;
  strcpy(dest, str);
  buf_used += len;
  return (char *)(dest - buf);
}

void stage_message(scrape_result_t result, int queue_id) {
  bool cur_used = queue_id == -1 ? cur : queue_id;
  scrape_result_queue[cur_used].push(result);
}

void stage_message(application_usage_t usage, int queue_id) {
  bool cur_used = queue_id == -1 ? cur : queue_id;
  usage.app = stage_str(usage.app);
  application_usage_queue[cur_used].push(usage);
}

void freeze_queue() {
  bool in_flip_desired = 0;
  if (!in_flip.compare_exchange_strong(
      in_flip_desired, 1,
      std::memory_order_relaxed, std::memory_order_relaxed)) {
    return;
  }
  cur = !cur;
  is_my_flip = 1;
}

bool recombine_queue(result_group_t &result) {
  if (!in_flip || !is_my_flip) {
    return false;
  }
  bool cur = !::cur;
  bool ret = false;
  if (header_queue[cur].size()) {
    while (result.scrape_results.size()) {
      result.scrape_results.pop();
    }
    while (result.usages.size()) {
      result.usages.pop();
    }
    auto header = header_queue[cur].front();
    result.worker = header.worker;
    header_queue[cur].pop();
    for (uint32_t i = 0; i < header.result_cnt; i++) {
      result.scrape_results.push(scrape_result_queue[cur].front());
      scrape_result_queue[cur].pop();
    }
    for (uint32_t i = 0; i < header.usage_cnt; i++) {
      result.usages.push(application_usage_queue[cur].front());
      application_usage_queue[cur].pop();
    }
    result.buf = &buf;
    ret = true;
  }
  if (!header_queue[cur].size()) {
    is_my_flip = 0;
    in_flip = 0;
  }
  return ret;
}

static inline void takein(int from) {
  auto do_recv = [from](void *buf, size_t len) {
    size_t ret;
    if ((ret = recv(from, buf, len, 0)) < 0) {
      perror("recv");
    }
  };
  auto cur_copy = cur;
  header_t header;
  scrape_result_t result;
  application_usage_t usage;
  uint32_t len;
  char buf[INIT_BUF_SIZE];
  do_recv(&header, sizeof(header));
  if (header.hostname_len) {
    do_recv(buf, header.hostname_len);
    header.worker.hostname = stage_str(buf);
  }
  header_queue[cur_copy].push(header);
  for (uint32_t i = 0; i < header.result_cnt; i++) {
    do_recv(&result, sizeof(result));
    stage_message(result, cur_copy);
  }
  for (uint32_t i = 0; i < header.usage_cnt; i++) {
    do_recv(&usage, sizeof(usage));
    do_recv(&len, sizeof(len));
    do_recv(buf, len);
    usage.app = buf;
    stage_message(usage, cur_copy);
  }
  close(from);
}

void *conn_mgr(void *arg) {
  (void)arg;
  deduplicate = 1;
  while (1) {
    int incoming;
    if ((incoming = accept(sock, NULL, NULL)) < 0) {
      perror("accept");
      continue;
    }
    takein(incoming);
  }
}

// cur should not change throughout the function
void sendout() {
  static addrinfo addr_to_use;
  if (!addr_to_use.ai_addr) {
    addrinfo hint;
    addrinfo *result;
    const char *hostname = getenv(DB_HOST_ENV);
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = SOCK_FAMILY;
    hint.ai_socktype = SOCK_TYPE;
    hint.ai_protocol = SOCK_PROTOCOL;
    if (auto ret = getaddrinfo(hostname, getenv(PORT_ENV), &hint, &result)) {
      fprintf(stderr, "%s: %s\n", hostname, gai_strerror(ret));
      return;
    }
    addrinfo *addr = result;
    for (; addr; addr = addr->ai_next) {
      if (!connect(sock, addr->ai_addr, addr->ai_addrlen)) {
        addr_to_use = *addr;
        break;
      } else {
        DEBUGOUT(perror("try_connect"));
      }
    }
    if (!addr) {
      fprintf(stderr, "error: no viable address for host %s\n", hostname);
      return;
    }
  } else if (connect(sock, addr_to_use.ai_addr, addr_to_use.ai_addrlen)) {
    perror("connect");
    return;
  }
  header_t header;
  header.result_cnt = scrape_result_queue[cur].size();
  header.usage_cnt = application_usage_queue[cur].size();
  header.worker = worker;
  if (const auto &hostname = header.worker.hostname) {
    header.hostname_len = strlen(hostname) + 1;
  }
  // * 3: structure, len, string
  size_t tot = header.usage_cnt * 3 + header.result_cnt;
  auto do_send = [&](const void *buf, size_t len) {
    int ret;
    if ((ret = send(sock, buf, len, --tot == 0 ? 0 : MSG_MORE)) != len) {
      if (ret == -1) {
        perror("send");
      } else {
        fputs("error: incomplete data sent\n", stderr);
      }
    }
  };
  do_send(&header, sizeof(header));
  if (header.hostname_len) {
    do_send(header.worker.hostname, header.hostname_len);
  }
  while (!scrape_result_queue[cur].empty()) {
    const auto &front = scrape_result_queue[cur].front();
    do_send(&front, sizeof(front));
    scrape_result_queue[cur].pop();
  }
  while (!application_usage_queue[cur].empty()) {
    const auto &front = application_usage_queue[cur].front();
    do_send(&front, sizeof(front));
    const auto app_addr = (size_t)(front.app) + buf;
    uint32_t len = strlen(app_addr) + 1;
    if (len > INIT_BUF_SIZE) {
      len = INIT_BUF_SIZE;
      app_addr[len] = '\0';
    }
    do_send(&len, sizeof(len));
    do_send(app_addr, len);
    application_usage_queue[cur].pop();
  }
}

void dump_message() {
  for (int i = 0; i < 2; i++) {
    freeze_queue();
    sleep(5);
    if (header_queue[!cur].size()) {
      std::string dump_path = std::string(db_path) + ".turingwatch.dump.XXXXXX";
      int fd = mkstemp((char *)dump_path.c_str());
      write(fd, &header_queue[!cur].front(), sizeof(header_t));
      result_group_t result;
      recombine_queue(result);
      while (!result.scrape_results.empty()) {
        write(fd, &result.scrape_results.front(), sizeof(scrape_result_t));
        result.scrape_results.pop();
      }
      while (!result.usages.empty()) {
        write(fd, &result.usages.front(), sizeof(application_usage_t));
        result.usages.pop();
      }
      if (buf_used) {
        write(fd, &buf_size, sizeof(buf_size));
        write(fd, buf, buf_size);
      }
      close(fd);
    }
  }
}

void collect_dumped_messages() {
  assert(0 && "unimplemented");
}
