#include "u32.h"

#include <pthread.h>
#include <stdio.h>

#define MAX_PROPS 1024

struct prop {
    HWND hwnd;
    char name[96];
    HANDLE value;
    bool used;
};

static struct prop props[MAX_PROPS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void key_of(const void *name, bool wide, char out[96])
{
    if ((uintptr_t)name < 0x10000) {
        snprintf(out, 96, "#%u", (unsigned)(uintptr_t)name);
    } else if (wide) {
        utf16_to_utf8(name, -1, out, 96);
    } else {
        snprintf(out, 96, "%s", (const char *)name);
    }
}

static BOOL set_prop(HWND hwnd, const void *name, bool wide, HANDLE value)
{
    char key[96];
    key_of(name, wide, key);
    pthread_mutex_lock(&lock);
    int free_slot = -1;
    for (int i = 0; i < MAX_PROPS; i++) {
        if (props[i].used && props[i].hwnd == hwnd && !strcmp(props[i].name, key)) {
            props[i].value = value;
            pthread_mutex_unlock(&lock);
            return TRUE;
        }
        if (!props[i].used && free_slot < 0)
            free_slot = i;
    }
    if (free_slot >= 0) {
        props[free_slot].used = true;
        props[free_slot].hwnd = hwnd;
        snprintf(props[free_slot].name, sizeof props[free_slot].name, "%s", key);
        props[free_slot].value = value;
    }
    pthread_mutex_unlock(&lock);
    return free_slot >= 0;
}

static HANDLE get_prop(HWND hwnd, const void *name, bool wide)
{
    char key[96];
    key_of(name, wide, key);
    pthread_mutex_lock(&lock);
    HANDLE v = NULL;
    for (int i = 0; i < MAX_PROPS; i++)
        if (props[i].used && props[i].hwnd == hwnd && !strcmp(props[i].name, key)) {
            v = props[i].value;
            break;
        }
    pthread_mutex_unlock(&lock);
    return v;
}

static HANDLE remove_prop(HWND hwnd, const void *name, bool wide)
{
    char key[96];
    key_of(name, wide, key);
    pthread_mutex_lock(&lock);
    HANDLE v = NULL;
    for (int i = 0; i < MAX_PROPS; i++)
        if (props[i].used && props[i].hwnd == hwnd && !strcmp(props[i].name, key)) {
            v = props[i].value;
            props[i].used = false;
            break;
        }
    pthread_mutex_unlock(&lock);
    return v;
}

static BOOL WINAPI SetPropA(HWND h, LPCSTR name, HANDLE v) { return set_prop(h, name, false, v); }
static BOOL WINAPI SetPropW(HWND h, LPCWSTR name, HANDLE v) { return set_prop(h, name, true, v); }
static HANDLE WINAPI GetPropA(HWND h, LPCSTR name) { return get_prop(h, name, false); }
static HANDLE WINAPI GetPropW(HWND h, LPCWSTR name) { return get_prop(h, name, true); }
static HANDLE WINAPI RemovePropA(HWND h, LPCSTR name) { return remove_prop(h, name, false); }
static HANDLE WINAPI RemovePropW(HWND h, LPCWSTR name) { return remove_prop(h, name, true); }

BIT_TABLE(u32_prop,
          BIT_FN(SetPropA), BIT_FN(SetPropW), BIT_FN(GetPropA), BIT_FN(GetPropW),
          BIT_FN(RemovePropA), BIT_FN(RemovePropW));
