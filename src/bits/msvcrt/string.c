#include "crt.h"
#include "wrap.h"

#include <ctype.h>
#include <errno.h>
#include <strings.h>

WRAP3(void *, memcpy, void *, const void *, size_t)
WRAP3(void *, memmove, void *, const void *, size_t)
WRAP3(void *, memset, void *, int, size_t)
WRAP3(int, memcmp, const void *, const void *, size_t)
WRAP3(void *, memchr, const void *, int, size_t)
WRAP1(size_t, strlen, const char *)
WRAP2(size_t, strnlen, const char *, size_t)
WRAP2(char *, strcpy, char *, const char *)
WRAP3(char *, strncpy, char *, const char *, size_t)
WRAP2(char *, strcat, char *, const char *)
WRAP3(char *, strncat, char *, const char *, size_t)
WRAP2(int, strcmp, const char *, const char *)
WRAP3(int, strncmp, const char *, const char *, size_t)
WRAP2(char *, strchr, const char *, int)
WRAP2(char *, strrchr, const char *, int)
WRAP2(char *, strstr, const char *, const char *)
WRAP2(size_t, strspn, const char *, const char *)
WRAP2(size_t, strcspn, const char *, const char *)
WRAP2(char *, strpbrk, const char *, const char *)
WRAP2(int, strcoll, const char *, const char *)
WRAP3(size_t, strxfrm, char *, const char *, size_t)
WRAP1(char *, strerror, int)
WRAP1(char *, strdup, const char *)
WRAP2(int, strcasecmp, const char *, const char *)
WRAP3(int, strncasecmp, const char *, const char *, size_t)

static __thread char *tok_state;

static char *WINAPI strtok_(char *s, const char *delim) { return strtok_r(s, delim, &tok_state); }
static char *WINAPI strtok_s(char *s, const char *delim, char **ctx) { return strtok_r(s, delim, ctx); }

static int WINAPI _memicmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) {
        int d = tolower(x[i]) - tolower(y[i]);
        if (d)
            return d;
    }
    return 0;
}

static char *WINAPI _strlwr(char *s)
{
    for (char *p = s; *p; p++)
        *p = tolower((unsigned char)*p);
    return s;
}

static char *WINAPI _strupr(char *s)
{
    for (char *p = s; *p; p++)
        *p = toupper((unsigned char)*p);
    return s;
}

static char *WINAPI _strrev(char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
    return s;
}

static char *WINAPI _strset(char *s, int c)
{
    for (char *p = s; *p; p++)
        *p = c;
    return s;
}

static char *WINAPI _strnset(char *s, int c, size_t n)
{
    for (char *p = s; *p && n; p++, n--)
        *p = c;
    return s;
}

static int WINAPI _strlwr_s(char *s, size_t n) { return s && strnlen(s, n) < n ? (_strlwr(s), 0) : EINVAL; }
static int WINAPI _strupr_s(char *s, size_t n) { return s && strnlen(s, n) < n ? (_strupr(s), 0) : EINVAL; }

static int WINAPI memcpy_s(void *dst, size_t size, const void *src, size_t n)
{
    if (!n)
        return 0;
    if (!dst || !src || size < n) {
        if (dst && size)
            memset(dst, 0, size);
        return !dst || !src ? EINVAL : ERANGE;
    }
    memcpy(dst, src, n);
    return 0;
}

static int WINAPI memmove_s(void *dst, size_t size, const void *src, size_t n)
{
    if (!n)
        return 0;
    if (!dst || !src)
        return EINVAL;
    if (size < n)
        return ERANGE;
    memmove(dst, src, n);
    return 0;
}

static int WINAPI strcpy_s(char *dst, size_t size, const char *src)
{
    if (!dst || !size)
        return EINVAL;
    if (!src) {
        dst[0] = 0;
        return EINVAL;
    }
    size_t n = strlen(src);
    if (n >= size) {
        dst[0] = 0;
        return ERANGE;
    }
    memcpy(dst, src, n + 1);
    return 0;
}

static int WINAPI strncpy_s(char *dst, size_t size, const char *src, size_t count)
{
    if (!dst || !size)
        return EINVAL;
    if (!src) {
        dst[0] = 0;
        return count ? EINVAL : 0;
    }
    size_t n = strnlen(src, count == (size_t)-1 ? size - 1 : count);
    if (n >= size) {
        if (count == (size_t)-1) {
            memcpy(dst, src, size - 1);
            dst[size - 1] = 0;
            return 80;
        }
        dst[0] = 0;
        return ERANGE;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}

static int WINAPI strcat_s(char *dst, size_t size, const char *src)
{
    if (!dst || !size)
        return EINVAL;
    size_t d = strnlen(dst, size);
    if (d == size)
        return EINVAL;
    return strcpy_s(dst + d, size - d, src);
}

static int WINAPI strncat_s(char *dst, size_t size, const char *src, size_t count)
{
    if (!dst || !size)
        return EINVAL;
    size_t d = strnlen(dst, size);
    if (d == size)
        return EINVAL;
    return strncpy_s(dst + d, size - d, src, count);
}

static size_t WINAPI strnlen_s(const char *s, size_t n) { return s ? strnlen(s, n) : 0; }

static int WINAPI strerror_s(char *buf, size_t size, int e) { return strcpy_s(buf, size, strerror(e)) ? ERANGE : 0; }

static char *WINAPI _strerror(const char *prefix)
{
    static __thread char buf[256];
    snprintf(buf, sizeof buf, "%s%s%s\n", prefix ? prefix : "", prefix ? ": " : "", strerror(*crt_errno()));
    return buf;
}

BIT_TABLE(crt_string,
          EXP(memcpy), EXP(memmove), EXP(memset), EXP(memcmp), EXP(memchr),
          EXP(strlen), EXP(strnlen), EXP(strcpy), EXP(strncpy), EXP(strcat), EXP(strncat),
          EXP(strcmp), EXP(strncmp), EXP(strchr), EXP(strrchr), EXP(strstr),
          EXP(strspn), EXP(strcspn), EXP(strpbrk), EXP(strcoll), EXP(strxfrm), EXP(strerror),
          BIT_AS("_strdup", strdup_), EXP(strdup),
          BIT_AS("_stricmp", strcasecmp_), BIT_AS("_strcmpi", strcasecmp_), BIT_AS("_stricoll", strcasecmp_),
          BIT_AS("_strnicmp", strncasecmp_), BIT_AS("_strncoll", strncmp_), BIT_AS("_strnicoll", strncasecmp_),
          EXP(strtok), BIT_FN(strtok_s), BIT_FN(_memicmp),
          BIT_FN(_strlwr), BIT_FN(_strupr), BIT_FN(_strrev), BIT_FN(_strset), BIT_FN(_strnset),
          BIT_FN(_strlwr_s), BIT_FN(_strupr_s),
          BIT_FN(memcpy_s), BIT_FN(memmove_s), BIT_FN(strcpy_s), BIT_FN(strncpy_s),
          BIT_FN(strcat_s), BIT_FN(strncat_s), BIT_FN(strnlen_s), BIT_FN(strerror_s), BIT_FN(_strerror));
