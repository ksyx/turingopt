#ifndef _TURING_MRECYCLE_INTERFACE_H
#define _TURING_MRECYCLE_INTERFACE_H
#ifdef __cplusplus
#define MRECYCLE_LINKAGE extern "C"
#else
#define MRECYCLE_LINKAGE ;
#endif
MRECYCLE_LINKAGE bool recycle(void *addr, size_t size, size_t alignment);
MRECYCLE_LINKAGE void *reuse(size_t size, size_t alignment);
#endif
