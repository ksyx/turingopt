#ifndef _TURINGWATCH_WORKER_INTERFACE_H
#define _TURINGWATCH_WORKER_INTERFACE_H
void scraper(const char *argv_0);
void watcher();
void parent(int argc, char *argv[]);
void worker_finalize();
#endif
