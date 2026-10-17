#include "k32.h"

#include <screw/process.h>
#include <screw/thread.h>

#include <sched.h>
#include <time.h>
#include <unistd.h>

#define CREATE_SUSPENDED 0x4u
#define STILL_ACTIVE 259u
#define TLS_OUT_OF_INDEXES 0xFFFFFFFFu

static HANDLE WINAPI CreateThread(void *sa, SIZE_T stack, thread_start start, LPVOID arg,
                                  DWORD flags, LPDWORD tid)
{
    return thread_spawn(start, arg, stack, flags & CREATE_SUSPENDED, tid);
}

static HANDLE WINAPI CreateRemoteThread(HANDLE proc, void *sa, SIZE_T stack, thread_start start,
                                        LPVOID arg, DWORD flags, LPDWORD tid)
{
    if (proc != CURRENT_PROCESS) {
        last_error_set(ERROR_ACCESS_DENIED);
        return NULL;
    }
    return CreateThread(sa, stack, start, arg, flags, tid);
}

static void WINAPI ExitThread(DWORD code) { thread_exit(code); }

static HANDLE WINAPI GetCurrentThread(void) { return CURRENT_THREAD; }
static DWORD WINAPI GetCurrentThreadId(void) { return teb_current()->thread_id; }
static HANDLE WINAPI GetCurrentProcess(void) { return CURRENT_PROCESS; }
static DWORD WINAPI GetCurrentProcessId(void) { return getpid(); }

static DWORD WINAPI GetThreadId(HANDLE h)
{
    struct thread *t = thread_get(h);
    return t ? t->tid : 0;
}

static DWORD WINAPI GetProcessId(HANDLE h) { return h == CURRENT_PROCESS ? (DWORD)getpid() : 0; }

static BOOL WINAPI GetExitCodeThread(HANDLE h, LPDWORD code)
{
    struct thread *t = thread_get(h);
    if (!t)
        return fail(ERROR_INVALID_HANDLE);
    *code = __atomic_load_n(&t->w.count, __ATOMIC_ACQUIRE) ? t->w.exit_code : STILL_ACTIVE;
    return TRUE;
}

static DWORD WINAPI ResumeThread(HANDLE h)
{
    struct thread *t = thread_get(h);
    if (!t) {
        last_error_set(ERROR_INVALID_HANDLE);
        return (DWORD)-1;
    }
    return thread_resume(t);
}

static DWORD WINAPI SuspendThread(HANDLE h)
{
    last_error_set(ERROR_NOT_SUPPORTED);
    return (DWORD)-1;
}

static BOOL WINAPI SetThreadPriority(HANDLE h, int priority)
{
    struct thread *t = thread_get(h);
    if (!t)
        return fail(ERROR_INVALID_HANDLE);
    t->priority = priority;
    return TRUE;
}

static int WINAPI GetThreadPriority(HANDLE h)
{
    struct thread *t = thread_get(h);
    return t ? t->priority : 0x7FFFFFFF;
}

static BOOL WINAPI SetThreadPriorityBoost(HANDLE h, BOOL disable) { return TRUE; }
static DWORD_PTR WINAPI SetThreadAffinityMask(HANDLE h, DWORD_PTR mask) { return ~(DWORD_PTR)0 >> 1; }
static DWORD WINAPI SetThreadIdealProcessor(HANDLE h, DWORD cpu) { return 0; }

static LONG WINAPI SetThreadDescription(HANDLE h, LPCWSTR desc)
{
    struct thread *t = thread_get(h);
    if (t) {
        char *name = wide_to_host(desc);
        name[strnlen(name, 15)] = 0;
        pthread_setname_np(t->pt, name);
        free(name);
    }
    return 0;
}

static BOOL WINAPI SwitchToThread(void) { return sched_yield() == 0; }

static void sleep_ms(DWORD ms)
{
    if (!ms) {
        sched_yield();
        return;
    }
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000l };
    while (nanosleep(&ts, &ts) && errno == EINTR)
        ;
}

static void WINAPI Sleep(DWORD ms) { sleep_ms(ms == INFINITE ? 0x7FFFFFFF : ms); }

static DWORD WINAPI SleepEx(DWORD ms, BOOL alertable)
{
    Sleep(ms);
    return 0;
}

static void WINAPI ExitProcess(UINT code) { process_exit(code); }

static BOOL WINAPI TerminateProcess(HANDLE h, UINT code)
{
    if (h == CURRENT_PROCESS)
        process_exit(code);
    return fail(ERROR_ACCESS_DENIED);
}

static DWORD WINAPI TlsAlloc(void) { return tls_slot_alloc(NULL); }
static BOOL WINAPI TlsFree(DWORD i) { return tls_slot_free(i) ? TRUE : fail(ERROR_INVALID_PARAMETER); }

static LPVOID WINAPI TlsGetValue(DWORD i)
{
    last_error_set(ERROR_SUCCESS);
    return tls_slot_get(i);
}

static BOOL WINAPI TlsSetValue(DWORD i, LPVOID v) { return tls_slot_set(i, v) ? TRUE : fail(ERROR_INVALID_PARAMETER); }

static DWORD WINAPI FlsAlloc(fls_callback cb) { return tls_slot_alloc(cb); }
static BOOL WINAPI FlsFree(DWORD i) { return TlsFree(i); }
static LPVOID WINAPI FlsGetValue(DWORD i) { return TlsGetValue(i); }
static BOOL WINAPI FlsSetValue(DWORD i, LPVOID v) { return TlsSetValue(i, v); }

static BOOL WINAPI GetThreadTimes(HANDLE h, FILETIME *created, FILETIME *exited, FILETIME *kernel, FILETIME *user)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    *created = *exited = *kernel = (FILETIME){ 0, 0 };
    uint64_t t = ts.tv_sec * 10000000ull + ts.tv_nsec / 100;
    *user = filetime_from_ticks(t);
    return TRUE;
}

static BOOL WINAPI GetProcessTimes(HANDLE h, FILETIME *created, FILETIME *exited, FILETIME *kernel, FILETIME *user)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    *created = *exited = *kernel = (FILETIME){ 0, 0 };
    *user = filetime_from_ticks(ts.tv_sec * 10000000ull + ts.tv_nsec / 100);
    return TRUE;
}

static BOOL WINAPI SetPriorityClass(HANDLE h, DWORD cls) { return TRUE; }
static DWORD WINAPI GetPriorityClass(HANDLE h) { return 0x20; }

static BOOL WINAPI GetProcessAffinityMask(HANDLE h, DWORD_PTR *proc, DWORD_PTR *sys)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    DWORD_PTR mask = n >= 64 ? ~(DWORD_PTR)0 : ((DWORD_PTR)1 << n) - 1;
    *proc = *sys = mask;
    return TRUE;
}

static DWORD WINAPI GetCurrentProcessorNumber(void) { return sched_getcpu(); }

static BOOL WINAPI SetProcessAffinityMask(HANDLE h, DWORD_PTR mask) { return mask != 0; }

static BOOL WINAPI ProcessIdToSessionId(DWORD pid, DWORD *session)
{
    if (!session)
        return fail(ERROR_INVALID_PARAMETER);
    *session = 0;
    return TRUE;
}

BIT_TABLE(k32_thread,
          BIT_FN(CreateThread), BIT_FN(CreateRemoteThread), BIT_FN(ExitThread),
          BIT_FN(GetCurrentThread), BIT_FN(GetCurrentThreadId),
          BIT_FN(GetCurrentProcess), BIT_FN(GetCurrentProcessId),
          BIT_FN(GetThreadId), BIT_FN(GetProcessId), BIT_FN(GetExitCodeThread),
          BIT_FN(ResumeThread), BIT_FN(SuspendThread),
          BIT_FN(SetThreadPriority), BIT_FN(GetThreadPriority), BIT_FN(SetThreadPriorityBoost),
          BIT_FN(SetThreadAffinityMask), BIT_FN(SetThreadIdealProcessor), BIT_FN(SetThreadDescription),
          BIT_FN(SwitchToThread), BIT_FN(Sleep), BIT_FN(SleepEx),
          BIT_FN(ExitProcess), BIT_FN(TerminateProcess),
          BIT_FN(TlsAlloc), BIT_FN(TlsFree), BIT_FN(TlsGetValue), BIT_FN(TlsSetValue),
          BIT_FN(FlsAlloc), BIT_FN(FlsFree), BIT_FN(FlsGetValue), BIT_FN(FlsSetValue),
          BIT_FN(GetThreadTimes), BIT_FN(GetProcessTimes),
          BIT_FN(SetPriorityClass), BIT_FN(GetPriorityClass),
          BIT_FN(GetProcessAffinityMask), BIT_FN(SetProcessAffinityMask), BIT_FN(GetCurrentProcessorNumber),
          BIT_FN(ProcessIdToSessionId));
