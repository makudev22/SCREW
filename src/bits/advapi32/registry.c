#include "adv.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_MULTI_SZ 7
#define REG_QWORD 11

#define REG_CREATED_NEW_KEY 1
#define REG_OPENED_EXISTING_KEY 2

#define RRF_RT_ANY 0x0000FFFF
#define RRF_NOEXPAND 0x10000000

#define DEFAULT_VALUE "@"

typedef void *HKEY;

struct key {
    struct object base;
    char *dir;
};

static const char *const roots[] = {
    "HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE", "HKEY_USERS",
    "HKEY_PERFORMANCE_DATA", "HKEY_CURRENT_CONFIG",
};

static void key_destroy(struct object *o)
{
    free(((struct key *)o)->dir);
    free(o);
}

static const char *store(void)
{
    static char dir[PATH_MAX];
    if (!dir[0]) {
        const char *env = getenv("SCREW_REGISTRY");
        if (env && *env)
            snprintf(dir, sizeof dir, "%s", env);
        else
            snprintf(dir, sizeof dir, "%s/.screw/registry", getenv("HOME") ? getenv("HOME") : "/tmp");
    }
    return dir;
}

static void make_dirs(const char *path)
{
    char buf[PATH_MAX];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(buf, 0755);
            *p = '/';
        }
    mkdir(buf, 0755);
}

char *reg_key_dir(void *h)
{
    uint32_t v = (uint32_t)(uintptr_t)h;
    if (v >= 0x80000000u && v <= 0x80000005u) {
        char *out = malloc(PATH_MAX);
        snprintf(out, PATH_MAX, "%s/%s", store(), roots[v - 0x80000000u]);
        return out;
    }
    struct key *k = (struct key *)handle_get(h, OBJ_KEY);
    return k ? strdup(k->dir) : NULL;
}

static void encode(const char *name, char *out, size_t size)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p && n + 4 < size; p++) {
        if (*p == '/' || *p == '%' || *p < 0x20 || (!n && (*p == '.' || *p == '@')))
            n += snprintf(out + n, size - n, "%%%02X", *p);
        else
            out[n++] = tolower(*p);
    }
    out[n] = 0;
}

static void decode(const char *in, char *out, size_t size)
{
    size_t n = 0;
    for (const char *p = in; *p && n + 1 < size; p++) {
        unsigned v;
        if (*p == '%' && sscanf(p + 1, "%2X", &v) == 1) {
            if (v)
                out[n++] = v;
            p += 2;
        } else {
            out[n++] = *p;
        }
    }
    out[n] = 0;
}

static char *sub_dir(const char *base, const char *sub)
{
    char *out = malloc(PATH_MAX);
    size_t n = snprintf(out, PATH_MAX, "%s", base);
    if (!sub)
        return out;
    char *copy = strdup(sub);
    for (char *p = copy; *p; p++)
        if (*p == '/')
            *p = '\\';
    for (char *save, *tok = strtok_r(copy, "\\", &save); tok; tok = strtok_r(NULL, "\\", &save)) {
        char part[NAME_MAX + 1];
        encode(tok, part, sizeof part);
        n += snprintf(out + n, PATH_MAX - n, "/%s", part);
    }
    free(copy);
    return out;
}

static char *value_path(const char *dir, const char *name)
{
    char part[NAME_MAX + 1];
    if (!name || !*name)
        snprintf(part, sizeof part, DEFAULT_VALUE);
    else
        encode(name, part, sizeof part);
    char *out = malloc(PATH_MAX);
    snprintf(out, PATH_MAX, "%s/.%s", dir, part);
    return out;
}

static HKEY wrap(char *dir)
{
    struct key *k = calloc(1, sizeof *k);
    object_init(&k->base, OBJ_KEY, key_destroy);
    k->dir = dir;
    return handle_new(&k->base);
}

static LONG open_key(HKEY parent, const char *sub, HKEY *out, bool create, DWORD *disposition)
{
    char *base = reg_key_dir(parent);
    if (!base)
        return ERROR_INVALID_HANDLE;
    char *dir = sub_dir(base, sub);
    free(base);
    struct stat st;
    bool exists = !stat(dir, &st) && S_ISDIR(st.st_mode);
    uint32_t root = (uint32_t)(uintptr_t)parent;
    bool predefined = !sub || !*sub;
    if (!exists && (create || (predefined && root >= 0x80000000u)))
        make_dirs(dir);
    else if (!exists) {
        free(dir);
        return ERROR_FILE_NOT_FOUND;
    }
    if (disposition)
        *disposition = exists ? REG_OPENED_EXISTING_KEY : REG_CREATED_NEW_KEY;
    *out = wrap(dir);
    return ERROR_SUCCESS;
}

static bool string_type(DWORD t) { return t == REG_SZ || t == REG_EXPAND_SZ || t == REG_MULTI_SZ; }

static LONG read_value(HKEY h, const char *name, DWORD *type, uint8_t **data, DWORD *size)
{
    char *dir = reg_key_dir(h);
    if (!dir)
        return ERROR_INVALID_HANDLE;
    char *path = value_path(dir, name);
    free(dir);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    free(path);
    if (fd < 0)
        return ERROR_FILE_NOT_FOUND;
    struct stat st;
    fstat(fd, &st);
    uint32_t t = 0;
    if (st.st_size < 4 || read(fd, &t, 4) != 4) {
        close(fd);
        return ERROR_FILE_NOT_FOUND;
    }
    *size = st.st_size - 4;
    *data = malloc(*size + 2);
    ssize_t n = read(fd, *data, *size);
    close(fd);
    *size = n < 0 ? 0 : n;
    (*data)[*size] = (*data)[*size + 1] = 0;
    *type = t;
    return ERROR_SUCCESS;
}

static LONG write_value(HKEY h, const char *name, DWORD type, const void *data, DWORD size)
{
    char *dir = reg_key_dir(h);
    if (!dir)
        return ERROR_INVALID_HANDLE;
    char *path = value_path(dir, name);
    free(dir);
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        free(path);
        return ERROR_ACCESS_DENIED;
    }
    uint32_t t = type;
    bool ok = write(fd, &t, 4) == 4 && (!size || write(fd, data, size) == (ssize_t)size);
    close(fd);
    ok = ok && !rename(tmp, path);
    free(path);
    return ok ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}

static LONG deliver_a(DWORD type, const uint8_t *data, DWORD size, DWORD *out_type, void *out, DWORD *out_size)
{
    DWORD need = size + (string_type(type) && (!size || data[size - 1]) ? 1 : 0);
    if (out_type)
        *out_type = type;
    if (!out_size)
        return out ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    DWORD cap = *out_size;
    *out_size = need;
    if (!out)
        return ERROR_SUCCESS;
    if (cap < need)
        return ERROR_MORE_DATA;
    memcpy(out, data, size);
    if (need > size)
        ((uint8_t *)out)[size] = 0;
    return ERROR_SUCCESS;
}

static LONG deliver_w(DWORD type, const uint8_t *data, DWORD size, DWORD *out_type, void *out, DWORD *out_size)
{
    if (!string_type(type))
        return deliver_a(type, data, size, out_type, out, out_size);
    int chars = utf8_to_utf16((const char *)data, size, NULL, 0);
    bool terminated = size && !data[size - 1];
    DWORD need = (chars + (terminated ? 0 : 1)) * 2;
    if (out_type)
        *out_type = type;
    if (!out_size)
        return out ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    DWORD cap = *out_size;
    *out_size = need;
    if (!out)
        return ERROR_SUCCESS;
    if (cap < need)
        return ERROR_MORE_DATA;
    utf8_to_utf16((const char *)data, size, out, chars);
    if (!terminated)
        ((WCHAR *)out)[chars] = 0;
    return ERROR_SUCCESS;
}

static LONG query(HKEY h, const char *name, DWORD *type, void *out, DWORD *size, bool wide)
{
    uint8_t *data;
    DWORD t, n;
    LONG r = read_value(h, name, &t, &data, &n);
    if (r)
        return r;
    r = wide ? deliver_w(t, data, n, type, out, size) : deliver_a(t, data, n, type, out, size);
    free(data);
    return r;
}

static LONG set_w(HKEY h, const char *name, DWORD type, const BYTE *data, DWORD size)
{
    if (!string_type(type))
        return write_value(h, name, type, data, size);
    int n = utf16_to_utf8((const WCHAR *)data, size / 2, NULL, 0);
    char *u = malloc(n + 1);
    utf16_to_utf8((const WCHAR *)data, size / 2, u, n);
    LONG r = write_value(h, name, type, u, n);
    free(u);
    return r;
}

static LONG enum_entries(HKEY h, DWORD index, bool values, char *name, size_t cap)
{
    char *dir = reg_key_dir(h);
    if (!dir)
        return ERROR_INVALID_HANDLE;
    DIR *d = opendir(dir);
    free(dir);
    if (!d)
        return ERROR_NO_MORE_ITEMS;
    struct dirent *e;
    DWORD i = 0;
    LONG r = ERROR_NO_MORE_ITEMS;
    while ((e = readdir(d))) {
        bool is_value = e->d_name[0] == '.' && strcmp(e->d_name, ".") && strcmp(e->d_name, "..");
        if (is_value && strlen(e->d_name) > 4 && !strcmp(e->d_name + strlen(e->d_name) - 4, ".new"))
            continue;
        bool is_key = e->d_name[0] != '.';
        if (values ? !is_value : !is_key)
            continue;
        if (i++ != index)
            continue;
        const char *raw = values ? e->d_name + 1 : e->d_name;
        if (values && !strcmp(raw, DEFAULT_VALUE))
            name[0] = 0;
        else
            decode(raw, name, cap);
        r = ERROR_SUCCESS;
        break;
    }
    closedir(d);
    return r;
}

static LONG remove_tree(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return ERROR_FILE_NOT_FOUND;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char p[PATH_MAX];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat st;
        if (!lstat(p, &st) && S_ISDIR(st.st_mode))
            remove_tree(p);
        else
            unlink(p);
    }
    closedir(d);
    return rmdir(dir) ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
}

static LONG delete_key(HKEY h, const char *sub, bool tree)
{
    char *base = reg_key_dir(h);
    if (!base)
        return ERROR_INVALID_HANDLE;
    char *dir = sub_dir(base, sub);
    free(base);
    LONG r;
    if (tree)
        r = remove_tree(dir);
    else
        r = rmdir(dir) ? (errno == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED) : ERROR_SUCCESS;
    free(dir);
    return r;
}

static LONG delete_value(HKEY h, const char *name)
{
    char *dir = reg_key_dir(h);
    if (!dir)
        return ERROR_INVALID_HANDLE;
    char *path = value_path(dir, name);
    free(dir);
    LONG r = unlink(path) ? ERROR_FILE_NOT_FOUND : ERROR_SUCCESS;
    free(path);
    return r;
}

static LONG get_value(HKEY h, const char *sub, const char *name, DWORD flags, DWORD *type, void *out,
                      DWORD *size, bool wide)
{
    HKEY k = h;
    if (sub && *sub) {
        LONG r = open_key(h, sub, &k, false, NULL);
        if (r)
            return r;
    }
    DWORD t = 0;
    LONG r = query(k, name, &t, out, size, wide);
    if (k != h)
        handle_close(k);
    if (type)
        *type = t;
    return r;
}

#define W1(x) char *x##_u = wide_to_host(x)
#define F1(x) free(x##_u)

static LONG WINAPI RegOpenKeyExA(HKEY h, LPCSTR sub, DWORD opt, DWORD sam, HKEY *out)
{
    return open_key(h, sub, out, false, NULL);
}

static LONG WINAPI RegOpenKeyExW(HKEY h, LPCWSTR sub, DWORD opt, DWORD sam, HKEY *out)
{
    W1(sub);
    LONG r = open_key(h, sub_u, out, false, NULL);
    F1(sub);
    return r;
}

static LONG WINAPI RegOpenKeyA(HKEY h, LPCSTR sub, HKEY *out) { return open_key(h, sub, out, false, NULL); }
static LONG WINAPI RegOpenKeyW(HKEY h, LPCWSTR sub, HKEY *out) { return RegOpenKeyExW(h, sub, 0, 0, out); }

static LONG WINAPI RegCreateKeyExA(HKEY h, LPCSTR sub, DWORD res, LPSTR cls, DWORD opt, DWORD sam, void *sa,
                                   HKEY *out, DWORD *disp)
{
    return open_key(h, sub, out, true, disp);
}

static LONG WINAPI RegCreateKeyExW(HKEY h, LPCWSTR sub, DWORD res, LPWSTR cls, DWORD opt, DWORD sam, void *sa,
                                   HKEY *out, DWORD *disp)
{
    W1(sub);
    LONG r = open_key(h, sub_u, out, true, disp);
    F1(sub);
    return r;
}

static LONG WINAPI RegCreateKeyA(HKEY h, LPCSTR sub, HKEY *out) { return open_key(h, sub, out, true, NULL); }
static LONG WINAPI RegCreateKeyW(HKEY h, LPCWSTR sub, HKEY *out)
{
    return RegCreateKeyExW(h, sub, 0, NULL, 0, 0, NULL, out, NULL);
}

static LONG WINAPI RegCloseKey(HKEY h)
{
    uint32_t v = (uint32_t)(uintptr_t)h;
    if (v >= 0x80000000u && v <= 0x80000005u)
        return ERROR_SUCCESS;
    return handle_close(h) ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

static LONG WINAPI RegFlushKey(HKEY h) { return ERROR_SUCCESS; }

static LONG WINAPI RegQueryValueExA(HKEY h, LPCSTR name, DWORD *res, DWORD *type, BYTE *data, DWORD *size)
{
    return query(h, name, type, data, size, false);
}

static LONG WINAPI RegQueryValueExW(HKEY h, LPCWSTR name, DWORD *res, DWORD *type, BYTE *data, DWORD *size)
{
    W1(name);
    LONG r = query(h, name_u, type, data, size, true);
    F1(name);
    return r;
}

static LONG WINAPI RegSetValueExA(HKEY h, LPCSTR name, DWORD res, DWORD type, const BYTE *data, DWORD size)
{
    if (string_type(type) && size && !data[size - 1])
        size--;
    return write_value(h, name, type, data, size);
}

static LONG WINAPI RegSetValueExW(HKEY h, LPCWSTR name, DWORD res, DWORD type, const BYTE *data, DWORD size)
{
    if (string_type(type) && size >= 2 && !((const WCHAR *)data)[size / 2 - 1])
        size -= 2;
    W1(name);
    LONG r = set_w(h, name_u, type, data, size);
    F1(name);
    return r;
}

static LONG WINAPI RegGetValueA(HKEY h, LPCSTR sub, LPCSTR name, DWORD flags, DWORD *type, void *data, DWORD *size)
{
    return get_value(h, sub, name, flags, type, data, size, false);
}

static LONG WINAPI RegGetValueW(HKEY h, LPCWSTR sub, LPCWSTR name, DWORD flags, DWORD *type, void *data, DWORD *size)
{
    W1(sub);
    W1(name);
    LONG r = get_value(h, sub_u, name_u, flags, type, data, size, true);
    F1(sub);
    F1(name);
    return r;
}

static LONG WINAPI RegDeleteValueA(HKEY h, LPCSTR name) { return delete_value(h, name); }

static LONG WINAPI RegDeleteValueW(HKEY h, LPCWSTR name)
{
    W1(name);
    LONG r = delete_value(h, name_u);
    F1(name);
    return r;
}

static LONG WINAPI RegDeleteKeyA(HKEY h, LPCSTR sub) { return delete_key(h, sub, false); }

static LONG WINAPI RegDeleteKeyW(HKEY h, LPCWSTR sub)
{
    W1(sub);
    LONG r = delete_key(h, sub_u, false);
    F1(sub);
    return r;
}

static LONG WINAPI RegDeleteKeyExW(HKEY h, LPCWSTR sub, DWORD sam, DWORD res) { return RegDeleteKeyW(h, sub); }
static LONG WINAPI RegDeleteTreeA(HKEY h, LPCSTR sub) { return delete_key(h, sub, true); }

static LONG WINAPI RegDeleteTreeW(HKEY h, LPCWSTR sub)
{
    W1(sub);
    LONG r = delete_key(h, sub_u, true);
    F1(sub);
    return r;
}

static LONG name_out_a(const char *name, LPSTR out, DWORD *len)
{
    DWORD n = strlen(name);
    if (*len <= n) {
        *len = n + 1;
        return ERROR_MORE_DATA;
    }
    memcpy(out, name, n + 1);
    *len = n;
    return ERROR_SUCCESS;
}

static LONG name_out_w(const char *name, LPWSTR out, DWORD *len)
{
    int n = utf8_to_utf16(name, -1, NULL, 0) - 1;
    if (*len <= (DWORD)n) {
        *len = n + 1;
        return ERROR_MORE_DATA;
    }
    utf8_to_utf16(name, -1, out, n + 1);
    *len = n;
    return ERROR_SUCCESS;
}

static LONG WINAPI RegEnumKeyExA(HKEY h, DWORD i, LPSTR name, DWORD *len, DWORD *res, LPSTR cls, DWORD *cls_len,
                                 FILETIME *ft)
{
    char buf[NAME_MAX + 1];
    LONG r = enum_entries(h, i, false, buf, sizeof buf);
    return r ? r : name_out_a(buf, name, len);
}

static LONG WINAPI RegEnumKeyExW(HKEY h, DWORD i, LPWSTR name, DWORD *len, DWORD *res, LPWSTR cls, DWORD *cls_len,
                                 FILETIME *ft)
{
    char buf[NAME_MAX + 1];
    LONG r = enum_entries(h, i, false, buf, sizeof buf);
    return r ? r : name_out_w(buf, name, len);
}

static LONG WINAPI RegEnumKeyA(HKEY h, DWORD i, LPSTR name, DWORD len) { return RegEnumKeyExA(h, i, name, &len, NULL, NULL, NULL, NULL); }
static LONG WINAPI RegEnumKeyW(HKEY h, DWORD i, LPWSTR name, DWORD len) { return RegEnumKeyExW(h, i, name, &len, NULL, NULL, NULL, NULL); }

static LONG WINAPI RegEnumValueA(HKEY h, DWORD i, LPSTR name, DWORD *len, DWORD *res, DWORD *type, BYTE *data,
                                 DWORD *size)
{
    char buf[NAME_MAX + 1];
    LONG r = enum_entries(h, i, true, buf, sizeof buf);
    if (r || (r = name_out_a(buf, name, len)))
        return r;
    return type || data || size ? query(h, buf, type, data, size, false) : ERROR_SUCCESS;
}

static LONG WINAPI RegEnumValueW(HKEY h, DWORD i, LPWSTR name, DWORD *len, DWORD *res, DWORD *type, BYTE *data,
                                 DWORD *size)
{
    char buf[NAME_MAX + 1];
    LONG r = enum_entries(h, i, true, buf, sizeof buf);
    if (r || (r = name_out_w(buf, name, len)))
        return r;
    return type || data || size ? query(h, buf, type, data, size, true) : ERROR_SUCCESS;
}

static LONG info(HKEY h, DWORD *subkeys, DWORD *max_sub, DWORD *values, DWORD *max_name, DWORD *max_data)
{
    char name[NAME_MAX + 1];
    DWORD k = 0, v = 0, ms = 0, mn = 0, md = 0;
    while (!enum_entries(h, k, false, name, sizeof name)) {
        k++;
        if (strlen(name) > ms)
            ms = strlen(name);
    }
    while (!enum_entries(h, v, true, name, sizeof name)) {
        v++;
        if (strlen(name) > mn)
            mn = strlen(name);
        DWORD size = 0;
        if (!query(h, name, NULL, NULL, &size, true) && size > md)
            md = size;
    }
    if (subkeys) *subkeys = k;
    if (max_sub) *max_sub = ms;
    if (values) *values = v;
    if (max_name) *max_name = mn;
    if (max_data) *max_data = md;
    return ERROR_SUCCESS;
}

static LONG WINAPI RegQueryInfoKeyA(HKEY h, LPSTR cls, DWORD *cls_len, DWORD *res, DWORD *subkeys, DWORD *max_sub,
                                    DWORD *max_cls, DWORD *values, DWORD *max_name, DWORD *max_data, DWORD *sec,
                                    FILETIME *ft)
{
    if (cls_len)
        *cls_len = 0;
    if (max_cls)
        *max_cls = 0;
    if (sec)
        *sec = 0;
    if (ft)
        *ft = (FILETIME){ 0, 0 };
    return info(h, subkeys, max_sub, values, max_name, max_data);
}

static LONG WINAPI RegQueryInfoKeyW(HKEY h, LPWSTR cls, DWORD *cls_len, DWORD *res, DWORD *subkeys, DWORD *max_sub,
                                    DWORD *max_cls, DWORD *values, DWORD *max_name, DWORD *max_data, DWORD *sec,
                                    FILETIME *ft)
{
    return RegQueryInfoKeyA(h, NULL, cls_len, res, subkeys, max_sub, max_cls, values, max_name, max_data, sec, ft);
}

BIT_TABLE(adv_registry,
          BIT_FN(RegOpenKeyExA), BIT_FN(RegOpenKeyExW), BIT_FN(RegOpenKeyA), BIT_FN(RegOpenKeyW),
          BIT_FN(RegCreateKeyExA), BIT_FN(RegCreateKeyExW), BIT_FN(RegCreateKeyA), BIT_FN(RegCreateKeyW),
          BIT_FN(RegCloseKey), BIT_FN(RegFlushKey),
          BIT_FN(RegQueryValueExA), BIT_FN(RegQueryValueExW), BIT_FN(RegSetValueExA), BIT_FN(RegSetValueExW),
          BIT_FN(RegGetValueA), BIT_FN(RegGetValueW),
          BIT_FN(RegDeleteValueA), BIT_FN(RegDeleteValueW), BIT_FN(RegDeleteKeyA), BIT_FN(RegDeleteKeyW),
          BIT_FN(RegDeleteKeyExW), BIT_FN(RegDeleteTreeA), BIT_FN(RegDeleteTreeW),
          BIT_FN(RegEnumKeyExA), BIT_FN(RegEnumKeyExW), BIT_FN(RegEnumKeyA), BIT_FN(RegEnumKeyW),
          BIT_FN(RegEnumValueA), BIT_FN(RegEnumValueW),
          BIT_FN(RegQueryInfoKeyA), BIT_FN(RegQueryInfoKeyW));
