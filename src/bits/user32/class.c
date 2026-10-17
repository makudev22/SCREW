#include "u32.h"

#include <pthread.h>
#include <stdio.h>

typedef struct {
    UINT size;
    UINT style;
    WNDPROC proc;
    int cls_extra;
    int wnd_extra;
    HINSTANCE instance;
    HICON icon;
    HCURSOR cursor;
    HBRUSH background;
    const void *menu_name;
    const void *class_name;
    HICON icon_small;
} WNDCLASSEX;

typedef struct {
    UINT style;
    WNDPROC proc;
    int cls_extra;
    int wnd_extra;
    HINSTANCE instance;
    HICON icon;
    HCURSOR cursor;
    HBRUSH background;
    const void *menu_name;
    const void *class_name;
} WNDCLASS;

static struct wclass *classes;
static ATOM next_atom = 0xC000;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

const char *class_key(const void *name, bool wide, char buf[128])
{
    if ((uintptr_t)name < 0x10000) {
        snprintf(buf, 128, "#%u", (unsigned)(uintptr_t)name);
        return buf;
    }
    if (wide) {
        char *u = wide_to_host(name);
        snprintf(buf, 128, "%s", u);
        free(u);
    } else {
        snprintf(buf, 128, "%s", (const char *)name);
    }
    for (char *p = buf; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p += 32;
    return buf;
}

struct wclass *class_find(const char *name, ATOM atom)
{
    pthread_mutex_lock(&lock);
    struct wclass *c = classes;
    while (c && !(atom ? c->atom == atom : !strcmp(c->name, name)))
        c = c->next;
    pthread_mutex_unlock(&lock);
    return c;
}

static ATOM add(const WNDCLASSEX *wc, bool wide)
{
    char key[128];
    class_key(wc->class_name, wide, key);
    if ((uintptr_t)wc->class_name >= 0x10000 && class_find(key, 0)) {
        last_error_set(1410);
        return 0;
    }
    struct wclass *c = calloc(1, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", key);
    c->wide = wide;
    c->proc = wc->proc;
    c->style = wc->style;
    c->cls_extra = wc->cls_extra;
    c->wnd_extra = wc->wnd_extra;
    c->instance = wc->instance;
    c->icon = wc->icon;
    c->cursor = wc->cursor;
    c->background = wc->background;
    pthread_mutex_lock(&lock);
    c->atom = next_atom++;
    c->next = classes;
    classes = c;
    pthread_mutex_unlock(&lock);
    return c->atom;
}

static WNDCLASSEX from_basic(const WNDCLASS *wc)
{
    return (WNDCLASSEX){ sizeof(WNDCLASSEX), wc->style, wc->proc, wc->cls_extra, wc->wnd_extra, wc->instance,
                         wc->icon, wc->cursor, wc->background, wc->menu_name, wc->class_name, NULL };
}

static ATOM WINAPI RegisterClassExA(const WNDCLASSEX *wc) { return add(wc, false); }
static ATOM WINAPI RegisterClassExW(const WNDCLASSEX *wc) { return add(wc, true); }

static ATOM WINAPI RegisterClassA(const WNDCLASS *wc)
{
    WNDCLASSEX ex = from_basic(wc);
    return add(&ex, false);
}

static ATOM WINAPI RegisterClassW(const WNDCLASS *wc)
{
    WNDCLASSEX ex = from_basic(wc);
    return add(&ex, true);
}

static BOOL unregister(const void *name, bool wide)
{
    char key[128];
    class_key(name, wide, key);
    ATOM atom = (uintptr_t)name < 0x10000 ? (ATOM)(uintptr_t)name : 0;
    pthread_mutex_lock(&lock);
    for (struct wclass **p = &classes; *p; p = &(*p)->next)
        if (atom ? (*p)->atom == atom : !strcmp((*p)->name, key)) {
            struct wclass *c = *p;
            *p = c->next;
            pthread_mutex_unlock(&lock);
            free(c);
            return TRUE;
        }
    pthread_mutex_unlock(&lock);
    last_error_set(1411);
    return FALSE;
}

static BOOL WINAPI UnregisterClassA(LPCSTR name, HINSTANCE inst) { return unregister(name, false); }
static BOOL WINAPI UnregisterClassW(LPCWSTR name, HINSTANCE inst) { return unregister(name, true); }

static BOOL info(const void *name, bool wide, WNDCLASSEX *out)
{
    char key[128];
    class_key(name, wide, key);
    struct wclass *c = class_find(key, (uintptr_t)name < 0x10000 ? (ATOM)(uintptr_t)name : 0);
    if (!c) {
        last_error_set(1411);
        return FALSE;
    }
    out->style = c->style;
    out->proc = c->proc;
    out->cls_extra = c->cls_extra;
    out->wnd_extra = c->wnd_extra;
    out->instance = c->instance;
    out->icon = c->icon;
    out->cursor = c->cursor;
    out->background = c->background;
    out->menu_name = NULL;
    out->class_name = name;
    return c->atom;
}

static BOOL WINAPI GetClassInfoExA(HINSTANCE inst, LPCSTR name, WNDCLASSEX *out) { return info(name, false, out); }
static BOOL WINAPI GetClassInfoExW(HINSTANCE inst, LPCWSTR name, WNDCLASSEX *out) { return info(name, true, out); }

BIT_TABLE(u32_class,
          BIT_FN(RegisterClassExA), BIT_FN(RegisterClassExW), BIT_FN(RegisterClassA), BIT_FN(RegisterClassW),
          BIT_FN(UnregisterClassA), BIT_FN(UnregisterClassW), BIT_FN(GetClassInfoExA), BIT_FN(GetClassInfoExW));
