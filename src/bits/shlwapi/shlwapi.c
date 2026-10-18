#include <screw/bit.h>
#include <screw/path.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <ctype.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static BOOL is_sep(char c) { return c == '\\' || c == '/'; }

static BOOL WINAPI PathIsRelativeA(LPCSTR p)
{
    if (!p || !*p)
        return TRUE;
    if (is_sep(p[0]))
        return FALSE;
    if (isalpha((unsigned char)p[0]) && p[1] == ':')
        return FALSE;
    return TRUE;
}

static BOOL WINAPI PathIsRelativeW(LPCWSTR p)
{
    if (!p || !*p)
        return TRUE;
    if (p[0] == '\\' || p[0] == '/')
        return FALSE;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':')
        return FALSE;
    return TRUE;
}

static LPSTR WINAPI PathFindExtensionA(LPCSTR p)
{
    const char *dot = NULL;
    for (const char *s = p; *s; s++) {
        if (*s == '.')
            dot = s;
        else if (is_sep(*s))
            dot = NULL;
    }
    return (LPSTR)(dot ? dot : p + strlen(p));
}

static LPWSTR WINAPI PathFindExtensionW(LPCWSTR p)
{
    const WCHAR *dot = NULL;
    for (const WCHAR *s = p; *s; s++) {
        if (*s == '.')
            dot = s;
        else if (*s == '\\' || *s == '/')
            dot = NULL;
    }
    return (LPWSTR)(dot ? dot : p + wstr_len(p));
}

static LPSTR WINAPI PathFindFileNameA(LPCSTR p)
{
    const char *base = p;
    for (const char *s = p; *s; s++)
        if (is_sep(*s))
            base = s + 1;
    return (LPSTR)base;
}

static LPWSTR WINAPI PathFindFileNameW(LPCWSTR p)
{
    const WCHAR *base = p;
    for (const WCHAR *s = p; *s; s++)
        if (*s == '\\' || *s == '/')
            base = s + 1;
    return (LPWSTR)base;
}

static BOOL WINAPI PathRemoveFileSpecA(LPSTR p)
{
    char *last = NULL;
    for (char *s = p; *s; s++)
        if (is_sep(*s))
            last = s;
    if (!last)
        return FALSE;
    *last = 0;
    return TRUE;
}

static BOOL WINAPI PathRemoveFileSpecW(LPWSTR p)
{
    WCHAR *last = NULL;
    for (WCHAR *s = p; *s; s++)
        if (*s == '\\' || *s == '/')
            last = s;
    if (!last)
        return FALSE;
    *last = 0;
    return TRUE;
}

static void WINAPI PathStripPathA(LPSTR p)
{
    char *base = PathFindFileNameA(p);
    if (base != p)
        memmove(p, base, strlen(base) + 1);
}

static LPSTR WINAPI PathAddBackslashA(LPSTR p)
{
    size_t n = strlen(p);
    if (n && !is_sep(p[n - 1])) {
        p[n] = '\\';
        p[n + 1] = 0;
    }
    return p + strlen(p);
}

static BOOL path_exists(const char *win)
{
    char *host = path_to_host(win);
    BOOL ok = host && !access(host, F_OK);
    free(host);
    return ok;
}

static BOOL WINAPI PathFileExistsA(LPCSTR p) { return path_exists(p); }

static BOOL WINAPI PathFileExistsW(LPCWSTR p)
{
    char *u = wide_to_host(p);
    BOOL ok = path_exists(u);
    free(u);
    return ok;
}

static LPCSTR WINAPI StrStrIA(LPCSTR hay, LPCSTR needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (!strncasecmp(hay, needle, n))
            return hay;
    return NULL;
}

static int WINAPI StrCmpIW(LPCWSTR a, LPCWSTR b)
{
    for (; *a && *b; a++, b++) {
        WCHAR ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        WCHAR cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb)
            return (int)ca - (int)cb;
    }
    return (int)*a - (int)*b;
}

static char *concat_path(char *dst, const char *more)
{
    if (*dst && !is_sep(dst[strlen(dst) - 1]))
        strcat(dst, "\\");
    while (is_sep(*more))
        more++;
    strcat(dst, more);
    return dst;
}

static LPSTR WINAPI PathAppendA(LPSTR path, LPCSTR more)
{
    concat_path(path, more);
    return path;
}

static LPSTR WINAPI PathCombineA(LPSTR out, LPCSTR dir, LPCSTR file)
{
    if (file && !PathIsRelativeA(file)) {
        strcpy(out, file);
        return out;
    }
    strcpy(out, dir ? dir : "");
    if (file)
        concat_path(out, file);
    return out;
}

BIT_TABLE(shlwapi_exports,
          BIT_FN(PathIsRelativeA), BIT_FN(PathIsRelativeW),
          BIT_FN(PathFindExtensionA), BIT_FN(PathFindExtensionW),
          BIT_FN(PathFindFileNameA), BIT_FN(PathFindFileNameW),
          BIT_FN(PathRemoveFileSpecA), BIT_FN(PathRemoveFileSpecW),
          BIT_FN(PathStripPathA), BIT_FN(PathAddBackslashA),
          BIT_FN(PathFileExistsA), BIT_FN(PathFileExistsW),
          BIT_FN(StrStrIA), BIT_FN(StrCmpIW),
          BIT_FN(PathAppendA), BIT_FN(PathCombineA));

BIT_DEFINE(bit_shlwapi, "shlwapi.dll", &shlwapi_exports);
