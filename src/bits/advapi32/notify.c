#include "adv.h"

#include <screw/sync.h>

#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <unistd.h>

#define REG_NOTIFY_CHANGE_NAME 0x1
#define REG_NOTIFY_CHANGE_ATTRIBUTES 0x2
#define REG_NOTIFY_CHANGE_LAST_SET 0x4
#define REG_NOTIFY_CHANGE_SECURITY 0x8

#define WATCH_MASK (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF)

struct watch {
    int fd;
    DWORD filter;
    struct waitable *event;
};

static void add_tree(int fd, const char *dir, bool subtree)
{
    inotify_add_watch(fd, dir, WATCH_MASK);
    if (!subtree)
        return;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.' || e->d_type != DT_DIR)
            continue;
        char sub[PATH_MAX];
        snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
        add_tree(fd, sub, true);
    }
    if (d)
        closedir(d);
}

static bool relevant(const struct inotify_event *ev, DWORD filter)
{
    if (ev->mask & IN_DELETE_SELF)
        return true;
    if (!ev->len)
        return false;
    size_t n = strlen(ev->name);
    if (n > 4 && !strcmp(ev->name + n - 4, ".new"))
        return false;
    bool is_value = ev->name[0] == '.';
    if (ev->mask & IN_ATTRIB)
        return filter & (REG_NOTIFY_CHANGE_ATTRIBUTES | REG_NOTIFY_CHANGE_SECURITY);
    if (is_value)
        return filter & REG_NOTIFY_CHANGE_LAST_SET;
    return filter & REG_NOTIFY_CHANGE_NAME;
}

static void wait_change(struct watch *w)
{
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;) {
        ssize_t n = read(w->fd, buf, sizeof buf);
        if (n <= 0)
            return;
        for (char *p = buf; p < buf + n;) {
            struct inotify_event *ev = (struct inotify_event *)p;
            if (relevant(ev, w->filter))
                return;
            p += sizeof *ev + ev->len;
        }
    }
}

static void *watch_thread(void *arg)
{
    struct watch *w = arg;
    wait_change(w);
    waitable_set(w->event, 1);
    object_unref(&w->event->base);
    close(w->fd);
    free(w);
    return NULL;
}

static LONG WINAPI RegNotifyChangeKeyValue(void *key, BOOL subtree, DWORD filter, HANDLE event, BOOL async)
{
    char *dir = reg_key_dir(key);
    if (!dir)
        return ERROR_INVALID_HANDLE;
    struct waitable *ev = event ? waitable_get(event) : NULL;
    if (async && !ev) {
        free(dir);
        return ERROR_INVALID_PARAMETER;
    }
    int fd = inotify_init1(IN_CLOEXEC);
    if (fd < 0) {
        free(dir);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    add_tree(fd, dir, subtree);
    free(dir);

    struct watch *w = calloc(1, sizeof *w);
    w->fd = fd;
    w->filter = filter ? filter : REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET;
    w->event = ev;
    if (!async) {
        wait_change(w);
        if (ev)
            waitable_set(ev, 1);
        close(fd);
        free(w);
        return ERROR_SUCCESS;
    }
    object_ref(&ev->base);
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int r = pthread_create(&t, &attr, watch_thread, w);
    pthread_attr_destroy(&attr);
    if (r) {
        object_unref(&ev->base);
        close(fd);
        free(w);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_SUCCESS;
}

typedef struct {
    DWORD low;
    LONG high;
} LUID;

static BOOL WINAPI AllocateLocallyUniqueId(LUID *out)
{
    static uint64_t next = 0x10000;
    if (!out)
        return FALSE;
    uint64_t v = __atomic_fetch_add(&next, 1, __ATOMIC_RELAXED);
    out->low = (DWORD)v;
    out->high = (LONG)(v >> 32);
    return TRUE;
}

BIT_TABLE(adv_notify, BIT_FN(RegNotifyChangeKeyValue), BIT_FN(AllocateLocallyUniqueId));
