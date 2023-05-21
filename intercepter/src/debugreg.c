#include "debugreg.h"

const dr7_t dr7_exec_on_dr0 = {
  .le = 1,
  .ge = 1,
  .reserved_10 = 1,

  .dr0_local = 1,
  .dr0_len = 
  #if 1
  // Really!
  0
  #else
  #if PTRBYTES == 4
  3
  #elif PTRBYTES == 8
  2
  #elif PTRBYTES == 2
  // This machine is old enough though
  1
  #elif PTRBYTES == 1
  // Really?
  0
  #else
  2
  #ifndef DEBUGREG_BEST_EFFORT
  #error Unsupported pointer width
  #endif
  #endif
  #endif
  ,
  .dr0_break = 0,
},
dr7_nothing = {
  .le = 1,
  .ge = 1,
  .reserved_10 = 1,
}
;