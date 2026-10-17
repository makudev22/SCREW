#include "crt.h"

#include <errno.h>
#include <wctype.h>

static WCHAR lower(WCHAR c) { return (WCHAR)towlower(c); }
static WCHAR upper(WCHAR c) { return (WCHAR)towupper(c); }

static size_t WINAPI wcslen_(const WCHAR *s) { return wstr_len(s); }

static size_t WINAPI wcsnlen_(const WCHAR *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}

static WCHAR *WINAPI wcscpy_(WCHAR *d, const WCHAR *s)
{
    WCHAR *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

static WCHAR *WINAPI wcsncpy_(WCHAR *d, const WCHAR *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}

static WCHAR *WINAPI wcscat_(WCHAR *d, const WCHAR *s)
{
    wcscpy_(d + wstr_len(d), s);
    return d;
}

static WCHAR *WINAPI wcsncat_(WCHAR *d, const WCHAR *s, size_t n)
{
    WCHAR *e = d + wstr_len(d);
    size_t i = 0;
    for (; i < n && s[i]; i++)
        e[i] = s[i];
    e[i] = 0;
    return d;
}

static int WINAPI wcscmp_(const WCHAR *a, const WCHAR *b) { return wstr_cmp(a, b); }

static int WINAPI wcsncmp_(const WCHAR *a, const WCHAR *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
        if (!a[i])
            break;
    }
    return 0;
}

static int WINAPI _wcsnicmp(const WCHAR *a, const WCHAR *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        WCHAR x = lower(a[i]), y = lower(b[i]);
        if (x != y)
            return (int)x - (int)y;
        if (!x)
            break;
    }
    return 0;
}

static int WINAPI _wcsicmp(const WCHAR *a, const WCHAR *b) { return _wcsnicmp(a, b, (size_t)-1); }

static WCHAR *WINAPI wcschr_(const WCHAR *s, WCHAR c)
{
    for (;; s++) {
        if (*s == c)
            return (WCHAR *)s;
        if (!*s)
            return NULL;
    }
}

static WCHAR *WINAPI wcsrchr_(const WCHAR *s, WCHAR c)
{
    const WCHAR *r = NULL;
    for (;; s++) {
        if (*s == c)
            r = s;
        if (!*s)
            return (WCHAR *)r;
    }
}

static WCHAR *WINAPI wcsstr_(const WCHAR *h, const WCHAR *n)
{
    size_t len = wstr_len(n);
    if (!len)
        return (WCHAR *)h;
    for (; *h; h++)
        if (!wcsncmp_(h, n, len))
            return (WCHAR *)h;
    return NULL;
}

static size_t WINAPI wcsspn_(const WCHAR *s, const WCHAR *set)
{
    size_t i = 0;
    while (s[i] && wcschr_(set, s[i]))
        i++;
    return i;
}

static size_t WINAPI wcscspn_(const WCHAR *s, const WCHAR *set)
{
    size_t i = 0;
    while (s[i] && !wcschr_(set, s[i]))
        i++;
    return i;
}

static WCHAR *WINAPI wcspbrk_(const WCHAR *s, const WCHAR *set)
{
    s += wcscspn_(s, set);
    return *s ? (WCHAR *)s : NULL;
}

static WCHAR *WINAPI wcstok_s(WCHAR *s, const WCHAR *delim, WCHAR **ctx)
{
    if (!s)
        s = *ctx;
    if (!s)
        return NULL;
    s += wcsspn_(s, delim);
    if (!*s) {
        *ctx = NULL;
        return NULL;
    }
    WCHAR *end = s + wcscspn_(s, delim);
    if (*end)
        *end++ = 0;
    *ctx = end;
    return s;
}

static __thread WCHAR *tok_state;
static WCHAR *WINAPI wcstok_(WCHAR *s, const WCHAR *delim) { return wcstok_s(s, delim, &tok_state); }

static WCHAR *WINAPI _wcsdup(const WCHAR *s) { return s ? wstr_dup(s) : NULL; }

static WCHAR *WINAPI _wcslwr(WCHAR *s)
{
    for (WCHAR *p = s; *p; p++)
        *p = lower(*p);
    return s;
}

static WCHAR *WINAPI _wcsupr(WCHAR *s)
{
    for (WCHAR *p = s; *p; p++)
        *p = upper(*p);
    return s;
}

static WCHAR *WINAPI _wcsrev(WCHAR *s)
{
    size_t n = wstr_len(s);
    for (size_t i = 0; i < n / 2; i++) {
        WCHAR t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
    return s;
}

static int WINAPI wcscoll_(const WCHAR *a, const WCHAR *b) { return wstr_cmp(a, b); }

static WCHAR *WINAPI wmemcpy_(WCHAR *d, const WCHAR *s, size_t n) { return memcpy(d, s, n * 2); }
static WCHAR *WINAPI wmemmove_(WCHAR *d, const WCHAR *s, size_t n) { return memmove(d, s, n * 2); }

static WCHAR *WINAPI wmemset_(WCHAR *d, WCHAR c, size_t n)
{
    for (size_t i = 0; i < n; i++)
        d[i] = c;
    return d;
}

static int WINAPI wmemcmp_(const WCHAR *a, const WCHAR *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

static WCHAR *WINAPI wmemchr_(const WCHAR *s, WCHAR c, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (s[i] == c)
            return (WCHAR *)s + i;
    return NULL;
}

static int WINAPI wcscpy_s(WCHAR *d, size_t size, const WCHAR *s)
{
    if (!d || !size)
        return EINVAL;
    if (!s) {
        d[0] = 0;
        return EINVAL;
    }
    size_t n = wstr_len(s);
    if (n >= size) {
        d[0] = 0;
        return ERANGE;
    }
    memcpy(d, s, (n + 1) * 2);
    return 0;
}

static int WINAPI wcsncpy_s(WCHAR *d, size_t size, const WCHAR *s, size_t count)
{
    if (!d || !size)
        return EINVAL;
    if (!s) {
        d[0] = 0;
        return count ? EINVAL : 0;
    }
    size_t n = wcsnlen_(s, count == (size_t)-1 ? size - 1 : count);
    if (n >= size) {
        if (count == (size_t)-1) {
            memcpy(d, s, (size - 1) * 2);
            d[size - 1] = 0;
            return 80;
        }
        d[0] = 0;
        return ERANGE;
    }
    memcpy(d, s, n * 2);
    d[n] = 0;
    return 0;
}

static int WINAPI wcscat_s(WCHAR *d, size_t size, const WCHAR *s)
{
    if (!d || !size)
        return EINVAL;
    size_t n = wcsnlen_(d, size);
    return n == size ? EINVAL : wcscpy_s(d + n, size - n, s);
}

static int WINAPI wcsncat_s(WCHAR *d, size_t size, const WCHAR *s, size_t count)
{
    if (!d || !size)
        return EINVAL;
    size_t n = wcsnlen_(d, size);
    return n == size ? EINVAL : wcsncpy_s(d + n, size - n, s, count);
}

static char *ascii_view(const WCHAR *s)
{
    size_t n = wstr_len(s);
    char *a = malloc(n + 1);
    for (size_t i = 0; i <= n; i++)
        a[i] = s[i] < 0x80 ? (char)s[i] : '\x01';
    return a;
}

#define PARSE(type, call)                             \
    char *a = ascii_view(s), *e;                      \
    errno = 0;                                        \
    type v = call;                                    \
    if (end)                                          \
        *end = (WCHAR *)s + (e - a);                  \
    if (errno)                                        \
        crt_errno_set(errno);                         \
    free(a);                                          \
    return v

static LONG WINAPI wcstol_(const WCHAR *s, WCHAR **end, int base)
{
    char *a = ascii_view(s), *e;
    errno = 0;
    long v = strtol(a, &e, base);
    if (v > INT32_MAX || v < INT32_MIN) {
        v = v > 0 ? INT32_MAX : INT32_MIN;
        errno = ERANGE;
    }
    if (end)
        *end = (WCHAR *)s + (e - a);
    if (errno)
        crt_errno_set(errno);
    free(a);
    return v;
}

static ULONG WINAPI wcstoul_(const WCHAR *s, WCHAR **end, int base)
{
    char *a = ascii_view(s), *e;
    errno = 0;
    unsigned long v = strtoul(a, &e, base);
    if (v > UINT32_MAX && (long)v > 0) {
        v = UINT32_MAX;
        errno = ERANGE;
    }
    if (end)
        *end = (WCHAR *)s + (e - a);
    if (errno)
        crt_errno_set(errno);
    free(a);
    return (ULONG)v;
}

static int64_t WINAPI wcstoll_(const WCHAR *s, WCHAR **end, int base) { PARSE(int64_t, strtoll(a, &e, base)); }
static uint64_t WINAPI wcstoull_(const WCHAR *s, WCHAR **end, int base) { PARSE(uint64_t, strtoull(a, &e, base)); }
static double WINAPI wcstod_(const WCHAR *s, WCHAR **end) { PARSE(double, strtod(a, &e)); }
static float WINAPI wcstof_(const WCHAR *s, WCHAR **end) { PARSE(float, strtof(a, &e)); }

static int WINAPI _wtoi(const WCHAR *s) { return wcstol_(s, NULL, 10); }
static int64_t WINAPI _wtoi64(const WCHAR *s) { return wcstoll_(s, NULL, 10); }
static double WINAPI _wtof(const WCHAR *s) { return wcstod_(s, NULL); }

static WCHAR *to_wide_num(uint64_t v, bool neg, WCHAR *out, int radix)
{
    char buf[72], *p = buf + sizeof buf - 1;
    *p = 0;
    do {
        int d = v % radix;
        *--p = d < 10 ? '0' + d : 'a' + d - 10;
        v /= radix;
    } while (v);
    if (neg)
        *--p = '-';
    utf8_to_utf16(p, -1, out, 72);
    return out;
}

static WCHAR *WINAPI _itow(int v, WCHAR *out, int radix)
{
    bool neg = radix == 10 && v < 0;
    return to_wide_num(neg ? -(int64_t)v : (uint32_t)v, neg, out, radix);
}

static WCHAR *WINAPI _i64tow(int64_t v, WCHAR *out, int radix)
{
    bool neg = radix == 10 && v < 0;
    return to_wide_num(neg ? -(uint64_t)v : (uint64_t)v, neg, out, radix);
}

static WCHAR *WINAPI _ultow(ULONG v, WCHAR *out, int radix) { return to_wide_num(v, false, out, radix); }
static WCHAR *WINAPI _ui64tow(uint64_t v, WCHAR *out, int radix) { return to_wide_num(v, false, out, radix); }

static int WINAPI _itow_s(int v, WCHAR *out, size_t size, int radix)
{
    WCHAR tmp[72];
    _itow(v, tmp, radix);
    return wcscpy_s(out, size, tmp);
}

static WCHAR WINAPI towlower_(WCHAR c) { return lower(c); }
static WCHAR WINAPI towupper_(WCHAR c) { return upper(c); }
static int WINAPI iswalpha_(WCHAR c) { return iswalpha(c); }
static int WINAPI iswdigit_(WCHAR c) { return iswdigit(c); }
static int WINAPI iswspace_(WCHAR c) { return iswspace(c); }
static int WINAPI iswupper_(WCHAR c) { return iswupper(c); }
static int WINAPI iswlower_(WCHAR c) { return iswlower(c); }
static int WINAPI iswalnum_(WCHAR c) { return iswalnum(c); }
static int WINAPI iswpunct_(WCHAR c) { return iswpunct(c); }
static int WINAPI iswxdigit_(WCHAR c) { return iswxdigit(c); }
static int WINAPI iswprint_(WCHAR c) { return iswprint(c); }
static int WINAPI iswcntrl_(WCHAR c) { return iswcntrl(c); }
static int WINAPI iswgraph_(WCHAR c) { return iswgraph(c); }

static int WINAPI iswctype_(WCHAR c, WORD mask)
{
    int r = 0;
    if ((mask & 0x1) && iswupper(c)) r |= 0x1;
    if ((mask & 0x2) && iswlower(c)) r |= 0x2;
    if ((mask & 0x4) && iswdigit(c)) r |= 0x4;
    if ((mask & 0x8) && iswspace(c)) r |= 0x8;
    if ((mask & 0x10) && iswpunct(c)) r |= 0x10;
    if ((mask & 0x20) && iswcntrl(c)) r |= 0x20;
    if ((mask & 0x40) && c == ' ') r |= 0x40;
    if ((mask & 0x80) && iswxdigit(c)) r |= 0x80;
    if ((mask & 0x100) && iswalpha(c)) r |= 0x100;
    return r;
}

static size_t WINAPI mbstowcs_(WCHAR *dst, const char *src, size_t n)
{
    int need = utf8_to_utf16(src, -1, NULL, 0) - 1;
    if (!dst)
        return need;
    WCHAR *tmp = malloc((need + 1) * 2);
    utf8_to_utf16(src, -1, tmp, need + 1);
    size_t copy = (size_t)need < n ? (size_t)need + 1 : n;
    memcpy(dst, tmp, copy * 2);
    free(tmp);
    return copy > (size_t)need ? (size_t)need : copy;
}

static size_t WINAPI wcstombs_(char *dst, const WCHAR *src, size_t n)
{
    int need = utf16_to_utf8(src, -1, NULL, 0) - 1;
    if (!dst)
        return need;
    char *tmp = malloc(need + 1);
    utf16_to_utf8(src, -1, tmp, need + 1);
    size_t copy = (size_t)need < n ? (size_t)need + 1 : n;
    memcpy(dst, tmp, copy);
    free(tmp);
    return copy > (size_t)need ? (size_t)need : copy;
}

static int WINAPI mbtowc_(WCHAR *dst, const char *src, size_t n)
{
    if (!src)
        return 0;
    if (!*src) {
        if (dst)
            *dst = 0;
        return 0;
    }
    unsigned char c = *src;
    int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    if ((size_t)len > n)
        return -1;
    WCHAR w[2];
    utf8_to_utf16(src, len, w, 2);
    if (dst)
        *dst = w[0];
    return len;
}

static int WINAPI wctomb_(char *dst, WCHAR c)
{
    if (!dst)
        return 0;
    return utf16_to_utf8(&c, 1, dst, 4);
}

static int WINAPI mblen_(const char *s, size_t n) { return mbtowc_(NULL, s, n); }
static WCHAR WINAPI btowc_(int c) { return c < 0x80 && c >= 0 ? c : 0xFFFF; }
static int WINAPI wctob_(WCHAR c) { return c < 0x80 ? c : -1; }

static size_t WINAPI mbrtowc_(WCHAR *dst, const char *src, size_t n, void *state)
{
    int r = mbtowc_(dst, src, n);
    return r < 0 ? (size_t)-2 : (size_t)r;
}

static size_t WINAPI wcrtomb_(char *dst, WCHAR c, void *state) { return wctomb_(dst, c); }

static size_t WINAPI mbrlen_(const char *s, size_t n, void *state) { return mbrtowc_(NULL, s, n, state); }

static size_t WINAPI wcsxfrm_(WCHAR *d, const WCHAR *s, size_t n)
{
    size_t len = wstr_len(s);
    if (d && n)
        wcsncpy_(d, s, n);
    return len;
}

BIT_TABLE(crt_wide,
          BIT_AS("wcslen", wcslen_), BIT_AS("wcsnlen", wcsnlen_), BIT_AS("wcscpy", wcscpy_),
          BIT_AS("wcsncpy", wcsncpy_), BIT_AS("wcscat", wcscat_), BIT_AS("wcsncat", wcsncat_),
          BIT_AS("wcscmp", wcscmp_), BIT_AS("wcsncmp", wcsncmp_), BIT_FN(_wcsicmp), BIT_FN(_wcsnicmp),
          BIT_AS("_wcscmpi", _wcsicmp), BIT_AS("_wcsicoll", _wcsicmp), BIT_AS("_wcsnicoll", _wcsnicmp),
          BIT_AS("wcschr", wcschr_), BIT_AS("wcsrchr", wcsrchr_), BIT_AS("wcsstr", wcsstr_),
          BIT_AS("wcsspn", wcsspn_), BIT_AS("wcscspn", wcscspn_), BIT_AS("wcspbrk", wcspbrk_),
          BIT_AS("wcstok", wcstok_), BIT_FN(wcstok_s), BIT_FN(_wcsdup), BIT_AS("wcsdup", _wcsdup),
          BIT_FN(_wcslwr), BIT_FN(_wcsupr), BIT_FN(_wcsrev), BIT_AS("wcscoll", wcscoll_),
          BIT_AS("wcsxfrm", wcsxfrm_),
          BIT_AS("wmemcpy", wmemcpy_), BIT_AS("wmemmove", wmemmove_), BIT_AS("wmemset", wmemset_),
          BIT_AS("wmemcmp", wmemcmp_), BIT_AS("wmemchr", wmemchr_),
          BIT_FN(wcscpy_s), BIT_FN(wcsncpy_s), BIT_FN(wcscat_s), BIT_FN(wcsncat_s),
          BIT_AS("wcsnlen_s", wcsnlen_),
          BIT_AS("wcstol", wcstol_), BIT_AS("wcstoul", wcstoul_), BIT_AS("wcstoll", wcstoll_),
          BIT_AS("_wcstoi64", wcstoll_), BIT_AS("wcstoull", wcstoull_), BIT_AS("_wcstoui64", wcstoull_),
          BIT_AS("wcstod", wcstod_), BIT_AS("wcstof", wcstof_),
          BIT_FN(_wtoi), BIT_AS("_wtol", _wtoi), BIT_FN(_wtoi64), BIT_AS("_wtoll", _wtoi64), BIT_FN(_wtof),
          BIT_FN(_itow), BIT_AS("_ltow", _itow), BIT_FN(_i64tow), BIT_FN(_ultow), BIT_FN(_ui64tow),
          BIT_FN(_itow_s),
          BIT_AS("towlower", towlower_), BIT_AS("towupper", towupper_),
          BIT_AS("iswalpha", iswalpha_), BIT_AS("iswdigit", iswdigit_), BIT_AS("iswspace", iswspace_),
          BIT_AS("iswupper", iswupper_), BIT_AS("iswlower", iswlower_), BIT_AS("iswalnum", iswalnum_),
          BIT_AS("iswpunct", iswpunct_), BIT_AS("iswxdigit", iswxdigit_), BIT_AS("iswprint", iswprint_),
          BIT_AS("iswcntrl", iswcntrl_), BIT_AS("iswgraph", iswgraph_), BIT_AS("iswctype", iswctype_),
          BIT_AS("is_wctype", iswctype_),
          BIT_AS("mbstowcs", mbstowcs_), BIT_AS("wcstombs", wcstombs_), BIT_AS("mbtowc", mbtowc_),
          BIT_AS("wctomb", wctomb_), BIT_AS("mblen", mblen_), BIT_AS("btowc", btowc_),
          BIT_AS("wctob", wctob_), BIT_AS("mbrtowc", mbrtowc_), BIT_AS("wcrtomb", wcrtomb_),
          BIT_AS("mbrlen", mbrlen_));
