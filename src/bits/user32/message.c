#include "u32.h"

#include <screw/sync.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define PM_NOREMOVE 0x0000
#define QS_ALLINPUT 0x04FF
#define MWMO_INPUTAVAILABLE 0x0004
#define WM_APP_FIRST 0xC000
#define MAX_TIMERS 256

typedef void WINAPI (*TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);

struct timer {
    HWND hwnd;
    UINT_PTR id;
    uint64_t due;
    UINT interval;
    TIMERPROC proc;
    uint64_t thread;
    bool live;
};

struct queue {
    uint64_t thread;
    MSG *ring;
    size_t cap;
    size_t head;
    size_t count;
    bool quit;
    int quit_code;
    int wake_fd;
    DWORD last_time;
    POINT last_pt;
    struct queue *next;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct queue *queues;
static struct timer timers[MAX_TIMERS];
static UINT next_message = WM_APP_FIRST;
static __thread struct queue *mine;

static struct queue *queue_for(uint64_t thread, bool create)
{
    for (struct queue *q = queues; q; q = q->next)
        if (q->thread == thread)
            return q;
    if (!create)
        return NULL;
    struct queue *q = calloc(1, sizeof *q);
    q->thread = thread;
    q->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    q->next = queues;
    queues = q;
    return q;
}

static struct queue *self(void)
{
    if (!mine) {
        pthread_mutex_lock(&lock);
        mine = queue_for(teb_current()->thread_id, true);
        pthread_mutex_unlock(&lock);
    }
    return mine;
}

static void signal_queue(struct queue *q)
{
    uint64_t one = 1;
    if (write(q->wake_fd, &one, sizeof one) < 0 && errno != EAGAIN)
        return;
}

static void drain(struct queue *q)
{
    uint64_t v;
    while (read(q->wake_fd, &v, sizeof v) > 0)
        ;
}

static void push(struct queue *q, const MSG *m)
{
    if (q->count == q->cap) {
        size_t cap = q->cap ? q->cap * 2 : 64;
        MSG *ring = malloc(cap * sizeof *ring);
        for (size_t i = 0; i < q->count; i++)
            ring[i] = q->ring[(q->head + i) % q->cap];
        free(q->ring);
        q->ring = ring;
        q->cap = cap;
        q->head = 0;
    }
    q->ring[(q->head + q->count++) % q->cap] = *m;
    signal_queue(q);
}

static void post_to(uint64_t thread, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, DWORD extra)
{
    MSG m = { hwnd, msg, wp, lp, (DWORD)monotonic_ms(), { 0, 0 }, extra };
    pthread_mutex_lock(&lock);
    push(queue_for(thread, true), &m);
    pthread_mutex_unlock(&lock);
}

static uint64_t owner(HWND hwnd)
{
    struct window *w = window_get(hwnd);
    return w ? w->thread : teb_current()->thread_id;
}

void queue_post(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { post_to(owner(hwnd), hwnd, msg, wp, lp, 0); }

void queue_post_key(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, DWORD ch) { post_to(owner(hwnd), hwnd, msg, wp, lp, ch); }

void queue_post_thread(uint64_t thread, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    post_to(thread, hwnd, msg, wp, lp, 0);
}

void queue_wake(void)
{
    pthread_mutex_lock(&lock);
    for (struct queue *q = queues; q; q = q->next)
        signal_queue(q);
    pthread_mutex_unlock(&lock);
}

static void pump_x11(void)
{
    Display *d = x11();
    if (!d)
        return;
    while (XPending(d)) {
        XEvent ev;
        XNextEvent(d, &ev);
        struct window *w = window_by_xid(ev.xany.window);
        if (!w)
            continue;
        switch (ev.type) {
        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == x11_atom("WM_DELETE_WINDOW"))
                queue_post(w->hwnd, WM_CLOSE, 0, 0);
            break;
        case ConfigureNotify:
            window_moved(w, ev.xconfigure.x, ev.xconfigure.y, ev.xconfigure.width, ev.xconfigure.height);
            break;
        case Expose:
            if (!ev.xexpose.count)
                w->needs_paint = true;
            break;
        case MapNotify:
            w->visible = true;
            break;
        case UnmapNotify:
            w->visible = false;
            break;
        case FocusIn:
            window_set_focus(w->hwnd);
            window_send(w, WM_ACTIVATEAPP, TRUE, 0);
            window_send(w, WM_ACTIVATE, WA_ACTIVE, 0);
            window_send(w, WM_SETFOCUS, 0, 0);
            break;
        case FocusOut:
            window_send(w, WM_KILLFOCUS, 0, 0);
            window_send(w, WM_ACTIVATE, WA_INACTIVE, 0);
            window_send(w, WM_ACTIVATEAPP, FALSE, 0);
            break;
        default:
            input_event(&ev, w);
            break;
        }
    }
}

static bool matches(const MSG *m, HWND hwnd, UINT lo, UINT hi)
{
    if (hwnd && hwnd != (HWND)(intptr_t)-1 && m->hwnd != hwnd)
        return false;
    if (hwnd == (HWND)(intptr_t)-1 && m->hwnd)
        return false;
    if (!lo && !hi)
        return true;
    return m->message >= lo && m->message <= hi;
}

static bool take_posted(struct queue *q, MSG *out, HWND hwnd, UINT lo, UINT hi, bool remove)
{
    for (size_t i = 0; i < q->count; i++) {
        size_t at = (q->head + i) % q->cap;
        if (!matches(&q->ring[at], hwnd, lo, hi))
            continue;
        *out = q->ring[at];
        if (remove) {
            for (size_t j = i; j > 0; j--)
                q->ring[(q->head + j) % q->cap] = q->ring[(q->head + j - 1) % q->cap];
            q->head = (q->head + 1) % q->cap;
            q->count--;
        }
        return true;
    }
    return false;
}

static bool take_timer(struct queue *q, MSG *out, HWND hwnd, bool remove)
{
    uint64_t now = monotonic_ms();
    for (int i = 0; i < MAX_TIMERS; i++) {
        struct timer *t = &timers[i];
        if (!t->live || t->thread != q->thread || t->due > now)
            continue;
        if (hwnd && t->hwnd != hwnd)
            continue;
        *out = (MSG){ t->hwnd, WM_TIMER, t->id, (LPARAM)t->proc, (DWORD)now, { 0, 0 }, 0 };
        if (remove)
            t->due = now + t->interval;
        return true;
    }
    return false;
}

static int next_timer_ms(struct queue *q)
{
    uint64_t now = monotonic_ms(), best = UINT64_MAX;
    for (int i = 0; i < MAX_TIMERS; i++)
        if (timers[i].live && timers[i].thread == q->thread && timers[i].due < best)
            best = timers[i].due;
    if (best == UINT64_MAX)
        return -1;
    return best <= now ? 0 : (int)(best - now);
}

static bool take_paint(struct queue *q, MSG *out, HWND hwnd)
{
    for (struct window *w = window_next(NULL); w; w = window_next(w)) {
        if (!w->needs_paint || !w->visible || w->thread != q->thread)
            continue;
        if (hwnd && w->hwnd != hwnd)
            continue;
        *out = (MSG){ w->hwnd, WM_PAINT, 0, 0, (DWORD)monotonic_ms(), { 0, 0 }, 0 };
        return true;
    }
    return false;
}

static bool peek(MSG *out, HWND hwnd, UINT lo, UINT hi, bool remove)
{
    struct queue *q = self();
    pump_x11();
    pthread_mutex_lock(&lock);
    drain(q);
    bool got = take_posted(q, out, hwnd, lo, hi, remove);
    if (!got && (!lo || (lo <= WM_PAINT && hi >= WM_PAINT)))
        got = take_paint(q, out, hwnd);
    if (!got && (!lo || (lo <= WM_TIMER && hi >= WM_TIMER)))
        got = take_timer(q, out, hwnd, remove);
    if (!got && q->quit) {
        *out = (MSG){ NULL, WM_QUIT, (WPARAM)q->quit_code, 0, (DWORD)monotonic_ms(), { 0, 0 }, 0 };
        if (remove)
            q->quit = false;
        got = true;
    }
    if (got) {
        q->last_time = out->time;
        q->last_pt = out->pt;
    }
    pthread_mutex_unlock(&lock);
    return got;
}

static void wait_input(int ms)
{
    struct queue *q = self();
    Display *d = x11();
    struct pollfd fds[2] = { { q->wake_fd, POLLIN, 0 }, { d ? ConnectionNumber(d) : -1, POLLIN, 0 } };
    if (d && XPending(d))
        return;
    pthread_mutex_lock(&lock);
    int t = next_timer_ms(q);
    pthread_mutex_unlock(&lock);
    if (ms < 0 || (t >= 0 && t < ms))
        ms = t;
    poll(fds, d ? 2 : 1, ms);
}

static BOOL get(MSG *msg, HWND hwnd, UINT lo, UINT hi)
{
    while (!peek(msg, hwnd, lo, hi, true))
        wait_input(-1);
    return msg->message != WM_QUIT;
}

static BOOL WINAPI GetMessageA(MSG *m, HWND h, UINT lo, UINT hi) { return get(m, h, lo, hi); }
static BOOL WINAPI GetMessageW(MSG *m, HWND h, UINT lo, UINT hi) { return get(m, h, lo, hi); }

static BOOL WINAPI PeekMessageA(MSG *m, HWND h, UINT lo, UINT hi, UINT flags)
{
    return peek(m, h, lo, hi, flags & PM_REMOVE);
}

static BOOL WINAPI PeekMessageW(MSG *m, HWND h, UINT lo, UINT hi, UINT flags)
{
    return peek(m, h, lo, hi, flags & PM_REMOVE);
}

static BOOL WINAPI WaitMessage(void)
{
    MSG m;
    if (!peek(&m, NULL, 0, 0, false))
        wait_input(-1);
    return TRUE;
}

static BOOL WINAPI TranslateMessage(const MSG *m)
{
    if (!m->private_ || (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN))
        return FALSE;
    UINT ch = m->message == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR;
    post_to(teb_current()->thread_id, m->hwnd, ch, m->private_, m->lparam, 0);
    return TRUE;
}

static LRESULT dispatch(const MSG *m)
{
    if (m->message == WM_TIMER && m->lparam) {
        ((TIMERPROC)m->lparam)(m->hwnd, WM_TIMER, m->wparam, m->time);
        return 0;
    }
    struct window *w = window_get(m->hwnd);
    if (!w)
        return 0;
    LRESULT r = window_send(w, m->message, m->wparam, m->lparam);
    if (m->message == WM_PAINT && w->needs_paint && w->proc)
        w->needs_paint = false;
    return r;
}

static LRESULT WINAPI DispatchMessageA(const MSG *m) { return dispatch(m); }
static LRESULT WINAPI DispatchMessageW(const MSG *m) { return dispatch(m); }

static BOOL post_message(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (h && h != (HWND)(intptr_t)0xFFFF && !window_get(h))
        return fail_win(1400);
    if (h == (HWND)(intptr_t)0xFFFF) {
        for (struct window *w = window_next(NULL); w; w = window_next(w))
            if (!w->parent)
                queue_post(w->hwnd, msg, wp, lp);
        return TRUE;
    }
    queue_post(h, msg, wp, lp);
    return TRUE;
}

static BOOL WINAPI PostMessageA(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return post_message(h, msg, wp, lp); }
static BOOL WINAPI PostMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return post_message(h, msg, wp, lp); }

static BOOL WINAPI PostThreadMessageA(DWORD tid, UINT msg, WPARAM wp, LPARAM lp)
{
    queue_post_thread(tid, NULL, msg, wp, lp);
    return TRUE;
}

static BOOL WINAPI PostThreadMessageW(DWORD tid, UINT msg, WPARAM wp, LPARAM lp)
{
    return PostThreadMessageA(tid, msg, wp, lp);
}

static LRESULT send_message(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (h == (HWND)(intptr_t)0xFFFF) {
        for (struct window *w = window_next(NULL); w; w = window_next(w))
            if (!w->parent)
                window_send(w, msg, wp, lp);
        return 0;
    }
    return window_send(window_get(h), msg, wp, lp);
}

static LRESULT WINAPI SendMessageA(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return send_message(h, msg, wp, lp); }
static LRESULT WINAPI SendMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return send_message(h, msg, wp, lp); }

static LRESULT WINAPI SendMessageTimeoutA(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT flags, UINT ms,
                                          ULONG_PTR *result)
{
    LRESULT r = send_message(h, msg, wp, lp);
    if (result)
        *result = r;
    return TRUE;
}

static LRESULT WINAPI SendMessageTimeoutW(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT flags, UINT ms,
                                          ULONG_PTR *result)
{
    return SendMessageTimeoutA(h, msg, wp, lp, flags, ms, result);
}

static BOOL WINAPI SendNotifyMessageA(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return post_message(h, msg, wp, lp); }
static BOOL WINAPI SendNotifyMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return post_message(h, msg, wp, lp); }

static BOOL WINAPI ReplyMessage(LRESULT r) { return FALSE; }
static BOOL WINAPI InSendMessage(void) { return FALSE; }

static void WINAPI PostQuitMessage(int code)
{
    struct queue *q = self();
    pthread_mutex_lock(&lock);
    q->quit = true;
    q->quit_code = code;
    signal_queue(q);
    pthread_mutex_unlock(&lock);
}

static LONG WINAPI GetMessageTime(void) { return self()->last_time; }

static DWORD WINAPI GetMessagePos(void)
{
    POINT p = self()->last_pt;
    return (DWORD)make_lparam(p.x, p.y);
}

static LPARAM WINAPI GetMessageExtraInfo(void) { return 0; }

static DWORD WINAPI GetQueueStatus(UINT flags)
{
    MSG m;
    return peek(&m, NULL, 0, 0, false) ? (flags << 16) | flags : 0;
}

static BOOL WINAPI GetInputState(void) { return GetQueueStatus(QS_ALLINPUT) != 0; }

static DWORD WINAPI MsgWaitForMultipleObjectsEx(DWORD n, const HANDLE *handles, DWORD ms, DWORD mask, DWORD flags)
{
    uint64_t deadline = ms == INFINITE ? UINT64_MAX : monotonic_ms() + ms;
    for (;;) {
        if (n) {
            DWORD r = wait_handles(n, handles, false, 0);
            if (r != WAIT_TIMEOUT)
                return r;
        }
        MSG m;
        if (mask && peek(&m, NULL, 0, 0, false))
            return WAIT_OBJECT_0 + n;
        uint64_t now = monotonic_ms();
        if (now >= deadline)
            return WAIT_TIMEOUT;
        uint64_t left = deadline - now;
        int slice = n ? 1 : (left > 1000 ? 1000 : (int)left);
        wait_input(slice);
    }
}

static DWORD WINAPI MsgWaitForMultipleObjects(DWORD n, const HANDLE *handles, BOOL all, DWORD ms, DWORD mask)
{
    return MsgWaitForMultipleObjectsEx(n, handles, ms, mask, 0);
}

static UINT_PTR WINAPI SetTimer(HWND h, UINT_PTR id, UINT ms, TIMERPROC proc)
{
    uint64_t thread = teb_current()->thread_id;
    if (ms < 10)
        ms = 10;
    pthread_mutex_lock(&lock);
    int slot = -1;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].live && timers[i].hwnd == h && h && timers[i].id == id) {
            slot = i;
            break;
        }
        if (!timers[i].live && slot < 0)
            slot = i;
    }
    UINT_PTR r = 0;
    if (slot >= 0) {
        if (!h && !id)
            id = slot + 1;
        timers[slot] = (struct timer){ h, id, monotonic_ms() + ms, ms, proc, thread, true };
        r = h ? (id ? id : 1) : id;
        signal_queue(queue_for(thread, true));
    }
    pthread_mutex_unlock(&lock);
    return r;
}

static BOOL WINAPI KillTimer(HWND h, UINT_PTR id)
{
    bool found = false;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_TIMERS; i++)
        if (timers[i].live && timers[i].hwnd == h && timers[i].id == id) {
            timers[i].live = false;
            found = true;
        }
    pthread_mutex_unlock(&lock);
    return found;
}

static UINT register_message(const char *name)
{
    static struct {
        char name[128];
        UINT id;
    } names[512];
    static int count;
    pthread_mutex_lock(&lock);
    UINT id = 0;
    for (int i = 0; i < count && !id; i++)
        if (!strcasecmp(names[i].name, name))
            id = names[i].id;
    if (!id && count < 512) {
        snprintf(names[count].name, sizeof names[count].name, "%s", name);
        id = names[count++].id = next_message++;
    }
    pthread_mutex_unlock(&lock);
    return id;
}

static UINT WINAPI RegisterWindowMessageA(LPCSTR name) { return register_message(name); }

static UINT WINAPI RegisterWindowMessageW(LPCWSTR name)
{
    char *n = wide_to_host(name);
    UINT r = register_message(n);
    free(n);
    return r;
}

BIT_TABLE(u32_message,
          BIT_FN(GetMessageA), BIT_FN(GetMessageW), BIT_FN(PeekMessageA), BIT_FN(PeekMessageW),
          BIT_FN(WaitMessage), BIT_FN(TranslateMessage), BIT_FN(DispatchMessageA), BIT_FN(DispatchMessageW),
          BIT_FN(PostMessageA), BIT_FN(PostMessageW), BIT_FN(PostThreadMessageA), BIT_FN(PostThreadMessageW),
          BIT_FN(SendMessageA), BIT_FN(SendMessageW), BIT_FN(SendMessageTimeoutA), BIT_FN(SendMessageTimeoutW),
          BIT_FN(SendNotifyMessageA), BIT_FN(SendNotifyMessageW), BIT_FN(ReplyMessage), BIT_FN(InSendMessage),
          BIT_FN(PostQuitMessage), BIT_FN(GetMessageTime), BIT_FN(GetMessagePos), BIT_FN(GetMessageExtraInfo),
          BIT_FN(GetQueueStatus), BIT_FN(GetInputState),
          BIT_FN(MsgWaitForMultipleObjects), BIT_FN(MsgWaitForMultipleObjectsEx),
          BIT_FN(SetTimer), BIT_FN(KillTimer), BIT_FN(RegisterWindowMessageA), BIT_FN(RegisterWindowMessageW));
