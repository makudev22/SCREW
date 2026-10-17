#include "k32.h"

#include <screw/sync.h>

#include <limits.h>
#include <pthread.h>

#define ERROR_TIMEOUT 1460
#define CREATE_EVENT_MANUAL_RESET 0x1u
#define CREATE_EVENT_INITIAL_SET 0x2u
#define CREATE_MUTEX_INITIAL_OWNER 0x1u
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x1u
#define SRW_WRITER 0x80000000u
#define INIT_ONCE_CHECK_ONLY 0x1u
#define INIT_ONCE_INIT_FAILED 0x4u

typedef struct {
    void *debug;
    LONG lock;
    LONG recursion;
    HANDLE owner;
    HANDLE semaphore;
    ULONG_PTR spin;
} CRITICAL_SECTION;

typedef struct {
    uint32_t state;
    uint32_t pad;
} SRWLOCK;

typedef struct {
    uint32_t seq;
    uint32_t pad;
} CONDITION_VARIABLE;

typedef struct {
    uint32_t state;
    uint32_t pad;
} INIT_ONCE;

typedef struct slist_entry {
    struct slist_entry *next;
} SLIST_ENTRY;

typedef struct {
    ULONGLONG depth;
    SLIST_ENTRY *first;
} __attribute__((aligned(16))) SLIST_HEADER;

typedef BOOL WINAPI (*init_once_fn)(INIT_ONCE *once, void *param, void **ctx);

static uint64_t me(void) { return teb_current()->thread_id; }

static void cs_lock(CRITICAL_SECTION *cs)
{
    int32_t c = 0;
    if (__atomic_compare_exchange_n(&cs->lock, &c, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return;
    for (int spin = 0; spin < 100; spin++) {
        c = 0;
        if (__atomic_compare_exchange_n(&cs->lock, &c, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
        __builtin_ia32_pause();
    }
    if (c != 2)
        c = __atomic_exchange_n(&cs->lock, 2, __ATOMIC_ACQUIRE);
    while (c) {
        futex_wait(&cs->lock, 2, INFINITE);
        c = __atomic_exchange_n(&cs->lock, 2, __ATOMIC_ACQUIRE);
    }
}

static void cs_unlock(CRITICAL_SECTION *cs)
{
    if (__atomic_fetch_sub(&cs->lock, 1, __ATOMIC_RELEASE) != 1) {
        __atomic_store_n(&cs->lock, 0, __ATOMIC_RELEASE);
        futex_wake(&cs->lock, 1);
    }
}

static void WINAPI InitializeCriticalSection(CRITICAL_SECTION *cs) { memset(cs, 0, sizeof *cs); }

static BOOL WINAPI InitializeCriticalSectionAndSpinCount(CRITICAL_SECTION *cs, DWORD spin)
{
    InitializeCriticalSection(cs);
    return TRUE;
}

static BOOL WINAPI InitializeCriticalSectionEx(CRITICAL_SECTION *cs, DWORD spin, DWORD flags)
{
    InitializeCriticalSection(cs);
    return TRUE;
}

static void WINAPI DeleteCriticalSection(CRITICAL_SECTION *cs) {}

static void WINAPI EnterCriticalSection(CRITICAL_SECTION *cs)
{
    uint64_t t = me();
    if ((uint64_t)cs->owner == t) {
        cs->recursion++;
        return;
    }
    cs_lock(cs);
    cs->owner = (HANDLE)t;
    cs->recursion = 1;
}

static BOOL WINAPI TryEnterCriticalSection(CRITICAL_SECTION *cs)
{
    uint64_t t = me();
    if ((uint64_t)cs->owner == t) {
        cs->recursion++;
        return TRUE;
    }
    int32_t c = 0;
    if (!__atomic_compare_exchange_n(&cs->lock, &c, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return FALSE;
    cs->owner = (HANDLE)t;
    cs->recursion = 1;
    return TRUE;
}

static void WINAPI LeaveCriticalSection(CRITICAL_SECTION *cs)
{
    if (--cs->recursion > 0)
        return;
    cs->owner = NULL;
    cs_unlock(cs);
}

static DWORD WINAPI SetCriticalSectionSpinCount(CRITICAL_SECTION *cs, DWORD spin) { return 0; }

static void WINAPI InitializeSRWLock(SRWLOCK *l) { l->state = 0; }

static void WINAPI AcquireSRWLockExclusive(SRWLOCK *l)
{
    for (;;) {
        uint32_t s = 0;
        if (__atomic_compare_exchange_n(&l->state, &s, SRW_WRITER, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
        futex_wait(&l->state, s, INFINITE);
    }
}

static void WINAPI AcquireSRWLockShared(SRWLOCK *l)
{
    for (;;) {
        uint32_t s = __atomic_load_n(&l->state, __ATOMIC_RELAXED);
        if (!(s & SRW_WRITER)) {
            if (__atomic_compare_exchange_n(&l->state, &s, s + 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                return;
            continue;
        }
        futex_wait(&l->state, s, INFINITE);
    }
}

static BOOL WINAPI TryAcquireSRWLockExclusive(SRWLOCK *l)
{
    uint32_t s = 0;
    return __atomic_compare_exchange_n(&l->state, &s, SRW_WRITER, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static BOOL WINAPI TryAcquireSRWLockShared(SRWLOCK *l)
{
    uint32_t s = __atomic_load_n(&l->state, __ATOMIC_RELAXED);
    while (!(s & SRW_WRITER))
        if (__atomic_compare_exchange_n(&l->state, &s, s + 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return TRUE;
    return FALSE;
}

static void WINAPI ReleaseSRWLockExclusive(SRWLOCK *l)
{
    __atomic_store_n(&l->state, 0, __ATOMIC_RELEASE);
    futex_wake(&l->state, INT_MAX);
}

static void WINAPI ReleaseSRWLockShared(SRWLOCK *l)
{
    if (__atomic_sub_fetch(&l->state, 1, __ATOMIC_RELEASE) == 0)
        futex_wake(&l->state, INT_MAX);
}

static void WINAPI InitializeConditionVariable(CONDITION_VARIABLE *cv) { cv->seq = 0; }

static void WINAPI WakeConditionVariable(CONDITION_VARIABLE *cv)
{
    __atomic_add_fetch(&cv->seq, 1, __ATOMIC_RELEASE);
    futex_wake(&cv->seq, 1);
}

static void WINAPI WakeAllConditionVariable(CONDITION_VARIABLE *cv)
{
    __atomic_add_fetch(&cv->seq, 1, __ATOMIC_RELEASE);
    futex_wake(&cv->seq, INT_MAX);
}

static BOOL WINAPI SleepConditionVariableCS(CONDITION_VARIABLE *cv, CRITICAL_SECTION *cs, DWORD ms)
{
    uint32_t seq = __atomic_load_n(&cv->seq, __ATOMIC_ACQUIRE);
    LONG rec = cs->recursion;
    cs->recursion = 1;
    LeaveCriticalSection(cs);
    int r = futex_wait(&cv->seq, seq, ms);
    EnterCriticalSection(cs);
    cs->recursion = rec;
    return r ? fail(ERROR_TIMEOUT) : TRUE;
}

static BOOL WINAPI SleepConditionVariableSRW(CONDITION_VARIABLE *cv, SRWLOCK *l, DWORD ms, ULONG flags)
{
    bool shared = flags & CONDITION_VARIABLE_LOCKMODE_SHARED;
    uint32_t seq = __atomic_load_n(&cv->seq, __ATOMIC_ACQUIRE);
    if (shared)
        ReleaseSRWLockShared(l);
    else
        ReleaseSRWLockExclusive(l);
    int r = futex_wait(&cv->seq, seq, ms);
    if (shared)
        AcquireSRWLockShared(l);
    else
        AcquireSRWLockExclusive(l);
    return r ? fail(ERROR_TIMEOUT) : TRUE;
}

struct named {
    char *name;
    struct object *obj;
    struct named *next;
};

static struct named *names;
static pthread_mutex_t names_lock = PTHREAD_MUTEX_INITIALIZER;

static HANDLE open_named(const char *name)
{
    HANDLE h = NULL;
    pthread_mutex_lock(&names_lock);
    for (struct named *n = names; n; n = n->next)
        if (!strcmp(n->name, name)) {
            object_ref(n->obj);
            h = handle_new(n->obj);
            break;
        }
    pthread_mutex_unlock(&names_lock);
    return h;
}

static HANDLE publish(const char *name, struct waitable *w)
{
    if (name && *name) {
        HANDLE h = open_named(name);
        if (h) {
            object_unref(&w->base);
            last_error_set(ERROR_ALREADY_EXISTS);
            return h;
        }
        struct named *n = malloc(sizeof *n);
        n->name = strdup(name);
        n->obj = &w->base;
        object_ref(&w->base);
        pthread_mutex_lock(&names_lock);
        n->next = names;
        names = n;
        pthread_mutex_unlock(&names_lock);
    }
    last_error_set(ERROR_SUCCESS);
    return handle_new(&w->base);
}

static HANDLE publish_w(LPCWSTR name, struct waitable *w)
{
    char *n = wide_to_host(name);
    HANDLE h = publish(n, w);
    free(n);
    return h;
}

static HANDLE open_w(LPCWSTR name)
{
    char *n = wide_to_host(name);
    HANDLE h = n ? open_named(n) : NULL;
    free(n);
    if (!h)
        last_error_set(ERROR_FILE_NOT_FOUND);
    return h;
}

static HANDLE open_a(LPCSTR name)
{
    HANDLE h = name ? open_named(name) : NULL;
    if (!h)
        last_error_set(ERROR_FILE_NOT_FOUND);
    return h;
}

static struct waitable *event(BOOL manual, BOOL initial)
{
    return waitable_new(WAIT_EVENT, initial ? 1 : 0, 1, manual);
}

static HANDLE WINAPI CreateEventA(void *sa, BOOL manual, BOOL initial, LPCSTR name)
{
    return publish(name, event(manual, initial));
}

static HANDLE WINAPI CreateEventW(void *sa, BOOL manual, BOOL initial, LPCWSTR name)
{
    return publish_w(name, event(manual, initial));
}

static HANDLE WINAPI CreateEventExA(void *sa, LPCSTR name, DWORD flags, DWORD access)
{
    return publish(name, event(flags & CREATE_EVENT_MANUAL_RESET, flags & CREATE_EVENT_INITIAL_SET));
}

static HANDLE WINAPI CreateEventExW(void *sa, LPCWSTR name, DWORD flags, DWORD access)
{
    return publish_w(name, event(flags & CREATE_EVENT_MANUAL_RESET, flags & CREATE_EVENT_INITIAL_SET));
}

static HANDLE WINAPI OpenEventA(DWORD access, BOOL inherit, LPCSTR name) { return open_a(name); }
static HANDLE WINAPI OpenEventW(DWORD access, BOOL inherit, LPCWSTR name) { return open_w(name); }

static struct waitable *typed(HANDLE h, enum waitable_type type)
{
    struct waitable *w = waitable_get(h);
    return w && w->type == type ? w : NULL;
}

static BOOL WINAPI SetEvent(HANDLE h)
{
    struct waitable *w = typed(h, WAIT_EVENT);
    if (!w)
        return fail(ERROR_INVALID_HANDLE);
    waitable_set(w, 1);
    return TRUE;
}

static BOOL WINAPI ResetEvent(HANDLE h)
{
    struct waitable *w = typed(h, WAIT_EVENT);
    if (!w)
        return fail(ERROR_INVALID_HANDLE);
    waitable_set(w, 0);
    return TRUE;
}

static BOOL WINAPI PulseEvent(HANDLE h)
{
    struct waitable *w = typed(h, WAIT_EVENT);
    if (!w)
        return fail(ERROR_INVALID_HANDLE);
    waitable_set(w, 1);
    waitable_set(w, 0);
    return TRUE;
}

static struct waitable *mutex(BOOL owned)
{
    struct waitable *w = waitable_new(WAIT_MUTEX, 0, 1, false);
    if (owned) {
        w->owner = me();
        w->recursion = 1;
    }
    return w;
}

static HANDLE WINAPI CreateMutexA(void *sa, BOOL owned, LPCSTR name) { return publish(name, mutex(owned)); }
static HANDLE WINAPI CreateMutexW(void *sa, BOOL owned, LPCWSTR name) { return publish_w(name, mutex(owned)); }

static HANDLE WINAPI CreateMutexExA(void *sa, LPCSTR name, DWORD flags, DWORD access)
{
    return publish(name, mutex(flags & CREATE_MUTEX_INITIAL_OWNER));
}

static HANDLE WINAPI CreateMutexExW(void *sa, LPCWSTR name, DWORD flags, DWORD access)
{
    return publish_w(name, mutex(flags & CREATE_MUTEX_INITIAL_OWNER));
}

static HANDLE WINAPI OpenMutexA(DWORD access, BOOL inherit, LPCSTR name) { return open_a(name); }
static HANDLE WINAPI OpenMutexW(DWORD access, BOOL inherit, LPCWSTR name) { return open_w(name); }

static BOOL WINAPI ReleaseMutex(HANDLE h)
{
    struct waitable *w = typed(h, WAIT_MUTEX);
    if (!w)
        return fail(ERROR_INVALID_HANDLE);
    return waitable_unlock(w) ? TRUE : fail(ERROR_ACCESS_DENIED);
}

static HANDLE WINAPI CreateSemaphoreA(void *sa, LONG initial, LONG max, LPCSTR name)
{
    return publish(name, waitable_new(WAIT_SEMAPHORE, initial, max, false));
}

static HANDLE WINAPI CreateSemaphoreW(void *sa, LONG initial, LONG max, LPCWSTR name)
{
    return publish_w(name, waitable_new(WAIT_SEMAPHORE, initial, max, false));
}

static HANDLE WINAPI CreateSemaphoreExA(void *sa, LONG initial, LONG max, LPCSTR name, DWORD f, DWORD a)
{
    return CreateSemaphoreA(sa, initial, max, name);
}

static HANDLE WINAPI CreateSemaphoreExW(void *sa, LONG initial, LONG max, LPCWSTR name, DWORD f, DWORD a)
{
    return CreateSemaphoreW(sa, initial, max, name);
}

static HANDLE WINAPI OpenSemaphoreA(DWORD access, BOOL inherit, LPCSTR name) { return open_a(name); }
static HANDLE WINAPI OpenSemaphoreW(DWORD access, BOOL inherit, LPCWSTR name) { return open_w(name); }

static BOOL WINAPI ReleaseSemaphore(HANDLE h, LONG n, PLONG prev)
{
    struct waitable *w = typed(h, WAIT_SEMAPHORE);
    if (!w)
        return fail(ERROR_INVALID_HANDLE);
    bool done;
    long p = waitable_release(w, n, &done);
    if (prev)
        *prev = p;
    return done ? TRUE : fail(298);
}

static DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms) { return wait_handles(1, &h, false, ms); }
static DWORD WINAPI WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL alertable) { return wait_handles(1, &h, false, ms); }

static DWORD WINAPI WaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL all, DWORD ms)
{
    return wait_handles(n, h, all, ms);
}

static DWORD WINAPI WaitForMultipleObjectsEx(DWORD n, const HANDLE *h, BOOL all, DWORD ms, BOOL alertable)
{
    return wait_handles(n, h, all, ms);
}

static DWORD WINAPI SignalObjectAndWait(HANDLE signal, HANDLE wait, DWORD ms, BOOL alertable)
{
    struct waitable *w = waitable_get(signal);
    if (!w)
        return WAIT_FAILED;
    if (w->type == WAIT_EVENT)
        waitable_set(w, 1);
    else if (w->type == WAIT_MUTEX)
        waitable_unlock(w);
    else if (w->type == WAIT_SEMAPHORE) {
        bool done;
        waitable_release(w, 1, &done);
    }
    return wait_handles(1, &wait, false, ms);
}

static void WINAPI InitOnceInitialize(INIT_ONCE *once) { once->state = 0; }

static BOOL WINAPI InitOnceBeginInitialize(INIT_ONCE *once, DWORD flags, BOOL *pending, void **ctx)
{
    for (;;) {
        uint32_t s = __atomic_load_n(&once->state, __ATOMIC_ACQUIRE);
        if (s == 2) {
            *pending = FALSE;
            if (ctx)
                *ctx = NULL;
            return TRUE;
        }
        if (flags & INIT_ONCE_CHECK_ONLY) {
            *pending = TRUE;
            return TRUE;
        }
        if (s == 0 && __atomic_compare_exchange_n(&once->state, &s, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            *pending = TRUE;
            return TRUE;
        }
        if (s == 1)
            futex_wait(&once->state, 1, INFINITE);
    }
}

static BOOL WINAPI InitOnceComplete(INIT_ONCE *once, DWORD flags, void *ctx)
{
    __atomic_store_n(&once->state, flags & INIT_ONCE_INIT_FAILED ? 0 : 2, __ATOMIC_RELEASE);
    futex_wake(&once->state, INT_MAX);
    return TRUE;
}

static BOOL WINAPI InitOnceExecuteOnce(INIT_ONCE *once, init_once_fn fn, void *param, void **ctx)
{
    BOOL pending;
    InitOnceBeginInitialize(once, 0, &pending, ctx);
    if (!pending)
        return TRUE;
    BOOL ok = fn(once, param, ctx);
    InitOnceComplete(once, ok ? 0 : INIT_ONCE_INIT_FAILED, NULL);
    return ok;
}

static uint32_t *word_of(volatile void *addr) { return (uint32_t *)((uintptr_t)addr & ~(uintptr_t)3); }

static BOOL WINAPI WaitOnAddress(volatile void *addr, void *cmp, SIZE_T size, DWORD ms)
{
    if (memcmp((const void *)addr, cmp, size))
        return TRUE;
    uint32_t *w = word_of(addr);
    if (futex_wait(w, __atomic_load_n(w, __ATOMIC_ACQUIRE), ms))
        return fail(ERROR_TIMEOUT);
    return TRUE;
}

static void WINAPI WakeByAddressSingle(void *addr) { futex_wake(word_of(addr), INT_MAX); }
static void WINAPI WakeByAddressAll(void *addr) { futex_wake(word_of(addr), INT_MAX); }

static pthread_mutex_t slist_lock = PTHREAD_MUTEX_INITIALIZER;

static void WINAPI InitializeSListHead(SLIST_HEADER *h) { memset(h, 0, sizeof *h); }

static SLIST_ENTRY *WINAPI InterlockedPushEntrySList(SLIST_HEADER *h, SLIST_ENTRY *e)
{
    pthread_mutex_lock(&slist_lock);
    SLIST_ENTRY *prev = h->first;
    e->next = prev;
    h->first = e;
    h->depth++;
    pthread_mutex_unlock(&slist_lock);
    return prev;
}

static SLIST_ENTRY *WINAPI InterlockedPopEntrySList(SLIST_HEADER *h)
{
    pthread_mutex_lock(&slist_lock);
    SLIST_ENTRY *e = h->first;
    if (e) {
        h->first = e->next;
        h->depth--;
    }
    pthread_mutex_unlock(&slist_lock);
    return e;
}

static SLIST_ENTRY *WINAPI InterlockedFlushSList(SLIST_HEADER *h)
{
    pthread_mutex_lock(&slist_lock);
    SLIST_ENTRY *e = h->first;
    h->first = NULL;
    h->depth = 0;
    pthread_mutex_unlock(&slist_lock);
    return e;
}

static WORD WINAPI QueryDepthSList(SLIST_HEADER *h) { return (WORD)h->depth; }

BIT_TABLE(k32_sync,
          BIT_FN(InitializeCriticalSection), BIT_FN(InitializeCriticalSectionAndSpinCount),
          BIT_FN(InitializeCriticalSectionEx), BIT_FN(DeleteCriticalSection),
          BIT_FN(EnterCriticalSection), BIT_FN(TryEnterCriticalSection), BIT_FN(LeaveCriticalSection),
          BIT_FN(SetCriticalSectionSpinCount),
          BIT_FN(InitializeSRWLock), BIT_FN(AcquireSRWLockExclusive), BIT_FN(AcquireSRWLockShared),
          BIT_FN(TryAcquireSRWLockExclusive), BIT_FN(TryAcquireSRWLockShared),
          BIT_FN(ReleaseSRWLockExclusive), BIT_FN(ReleaseSRWLockShared),
          BIT_FN(InitializeConditionVariable), BIT_FN(WakeConditionVariable), BIT_FN(WakeAllConditionVariable),
          BIT_FN(SleepConditionVariableCS), BIT_FN(SleepConditionVariableSRW),
          BIT_FN(CreateEventA), BIT_FN(CreateEventW), BIT_FN(CreateEventExA), BIT_FN(CreateEventExW),
          BIT_FN(OpenEventA), BIT_FN(OpenEventW),
          BIT_FN(SetEvent), BIT_FN(ResetEvent), BIT_FN(PulseEvent),
          BIT_FN(CreateMutexA), BIT_FN(CreateMutexW), BIT_FN(CreateMutexExA), BIT_FN(CreateMutexExW),
          BIT_FN(OpenMutexA), BIT_FN(OpenMutexW), BIT_FN(ReleaseMutex),
          BIT_FN(CreateSemaphoreA), BIT_FN(CreateSemaphoreW),
          BIT_FN(CreateSemaphoreExA), BIT_FN(CreateSemaphoreExW),
          BIT_FN(OpenSemaphoreA), BIT_FN(OpenSemaphoreW), BIT_FN(ReleaseSemaphore),
          BIT_FN(WaitForSingleObject), BIT_FN(WaitForSingleObjectEx),
          BIT_FN(WaitForMultipleObjects), BIT_FN(WaitForMultipleObjectsEx),
          BIT_FN(SignalObjectAndWait),
          BIT_FN(InitOnceInitialize), BIT_FN(InitOnceBeginInitialize),
          BIT_FN(InitOnceComplete), BIT_FN(InitOnceExecuteOnce),
          BIT_FN(WaitOnAddress), BIT_FN(WakeByAddressSingle), BIT_FN(WakeByAddressAll),
          BIT_FN(InitializeSListHead), BIT_FN(InterlockedPushEntrySList),
          BIT_FN(InterlockedPopEntrySList), BIT_FN(InterlockedFlushSList), BIT_FN(QueryDepthSList));
