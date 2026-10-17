#include "k32.h"

#include <screw/loader.h>
#include <screw/process.h>

#include <stdio.h>
#include <sys/stat.h>

#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4

static struct module *by_handle(HMODULE h)
{
    if (!h)
        return module_main();
    struct module *m = module_at(h);
    return m && m->base == h ? m : NULL;
}

static HMODULE handle_of(const char *name)
{
    if (!name)
        return module_main()->base;
    struct module *m = module_find(name);
    if (!m)
        last_error_set(ERROR_MOD_NOT_FOUND);
    return m ? m->base : NULL;
}

static HMODULE WINAPI GetModuleHandleA(LPCSTR name) { return handle_of(name); }

static HMODULE WINAPI GetModuleHandleW(LPCWSTR name)
{
    char *n = wide_to_host(name);
    HMODULE h = handle_of(n);
    free(n);
    return h;
}

static BOOL handle_ex(DWORD flags, const char *name, const void *addr, HMODULE *out)
{
    struct module *m = flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS ? module_at(addr)
                       : name ? module_find(name) : module_main();
    *out = m ? m->base : NULL;
    return m ? TRUE : fail(ERROR_MOD_NOT_FOUND);
}

static BOOL WINAPI GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *out)
{
    return handle_ex(flags, name, name, out);
}

static BOOL WINAPI GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *out)
{
    if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)
        return handle_ex(flags, NULL, name, out);
    char *n = wide_to_host(name);
    BOOL r = handle_ex(flags, n, NULL, out);
    free(n);
    return r;
}

static HMODULE load(const char *name)
{
    struct module *m = NULL;
    if (strchr(name, '\\') || strchr(name, '/')) {
        char *host = path_to_host(name);
        struct stat st;
        if (host && !stat(host, &st)) {
            const char *slash = strrchr(host, '/');
            char norm[MODULE_NAME_MAX];
            module_normalize(slash ? slash + 1 : host, norm);
            m = bit_find(norm) ? module_load(norm) : module_load_file(host);
        }
        free(host);
    }
    if (!m)
        m = module_load(name);
    if (!m) {
        last_error_set(ERROR_MOD_NOT_FOUND);
        return NULL;
    }
    modules_attach();
    return m->base;
}

static HMODULE WINAPI LoadLibraryA(LPCSTR name) { return load(name); }

static HMODULE WINAPI LoadLibraryW(LPCWSTR name)
{
    char *n = wide_to_host(name);
    HMODULE h = load(n);
    free(n);
    return h;
}

static HMODULE WINAPI LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags) { return LoadLibraryA(name); }
static HMODULE WINAPI LoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags) { return LoadLibraryW(name); }

static BOOL WINAPI FreeLibrary(HMODULE h) { return by_handle(h) ? TRUE : fail(ERROR_INVALID_HANDLE); }

static BOOL WINAPI DisableThreadLibraryCalls(HMODULE h) { return TRUE; }

static FARPROC WINAPI GetProcAddress(HMODULE h, LPCSTR name)
{
    struct module *m = by_handle(h);
    if (!m) {
        last_error_set(ERROR_MOD_NOT_FOUND);
        return NULL;
    }
    void *p = (uintptr_t)name < 0x10000 ? module_ordinal(m, (uintptr_t)name) : module_symbol(m, name);
    if (!p)
        last_error_set(ERROR_PROC_NOT_FOUND);
    return p;
}

static char *file_name(HMODULE h)
{
    struct module *m = by_handle(h);
    if (!m)
        return NULL;
    if (m == module_main())
        return strdup(process_exe_windows());
    if (m->path)
        return path_to_windows(m->path);
    char *out = malloc(strlen(m->name) + 32);
    sprintf(out, "C:\\windows\\system32\\%s", m->name);
    return out;
}

static DWORD truncated_a(const char *src, LPSTR dst, DWORD size)
{
    if (!size)
        return 0;
    DWORD len = strlen(src);
    if (len >= size) {
        memcpy(dst, src, size - 1);
        dst[size - 1] = 0;
        last_error_set(ERROR_INSUFFICIENT_BUFFER);
        return size;
    }
    memcpy(dst, src, len + 1);
    return len;
}

static DWORD WINAPI GetModuleFileNameA(HMODULE h, LPSTR out, DWORD size)
{
    char *name = file_name(h);
    if (!name)
        return fail(ERROR_MOD_NOT_FOUND);
    DWORD r = truncated_a(name, out, size);
    free(name);
    return r;
}

static DWORD WINAPI GetModuleFileNameW(HMODULE h, LPWSTR out, DWORD size)
{
    char *name = file_name(h);
    if (!name)
        return fail(ERROR_MOD_NOT_FOUND);
    WCHAR *w = host_to_wide(name);
    free(name);
    if (!size) {
        free(w);
        return 0;
    }
    DWORD len = wstr_len(w);
    DWORD n = len < size ? len : size - 1;
    memcpy(out, w, n * sizeof(WCHAR));
    out[n] = 0;
    free(w);
    if (len >= size) {
        last_error_set(ERROR_INSUFFICIENT_BUFFER);
        return size;
    }
    return len;
}

BIT_TABLE(k32_module,
          BIT_FN(GetModuleHandleA), BIT_FN(GetModuleHandleW),
          BIT_FN(GetModuleHandleExA), BIT_FN(GetModuleHandleExW),
          BIT_FN(LoadLibraryA), BIT_FN(LoadLibraryW),
          BIT_FN(LoadLibraryExA), BIT_FN(LoadLibraryExW),
          BIT_FN(FreeLibrary), BIT_FN(DisableThreadLibraryCalls),
          BIT_FN(GetProcAddress),
          BIT_FN(GetModuleFileNameA), BIT_FN(GetModuleFileNameW));
