#ifndef THREADS_H
#define THREADS_H

typedef void (*tc_task_fn)(void *arg, int worker);

void tc_parallel(int n, tc_task_fn fn, void *arg);

long tc_atomic_next(volatile long *counter);
int tc_cpu_count(void);

#endif
