#include "k32.h"

#include <screw/process.h>

#include <stdio.h>

typedef struct {
    DWORD size;
    LPSTR reserved;
    LPSTR desktop;
    LPSTR title;
    DWORD x, y, width, height, columns, rows, fill, flags;
    WORD show;
    WORD reserved2_size;
    BYTE *reserved2;
    HANDLE input;
    HANDLE output;
    HANDLE error;
} STARTUPINFO;

#define STARTF_USESTDHANDLES 0x100u

static LPSTR WINAPI GetCommandLineA(void) { return (LPSTR)process_cmdline(); }
static LPWSTR WINAPI GetCommandLineW(void) { return (LPWSTR)process_wcmdline(); }

static DWORD WINAPI GetEnvironmentVariableA(LPCSTR name, LPSTR out, DWORD size)
{
    const char *v = env_get(name);
    if (!v)
        return fail(ERROR_ENVVAR_NOT_FOUND);
    return copy_out_a(v, out, size);
}

static DWORD WINAPI GetEnvironmentVariableW(LPCWSTR name, LPWSTR out, DWORD size)
{
    char *n = wide_to_host(name);
    const char *v = env_get(n);
    free(n);
    if (!v)
        return fail(ERROR_ENVVAR_NOT_FOUND);
    return copy_out_w(v, out, size);
}

static BOOL WINAPI SetEnvironmentVariableA(LPCSTR name, LPCSTR value)
{
    env_set(name, value);
    return TRUE;
}

static BOOL WINAPI SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value)
{
    char *n = wide_to_host(name), *v = wide_to_host(value);
    env_set(n, v);
    free(n);
    free(v);
    return TRUE;
}

static LPSTR WINAPI GetEnvironmentStrings(void) { return env_block(); }
static LPWSTR WINAPI GetEnvironmentStringsW(void) { return env_block_w(); }

static BOOL WINAPI FreeEnvironmentStringsA(LPSTR block)
{
    free(block);
    return TRUE;
}

static BOOL WINAPI FreeEnvironmentStringsW(LPWSTR block)
{
    free(block);
    return TRUE;
}

static char *expand(const char *src)
{
    size_t cap = strlen(src) + 64, n = 0;
    char *out = malloc(cap);
    for (const char *p = src; *p;) {
        const char *end = *p == '%' ? strchr(p + 1, '%') : NULL;
        const char *val = NULL;
        size_t take = 1;
        if (end && end > p + 1) {
            char name[256];
            size_t len = end - p - 1 < 255 ? (size_t)(end - p - 1) : 255;
            memcpy(name, p + 1, len);
            name[len] = 0;
            val = env_get(name);
            if (val)
                take = end - p + 1;
        }
        const char *chunk = val ? val : p;
        size_t clen = val ? strlen(val) : 1;
        if (n + clen + 1 > cap) {
            cap = (n + clen + 1) * 2;
            out = realloc(out, cap);
        }
        memcpy(out + n, chunk, clen);
        n += clen;
        p += take;
    }
    out[n] = 0;
    return out;
}

static DWORD WINAPI ExpandEnvironmentStringsA(LPCSTR src, LPSTR out, DWORD size)
{
    char *e = expand(src);
    DWORD need = strlen(e) + 1;
    if (out && size >= need)
        memcpy(out, e, need);
    free(e);
    return need;
}

static DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR src, LPWSTR out, DWORD size)
{
    char *s = wide_to_host(src);
    char *e = expand(s);
    free(s);
    DWORD need = utf8_to_utf16(e, -1, NULL, 0);
    if (out && size >= need)
        utf8_to_utf16(e, -1, out, size);
    free(e);
    return need;
}

static void startup(STARTUPINFO *si)
{
    DWORD size = si->size ? si->size : sizeof *si;
    memset(si, 0, size < sizeof *si ? size : sizeof *si);
    si->size = sizeof *si;
    si->flags = STARTF_USESTDHANDLES;
    si->show = 1;
    si->input = std_handle(0);
    si->output = std_handle(1);
    si->error = std_handle(2);
}

static void WINAPI GetStartupInfoA(STARTUPINFO *si) { startup(si); }
static void WINAPI GetStartupInfoW(STARTUPINFO *si) { startup(si); }

BIT_TABLE(k32_env,
          BIT_FN(GetCommandLineA), BIT_FN(GetCommandLineW),
          BIT_FN(GetEnvironmentVariableA), BIT_FN(GetEnvironmentVariableW),
          BIT_FN(SetEnvironmentVariableA), BIT_FN(SetEnvironmentVariableW),
          BIT_FN(GetEnvironmentStrings), BIT_AS("GetEnvironmentStringsA", GetEnvironmentStrings),
          BIT_FN(GetEnvironmentStringsW),
          BIT_FN(FreeEnvironmentStringsA), BIT_FN(FreeEnvironmentStringsW),
          BIT_FN(ExpandEnvironmentStringsA), BIT_FN(ExpandEnvironmentStringsW),
          BIT_FN(GetStartupInfoA), BIT_FN(GetStartupInfoW));
