#ifndef _TURINGWATCH_WORKER_INTERFACE_H
#define _TURINGWATCH_WORKER_INTERFACE_H
void scraper();
void watcher();
void *node_watcher_distributor(void *arg);
void parent(int argc, char *argv[]);
void worker_finalize();
#endif
