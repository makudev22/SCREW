#include <screw/loader.h>
#include <screw/teb.h>
#include <screw/thread.h>

#include <sched.h>
#include <stdlib.h>

#define SLOT_MAX (TLS_SLOTS + TLS_EXPANSION)

static __thread struct thread *self;
static pthread_mutex_t slot_lock = PTHREAD_MUTEX_INITIALIZER;
static bool slot_used[SLOT_MAX];
static fls_callback slot_cb[SLOT_MAX];

static void destroy(struct object *o)
{
    struct thread *t = (struct thread *)o;
    pthread_mutex_destroy(&t->gate_lock);
    pthread_cond_destroy(&t->gate);
    free(t);
}

static struct thread *thread_new(void)
{
    struct thread *t = calloc(1, sizeof *t);
    waitable_init(&t->w, WAIT_THREAD, 0, 1, true, destroy);
    pthread_mutex_init(&t->gate_lock, NULL);
    pthread_cond_init(&t->gate, NULL);
    return t;
}

void thread_attach_main(void)
{
    struct teb *teb = teb_create();
    teb_bind(teb);
    self = thread_new();
    self->pt = pthread_self();
    self->tid = teb->thread_id;
    teb->handle = handle_new(&self->w.base);
}

struct thread *thread_self(void) { return self; }

struct thread *thread_get(HANDLE h)
{
    if (h == CURRENT_THREAD)
        return self;
    struct waitable *w = waitable_get(h);
    return w && w->type == WAIT_THREAD ? (struct thread *)w : NULL;
}

static void run_fls_callbacks(void)
{
    for (DWORD i = 0; i < SLOT_MAX; i++) {
        if (!slot_cb[i])
            continue;
        void *v = tls_slot_get(i);
        if (v) {
            tls_slot_set(i, NULL);
            slot_cb[i](v);
        }
    }
}

static void finish(DWORD code)
{
    struct teb *teb = teb_current();
    modules_thread_event(DLL_THREAD_DETACH);
    run_fls_callbacks();
    waitable_finish(&self->w, code);
    object_unref(&self->w.base);
    teb_destroy(teb);
}

static void *trampoline(void *p)
{
    struct thread *t = p;
    self = t;
    struct teb *teb = teb_create();
    teb_bind(teb);
    __atomic_store_n(&t->tid, teb->thread_id, __ATOMIC_RELEASE);

    pthread_mutex_lock(&t->gate_lock);
    while (t->suspend)
        pthread_cond_wait(&t->gate, &t->gate_lock);
    pthread_mutex_unlock(&t->gate_lock);

    tls_thread_init();
    modules_thread_event(DLL_THREAD_ATTACH);
    DWORD code = t->start(t->arg);
    finish(code);
    return NULL;
}

HANDLE thread_spawn(thread_start start, void *arg, size_t stack, bool suspended, DWORD *tid)
{
    struct thread *t = thread_new();
    t->start = start;
    t->arg = arg;
    t->suspend = suspended;
    object_ref(&t->w.base);
    HANDLE h = handle_new(&t->w.base);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, stack > 1u << 20 ? (stack + 4095) & ~4095ul : 8u << 20);
    int r = pthread_create(&t->pt, &attr, trampoline, t);
    pthread_attr_destroy(&attr);
    if (r) {
        handle_close(h);
        object_unref(&t->w.base);
        last_error_set(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (tid) {
        while (!__atomic_load_n(&t->tid, __ATOMIC_ACQUIRE))
            sched_yield();
        *tid = t->tid;
    }
    return h;
}

void thread_exit(DWORD code)
{
    finish(code);
    pthread_exit(NULL);
}

DWORD thread_resume(struct thread *t)
{
    pthread_mutex_lock(&t->gate_lock);
    DWORD prev = t->suspend;
    if (t->suspend && !--t->suspend)
        pthread_cond_broadcast(&t->gate);
    pthread_mutex_unlock(&t->gate_lock);
    return prev;
}

DWORD tls_slot_alloc(fls_callback cb)
{
    pthread_mutex_lock(&slot_lock);
    DWORD i = 0;
    while (i < SLOT_MAX && slot_used[i])
        i++;
    if (i < SLOT_MAX) {
        slot_used[i] = true;
        slot_cb[i] = cb;
    }
    pthread_mutex_unlock(&slot_lock);
    return i < SLOT_MAX ? i : 0xFFFFFFFFu;
}

bool tls_slot_free(DWORD index)
{
    if (index >= SLOT_MAX || !slot_used[index])
        return false;
    pthread_mutex_lock(&slot_lock);
    slot_used[index] = false;
    slot_cb[index] = NULL;
    pthread_mutex_unlock(&slot_lock);
    return true;
}

void *tls_slot_get(DWORD index)
{
    struct teb *t = teb_current();
    if (index < TLS_SLOTS)
        return t->tls_slots[index];
    if (index >= SLOT_MAX || !t->tls_expansion)
        return NULL;
    return t->tls_expansion[index - TLS_SLOTS];
}

bool tls_slot_set(DWORD index, void *value)
{
    struct teb *t = teb_current();
    if (index < TLS_SLOTS) {
        t->tls_slots[index] = value;
        return true;
    }
    if (index >= SLOT_MAX)
        return false;
    if (!t->tls_expansion)
        t->tls_expansion = calloc(TLS_EXPANSION, sizeof(void *));
    t->tls_expansion[index - TLS_SLOTS] = value;
    return true;
}
