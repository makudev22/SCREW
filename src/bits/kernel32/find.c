#include "k32.h"

#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>

#define MAX_PATH 260

typedef struct {
    DWORD attributes;
    FILETIME created;
    FILETIME accessed;
    FILETIME written;
    DWORD size_high;
    DWORD size_low;
    DWORD reserved0;
    DWORD reserved1;
    char name[MAX_PATH];
    char alternate[14];
} FIND_DATA_A;

typedef struct {
    DWORD attributes;
    FILETIME created;
    FILETIME accessed;
    FILETIME written;
    DWORD size_high;
    DWORD size_low;
    DWORD reserved0;
    DWORD reserved1;
    WCHAR name[MAX_PATH];
    WCHAR alternate[14];
} FIND_DATA_W;

struct finder {
    struct object base;
    DIR *dir;
    char *path;
    char *pattern;
};

struct entry {
    struct stat st;
    char name[NAME_MAX + 1];
    char full[PATH_MAX];
};

static void destroy(struct object *o)
{
    struct finder *f = (struct finder *)o;
    if (f->dir)
        closedir(f->dir);
    free(f->path);
    free(f->pattern);
    free(f);
}

static bool next(struct finder *f, struct entry *e)
{
    struct dirent *d;
    while ((d = readdir(f->dir))) {
        if (fnmatch(f->pattern, d->d_name, FNM_CASEFOLD))
            continue;
        snprintf(e->full, sizeof e->full, "%s/%s", f->path, d->d_name);
        if (stat(e->full, &e->st) && lstat(e->full, &e->st))
            continue;
        snprintf(e->name, sizeof e->name, "%s", d->d_name);
        return true;
    }
    return false;
}

static void fill(const struct entry *e, FIND_DATA_A *a, FIND_DATA_W *w)
{
    DWORD attrs = attributes_of(e->full, &e->st);
    FILETIME c = filetime_from_ticks(ticks_from_timespec(&e->st.st_ctim));
    FILETIME ac = filetime_from_ticks(ticks_from_timespec(&e->st.st_atim));
    FILETIME wr = filetime_from_ticks(ticks_from_timespec(&e->st.st_mtim));
    DWORD hi = S_ISDIR(e->st.st_mode) ? 0 : e->st.st_size >> 32;
    DWORD lo = S_ISDIR(e->st.st_mode) ? 0 : (DWORD)e->st.st_size;
    if (a) {
        memset(a, 0, sizeof *a);
        a->attributes = attrs;
        a->created = c;
        a->accessed = ac;
        a->written = wr;
        a->size_high = hi;
        a->size_low = lo;
        snprintf(a->name, sizeof a->name, "%s", e->name);
    }
    if (w) {
        memset(w, 0, sizeof *w);
        w->attributes = attrs;
        w->created = c;
        w->accessed = ac;
        w->written = wr;
        w->size_high = hi;
        w->size_low = lo;
        utf8_to_utf16(e->name, -1, w->name, MAX_PATH - 1);
    }
}

static HANDLE first(const char *spec, FIND_DATA_A *a, FIND_DATA_W *w)
{
    char *host = path_to_host(spec);
    if (!host) {
        last_error_set(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    char *slash = strrchr(host, '/');
    struct finder *f = calloc(1, sizeof *f);
    object_init(&f->base, OBJ_FIND, destroy);
    if (slash) {
        *slash = 0;
        f->path = strdup(*host ? host : "/");
        f->pattern = strdup(slash + 1);
    } else {
        f->path = strdup(".");
        f->pattern = strdup(host);
    }
    free(host);
    if (!strcmp(f->pattern, "*.*") || !*f->pattern) {
        free(f->pattern);
        f->pattern = strdup("*");
    }

    f->dir = opendir(f->path);
    struct entry e;
    if (!f->dir || !next(f, &e)) {
        last_error_set(f->dir ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND);
        destroy(&f->base);
        return INVALID_HANDLE_VALUE;
    }
    fill(&e, a, w);
    return handle_new(&f->base);
}

static BOOL advance(HANDLE h, FIND_DATA_A *a, FIND_DATA_W *w)
{
    struct finder *f = (struct finder *)handle_get(h, OBJ_FIND);
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    struct entry e;
    if (!next(f, &e))
        return fail(ERROR_NO_MORE_FILES);
    fill(&e, a, w);
    return TRUE;
}

static HANDLE WINAPI FindFirstFileA(LPCSTR spec, FIND_DATA_A *out) { return first(spec, out, NULL); }

static HANDLE WINAPI FindFirstFileW(LPCWSTR spec, FIND_DATA_W *out)
{
    char *s = wide_to_host(spec);
    HANDLE h = first(s, NULL, out);
    free(s);
    return h;
}

static HANDLE WINAPI FindFirstFileExA(LPCSTR spec, int level, FIND_DATA_A *out, int op, void *filter, DWORD flags)
{
    return FindFirstFileA(spec, out);
}

static HANDLE WINAPI FindFirstFileExW(LPCWSTR spec, int level, FIND_DATA_W *out, int op, void *filter, DWORD flags)
{
    return FindFirstFileW(spec, out);
}

static BOOL WINAPI FindNextFileA(HANDLE h, FIND_DATA_A *out) { return advance(h, out, NULL); }
static BOOL WINAPI FindNextFileW(HANDLE h, FIND_DATA_W *out) { return advance(h, NULL, out); }

static BOOL WINAPI FindClose(HANDLE h)
{
    if (!handle_get(h, OBJ_FIND))
        return fail(ERROR_INVALID_HANDLE);
    handle_close(h);
    return TRUE;
}

BIT_TABLE(k32_find,
          BIT_FN(FindFirstFileA), BIT_FN(FindFirstFileW),
          BIT_FN(FindFirstFileExA), BIT_FN(FindFirstFileExW),
          BIT_FN(FindNextFileA), BIT_FN(FindNextFileW), BIT_FN(FindClose));
