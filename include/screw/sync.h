#ifndef SCREW_SYNC_H
#define SCREW_SYNC_H

#include <screw/handle.h>

enum waitable_type {
    WAIT_EVENT,
    WAIT_MUTEX,
    WAIT_SEMAPHORE,
    WAIT_THREAD,
};

struct waitable {
    struct object base;
    enum waitable_type type;
    long count;
    long max;
    bool manual;
    uint64_t owner;
    unsigned recursion;
    DWORD exit_code;
};

void waitable_init(struct waitable *w, enum waitable_type type, long count, long max, bool manual,
                   void (*destroy)(struct object *));
struct waitable *waitable_new(enum waitable_type type, long count, long max, bool manual);
struct waitable *waitable_get(HANDLE h);

void waitable_set(struct waitable *w, long count);
long waitable_release(struct waitable *w, long n, bool *ok);
bool waitable_unlock(struct waitable *w);
void waitable_finish(struct waitable *w, DWORD exit_code);

DWORD wait_handles(DWORD n, const HANDLE *handles, bool all, DWORD ms);

uint64_t monotonic_ms(void);
int futex_wait(void *addr, uint32_t expected, DWORD ms);
void futex_wake(void *addr, int count);

#endif
