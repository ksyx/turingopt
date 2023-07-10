#ifndef _TURINGWATCHER_MAIN_H
#define _TURINGWATCHER_MAIN_H
#include "common.h"
#include "tresdef.h"

struct tres_t {
  size_t value[TRES_SIZE];
  // comma delimitered string of form index=value
  tres_t(const char *tres_str);
  tres_t() {
    memset(value, 0, sizeof(value));
  }
  tres_t &operator +=(const tres_t &rhs) {
    for (int i = 0; i < TRES_SIZE; i++)
      value[i] += rhs.value[i];
    return *this;
  }
  void print();
};
#endif
