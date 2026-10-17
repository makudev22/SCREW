#ifndef SCREW_THREAD_H
#define SCREW_THREAD_H

#include <screw/sync.h>

#include <pthread.h>

typedef DWORD WINAPI (*thread_start)(void *arg);

struct thread {
    struct waitable w;
    pthread_t pt;
    uint64_t tid;
    thread_start start;
    void *arg;
    pthread_mutex_t gate_lock;
    pthread_cond_t gate;
    int suspend;
    int priority;
};

void thread_attach_main(void);
HANDLE thread_spawn(thread_start start, void *arg, size_t stack, bool suspended, DWORD *tid);
_Noreturn void thread_exit(DWORD code);
struct thread *thread_get(HANDLE h);
struct thread *thread_self(void);
DWORD thread_resume(struct thread *t);

typedef void WINAPI (*fls_callback)(void *data);
DWORD tls_slot_alloc(fls_callback cb);
bool tls_slot_free(DWORD index);
void *tls_slot_get(DWORD index);
bool tls_slot_set(DWORD index, void *value);

#endif
