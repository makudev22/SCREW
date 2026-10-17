#include <screw/sync.h>
#include <screw/teb.h>

#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define MAX_WAIT 64

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void setup(void)
{
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(&changed, &a);
    pthread_condattr_destroy(&a);
}

uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ull + ts.tv_nsec / 1000000;
}

static struct timespec *timeout_of(DWORD ms, struct timespec *ts)
{
    if (ms == INFINITE)
        return NULL;
    ts->tv_sec = ms / 1000;
    ts->tv_nsec = (ms % 1000) * 1000000l;
    return ts;
}

int futex_wait(void *addr, uint32_t expected, DWORD ms)
{
    struct timespec ts;
    long r = syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, expected, timeout_of(ms, &ts), NULL, 0);
    return r < 0 && errno == ETIMEDOUT ? -1 : 0;
}

void futex_wake(void *addr, int count)
{
    syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, count, NULL, NULL, 0);
}

static void destroy(struct object *o) { free(o); }

struct waitable *waitable_new(enum waitable_type type, long count, long max, bool manual)
{
    pthread_once(&once, setup);
    struct waitable *w = calloc(1, sizeof *w);
    object_init(&w->base, OBJ_WAITABLE, destroy);
    w->type = type;
    w->count = count;
    w->max = max;
    w->manual = manual;
    return w;
}

struct waitable *waitable_get(HANDLE h)
{
    return (struct waitable *)handle_get(h, OBJ_WAITABLE);
}

static uint64_t self_id(void) { return teb_current()->thread_id; }

static bool ready(struct waitable *w, uint64_t me)
{
    if (w->type == WAIT_MUTEX)
        return !w->recursion || w->owner == me;
    return w->count > 0;
}

static void take(struct waitable *w, uint64_t me)
{
    switch (w->type) {
    case WAIT_MUTEX:
        w->owner = me;
        w->recursion++;
        break;
    case WAIT_SEMAPHORE:
        w->count--;
        break;
    case WAIT_EVENT:
        if (!w->manual)
            w->count = 0;
        break;
    case WAIT_THREAD:
        break;
    }
}

static void notify(void) { pthread_cond_broadcast(&changed); }

void waitable_set(struct waitable *w, long count)
{
    pthread_mutex_lock(&lock);
    w->count = count;
    notify();
    pthread_mutex_unlock(&lock);
}

long waitable_release(struct waitable *w, long n, bool *ok)
{
    pthread_mutex_lock(&lock);
    long prev = w->count;
    *ok = n > 0 && prev + n <= w->max;
    if (*ok) {
        w->count += n;
        notify();
    }
    pthread_mutex_unlock(&lock);
    return prev;
}

bool waitable_unlock(struct waitable *w)
{
    pthread_mutex_lock(&lock);
    bool ok = w->recursion && w->owner == self_id();
    if (ok && !--w->recursion) {
        w->owner = 0;
        notify();
    }
    pthread_mutex_unlock(&lock);
    return ok;
}

void waitable_finish(struct waitable *w, DWORD exit_code)
{
    pthread_mutex_lock(&lock);
    w->exit_code = exit_code;
    w->count = 1;
    notify();
    pthread_mutex_unlock(&lock);
}

static int check(struct waitable **w, DWORD n, bool all, uint64_t me)
{
    if (all) {
        for (DWORD i = 0; i < n; i++)
            if (!ready(w[i], me))
                return -1;
        for (DWORD i = 0; i < n; i++)
            take(w[i], me);
        return 0;
    }
    for (DWORD i = 0; i < n; i++)
        if (ready(w[i], me)) {
            take(w[i], me);
            return i;
        }
    return -1;
}

DWORD wait_handles(DWORD n, const HANDLE *handles, bool all, DWORD ms)
{
    pthread_once(&once, setup);
    if (!n || n > MAX_WAIT) {
        last_error_set(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    struct waitable *w[MAX_WAIT];
    for (DWORD i = 0; i < n; i++) {
        w[i] = waitable_get(handles[i]);
        if (!w[i]) {
            last_error_set(ERROR_INVALID_HANDLE);
            return WAIT_FAILED;
        }
    }

    struct timespec deadline;
    if (ms != INFINITE) {
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec += ms / 1000;
        deadline.tv_nsec += (ms % 1000) * 1000000l;
        if (deadline.tv_nsec >= 1000000000l) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000l;
        }
    }

    uint64_t me = self_id();
    DWORD result;
    pthread_mutex_lock(&lock);
    for (;;) {
        int hit = check(w, n, all, me);
        if (hit >= 0) {
            result = WAIT_OBJECT_0 + hit;
            break;
        }
        if (ms == 0) {
            result = WAIT_TIMEOUT;
            break;
        }
        int r = ms == INFINITE ? pthread_cond_wait(&changed, &lock)
                               : pthread_cond_timedwait(&changed, &lock, &deadline);
        if (r == ETIMEDOUT) {
            hit = check(w, n, all, me);
            result = hit >= 0 ? WAIT_OBJECT_0 + hit : WAIT_TIMEOUT;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}
