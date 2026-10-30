//
// Created by Bradley Pearce on 05/02/2026.
//

#ifndef MYLO_LANG_OS_JOB_H
#define MYLO_LANG_OS_JOB_H

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
    #define popen _popen
    #define pclose _pclose
    #define SLEEP_MS(x) Sleep(x)
    // Simple Mutex for Windows
    static CRITICAL_SECTION job_mutex;
static int mutex_initialized = 0;
#define LOCK_JOBS if(!mutex_initialized) { InitializeCriticalSection(&job_mutex); mutex_initialized=1; } EnterCriticalSection(&job_mutex)
#define UNLOCK_JOBS LeaveCriticalSection(&job_mutex)
#elif defined(__AMIGA__)
    #include <unistd.h>
    #define popen(c, m) NULL
    #define pclose(f) (-1)
    #define SLEEP_MS(x) usleep((x)*1000)
    #define LOCK_JOBS
    #define UNLOCK_JOBS
    typedef int pthread_t;
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>
#define SLEEP_MS(x) usleep((x)*1000)
// Simple Mutex for POSIX
static pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK_JOBS pthread_mutex_lock(&job_mutex)
#define UNLOCK_JOBS pthread_mutex_unlock(&job_mutex)
#endif

#define MAX_JOBS 32

typedef struct {
    char name[64];
    char *cmd;
    char *out_res;
    char *err_res;
    int status; // 0=Running, 1=Done, -1=Error
    int active;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
} Job;

static Job job_registry[MAX_JOBS];
void internal_exec_command(const char* cmd, char** out_str, char** err_str);

#ifdef _WIN32
unsigned __stdcall job_worker(void* arg);
#else
void* job_worker(void* arg);
#endif
#endif //MYLO_LANG_OS_JOB_H