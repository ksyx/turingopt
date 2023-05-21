#ifndef _TURING_DEBUGREG_H
#define _TURING_DEBUGREG_H
#include <limits.h>
#include <stdint.h>
#include <sys/user.h>

// Magic calculation originally written by Hallvard B. Furuseth for any
// (1<<k)-1 where 0 <= k < 2040 https://stackoverflow.com/a/51617465/6739351
// https://groups.google.com/g/comp.lang.c/c/1kiXXt5T0TQ/m/S_B_8D4VmOkJ
#define MAXBITS(m) \
  ((m) / ((m) % 255 + 1) / 255 % 255 * 8 + 7 - 86 / ((m) % 255 + 12))
#define PTRBYTES MAXBITS(UINTPTR_MAX) / 8

#define DR_OFFSET(x) (((struct user *)(NULL))->u_debugreg + x)

typedef struct {
  unsigned int  dr0_local:      1;  
  unsigned int  dr0_global:     1;  
  unsigned int  dr1_local:      1;  
  unsigned int  dr1_global:     1;  
  unsigned int  dr2_local:      1;  
  unsigned int  dr2_global:     1;  
  unsigned int  dr3_local:      1;  
  unsigned int  dr3_global:     1;  
  unsigned int  le:             1;  
  unsigned int  ge:             1;  
  unsigned int  reserved_10:    1;  
  unsigned int  rtm:            1;  
  unsigned int  reserved_12:    1;  
  unsigned int  gd:             1;  
  unsigned int  reserved_14_15: 2;
  unsigned int  dr0_break:      2;  
  unsigned int  dr0_len:        2;  
  unsigned int  dr1_break:      2;  
  unsigned int  dr1_len:        2;  
  unsigned int  dr2_break:      2;  
  unsigned int  dr2_len:        2;  
  unsigned int  dr3_break:      2;  
  unsigned int  dr3_len:        2;
} dr7_t;
#endif