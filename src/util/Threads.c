#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "Threads.h"
#include <stdlib.h>

typedef struct { tc_task_fn fn; void *arg; int worker; } Job;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

static unsigned __stdcall run_job(void *p) {
    Job *j = (Job *)p;
    j->fn(j->arg, j->worker);
    return 0;
}

static void spread_over_groups(HANDLE h, int index) {
    WORD groups = GetActiveProcessorGroupCount();
    if (groups <= 1) return;
    DWORD total = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (total == 0) return;
    DWORD slot = (DWORD)index % total;
    for (WORD g = 0; g < groups; g++) {
        DWORD count = GetActiveProcessorCount(g);
        if (slot < count) {
            GROUP_AFFINITY ga;
            ZeroMemory(&ga, sizeof ga);
            ga.Group = g;
            ga.Mask = count >= sizeof(KAFFINITY) * 8 ? ~(KAFFINITY)0 : (((KAFFINITY)1 << count) - 1);
            SetThreadGroupAffinity(h, &ga, NULL);
            return;
        }
        slot -= count;
    }
}

void tc_parallel(int n, tc_task_fn fn, void *arg) {
    if (n < 1) n = 1;
    HANDLE *h = (HANDLE *)malloc(sizeof(HANDLE) * n);
    Job *jobs = (Job *)malloc(sizeof(Job) * n);
    if (!h || !jobs) {
        free(h); free(jobs);
        for (int i = 0; i < n; i++) fn(arg, i);
        return;
    }
    for (int i = 0; i < n; i++) {
        jobs[i].fn = fn; jobs[i].arg = arg; jobs[i].worker = i;
        h[i] = (HANDLE)_beginthreadex(NULL, 0, run_job, &jobs[i], CREATE_SUSPENDED, NULL);
        if (h[i]) {
            spread_over_groups(h[i], i);
            ResumeThread(h[i]);
        }
    }
    for (int i = 0; i < n; i++) {
        if (!h[i]) run_job(&jobs[i]);
    }
    for (int i = 0; i < n; i++) {
        if (h[i]) {
            WaitForSingleObject(h[i], INFINITE);
            CloseHandle(h[i]);
        }
    }
    free(h);
    free(jobs);
}

long tc_atomic_next(volatile long *counter) { return InterlockedIncrement(counter) - 1; }
int tc_cpu_count(void) {
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (n == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        n = si.dwNumberOfProcessors;
    }
    return n > 0 ? (int)n : 1;
}

#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

static void *run_job(void *p) {
    Job *j = (Job *)p;
    j->fn(j->arg, j->worker);
    return NULL;
}

void tc_parallel(int n, tc_task_fn fn, void *arg) {
    if (n < 1) n = 1;
    pthread_t *h = (pthread_t *)malloc(sizeof(pthread_t) * n);
    char *started = (char *)malloc((size_t)n);
    Job *jobs = (Job *)malloc(sizeof(Job) * n);
    if (!h || !started || !jobs) {
        free(h); free(started); free(jobs);
        for (int i = 0; i < n; i++) fn(arg, i);
        return;
    }
    for (int i = 0; i < n; i++) {
        jobs[i].fn = fn; jobs[i].arg = arg; jobs[i].worker = i;
        started[i] = pthread_create(&h[i], NULL, run_job, &jobs[i]) == 0;
    }
    for (int i = 0; i < n; i++) {
        if (!started[i]) run_job(&jobs[i]);
    }
    for (int i = 0; i < n; i++) {
        if (started[i]) pthread_join(h[i], NULL);
    }
    free(h);
    free(started);
    free(jobs);
}

long tc_atomic_next(volatile long *counter) { return __atomic_fetch_add(counter, 1, __ATOMIC_SEQ_CST); }
int tc_cpu_count(void) {
#if defined(__linux__) && defined(CPU_COUNT)
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof set, &set) == 0) {
        int c = CPU_COUNT(&set);
        if (c > 0) return c;
    }
#endif
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}
#endif
