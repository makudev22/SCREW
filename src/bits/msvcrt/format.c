#include "crt.h"

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>

#define OPT_LEGACY_NULL_TERMINATION 1u
#define OPT_STANDARD_SNPRINTF 2u
#define OPT_LEGACY_WIDE 4u

enum width { W_INT, W_CHAR, W_SHORT, W_LONG64, W_PTR };

struct sb {
    char *p;
    size_t len;
    size_t cap;
};

static void sb_put(struct sb *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

static void sb_wide(struct sb *b, const WCHAR *w, int prec)
{
    if (!w) {
        sb_put(b, "(null)", 6);
        return;
    }
    size_t n = wstr_len(w);
    if (prec >= 0 && (size_t)prec < n)
        n = prec;
    int len = utf16_to_utf8(w, n, NULL, 0);
    char *u = malloc(len + 1);
    utf16_to_utf8(w, n, u, len);
    sb_put(b, u, len);
    free(u);
}

static void pad(struct sb *b, const char *s, size_t n, int width, bool left)
{
    if (!left)
        for (int i = n; i < width; i++)
            sb_put(b, " ", 1);
    sb_put(b, s, n);
    if (left)
        for (int i = n; i < width; i++)
            sb_put(b, " ", 1);
}

static void host_print(struct sb *b, const char *spec, ...)
{
    char small[128];
    va_list ap;
    va_start(ap, spec);
    int n = vsnprintf(small, sizeof small, spec, ap);
    va_end(ap);
    if (n < (int)sizeof small) {
        sb_put(b, small, n);
        return;
    }
    char *big = malloc(n + 1);
    va_start(ap, spec);
    vsnprintf(big, n + 1, spec, ap);
    va_end(ap);
    sb_put(b, big, n);
    free(big);
}

static enum width length(const char **f)
{
    const char *p = *f;
    enum width w = W_INT;
    if (p[0] == 'h' && p[1] == 'h')
        w = W_CHAR, p += 2;
    else if (p[0] == 'h')
        w = W_SHORT, p++;
    else if (p[0] == 'l' && p[1] == 'l')
        w = W_LONG64, p += 2;
    else if (p[0] == 'I' && p[1] == '6' && p[2] == '4')
        w = W_LONG64, p += 3;
    else if (p[0] == 'I' && p[1] == '3' && p[2] == '2')
        p += 3;
    else if (p[0] == 'I' || p[0] == 'z' || p[0] == 't' || p[0] == 'j')
        w = W_PTR, p++;
    else if (p[0] == 'l' || p[0] == 'w' || p[0] == 'L')
        w = W_INT, p++;
    *f = p;
    return w;
}

static int format(struct sb *out, const char *fmt, ms_va_list *ap, bool wide_default)
{
    for (const char *f = fmt; *f;) {
        if (*f != '%') {
            const char *e = strchr(f, '%');
            size_t n = e ? (size_t)(e - f) : strlen(f);
            sb_put(out, f, n);
            f += n;
            continue;
        }
        const char *start = f++;
        if (*f == '%') {
            sb_put(out, "%", 1);
            f++;
            continue;
        }

        char flags[8];
        int nf = 0;
        while (strchr("-+ #0'", *f) && *f && nf < 6)
            flags[nf++] = *f++;
        flags[nf] = 0;
        bool left = strchr(flags, '-');

        int width = -1, prec = -1;
        if (*f == '*') {
            width = ms_va_arg(*ap, int);
            if (width < 0) {
                left = true;
                flags[nf++] = '-';
                flags[nf] = 0;
                width = -width;
            }
            f++;
        } else if (*f >= '0' && *f <= '9') {
            width = strtol(f, (char **)&f, 10);
        }
        if (*f == '.') {
            f++;
            if (*f == '*') {
                prec = ms_va_arg(*ap, int);
                f++;
            } else {
                prec = strtol(f, (char **)&f, 10);
            }
        }

        const char *lp = f;
        enum width w = length(&f);
        bool long_mod = lp[0] == 'l' || lp[0] == 'w';
        bool short_mod = lp[0] == 'h';
        char conv = *f ? *f++ : 0;

        char spec[48];
        int sl = snprintf(spec, sizeof spec, "%%%s", flags);
        if (width >= 0)
            sl += snprintf(spec + sl, sizeof spec - sl, "%d", width);
        if (prec >= 0)
            sl += snprintf(spec + sl, sizeof spec - sl, ".%d", prec);

        switch (conv) {
        case 'd':
        case 'i': {
            int64_t v = w == W_LONG64 || w == W_PTR ? ms_va_arg(*ap, int64_t) : ms_va_arg(*ap, int);
            if (w == W_CHAR)
                v = (signed char)v;
            else if (w == W_SHORT)
                v = (short)v;
            snprintf(spec + sl, sizeof spec - sl, "lld");
            host_print(out, spec, (long long)v);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            uint64_t v = w == W_LONG64 || w == W_PTR ? ms_va_arg(*ap, uint64_t) : ms_va_arg(*ap, unsigned);
            if (w == W_CHAR)
                v = (unsigned char)v;
            else if (w == W_SHORT)
                v = (unsigned short)v;
            snprintf(spec + sl, sizeof spec - sl, "ll%c", conv);
            host_print(out, spec, (unsigned long long)v);
            break;
        }
        case 'p': {
            void *v = ms_va_arg(*ap, void *);
            char buf[32];
            int n = snprintf(buf, sizeof buf, "%016llX", (unsigned long long)(uintptr_t)v);
            pad(out, buf, n, width, left);
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            double v = ms_va_arg(*ap, double);
            snprintf(spec + sl, sizeof spec - sl, "%c", conv);
            host_print(out, spec, v);
            break;
        }
        case 'c':
        case 'C': {
            unsigned v = ms_va_arg(*ap, unsigned);
            bool wide = conv == 'C' ? !wide_default : short_mod ? false : long_mod || wide_default;
            char buf[8];
            int n;
            if (wide) {
                WCHAR wc = v;
                n = utf16_to_utf8(&wc, 1, buf, sizeof buf);
            } else {
                buf[0] = v;
                n = 1;
            }
            pad(out, buf, n, width, left);
            break;
        }
        case 's':
        case 'S':
        case 'Z': {
            void *v = ms_va_arg(*ap, void *);
            bool wide = conv == 'S' ? !wide_default : short_mod ? false : long_mod || wide_default;
            struct sb tmp = { 0 };
            if (wide)
                sb_wide(&tmp, v, prec);
            else if (!v)
                sb_put(&tmp, "(null)", 6);
            else
                sb_put(&tmp, v, prec >= 0 ? strnlen(v, prec) : strlen(v));
            pad(out, tmp.p ? tmp.p : "", tmp.len, width, left);
            free(tmp.p);
            break;
        }
        case 'n': {
            int *v = ms_va_arg(*ap, int *);
            if (v)
                *v = out->len;
            break;
        }
        default:
            sb_put(out, start, f - start);
            break;
        }
    }
    if (!out->p)
        sb_put(out, "", 0);
    return out->len;
}

static int format_w(struct sb *out, const WCHAR *fmt, ms_va_list *ap, bool wide_default)
{
    char *f = wide_to_host(fmt);
    int r = format(out, f, ap, wide_default);
    free(f);
    return r;
}

static int to_file(struct wfile *f, struct sb *b)
{
    int r = fwrite(b->p, 1, b->len, crt_host(f)) == b->len ? (int)b->len : -1;
    free(b->p);
    return r;
}

static int to_file_w(struct wfile *f, struct sb *b)
{
    int chars = utf8_to_utf16(b->p, b->len, NULL, 0);
    return to_file(f, b) < 0 ? -1 : chars;
}

static int to_buf(char *buf, size_t n, struct sb *b, unsigned opts)
{
    int len = b->len;
    if (buf && n) {
        size_t copy = (size_t)len < n ? (size_t)len : n - 1;
        memcpy(buf, b->p, copy);
        if ((size_t)len < n || (opts & OPT_STANDARD_SNPRINTF) || !(opts & OPT_LEGACY_NULL_TERMINATION))
            buf[copy] = 0;
        if ((size_t)len >= n && !(opts & OPT_STANDARD_SNPRINTF))
            len = -1;
    }
    free(b->p);
    return len;
}

static int to_wbuf(WCHAR *buf, size_t n, struct sb *b, unsigned opts)
{
    int need = utf8_to_utf16(b->p, b->len, NULL, 0);
    if (buf && n) {
        WCHAR *w = malloc((need + 1) * sizeof *w);
        utf8_to_utf16(b->p, b->len, w, need);
        size_t copy = (size_t)need < n ? (size_t)need : n - 1;
        memcpy(buf, w, copy * sizeof *w);
        buf[copy] = 0;
        free(w);
        if ((size_t)need >= n && !(opts & OPT_STANDARD_SNPRINTF))
            need = -1;
    }
    free(b->p);
    return need;
}

#define FMT(call)                                 \
    do {                                          \
        ms_va_list ap;                            \
        ms_va_start(ap, fmt);                     \
        struct sb b = { 0 };                      \
        format(&b, fmt, &ap, false);              \
        ms_va_end(ap);                            \
        return call;                              \
    } while (0)

#define WFMT(call)                                \
    do {                                          \
        ms_va_list ap;                            \
        ms_va_start(ap, fmt);                     \
        struct sb b = { 0 };                      \
        format_w(&b, fmt, &ap, true);             \
        ms_va_end(ap);                            \
        return call;                              \
    } while (0)

static int WINAPI printf_(const char *fmt, ...) { FMT(to_file(crt_stream(1), &b)); }
static int WINAPI fprintf_(struct wfile *f, const char *fmt, ...) { FMT(to_file(f, &b)); }
static int WINAPI sprintf_(char *buf, const char *fmt, ...) { FMT(to_buf(buf, (size_t)INT_MAX, &b, 0)); }
static int WINAPI snprintf_(char *buf, size_t n, const char *fmt, ...) { FMT(to_buf(buf, n, &b, OPT_STANDARD_SNPRINTF)); }
static int WINAPI _snprintf(char *buf, size_t n, const char *fmt, ...) { FMT(to_buf(buf, n, &b, OPT_LEGACY_NULL_TERMINATION)); }
static int WINAPI sprintf_s(char *buf, size_t n, const char *fmt, ...) { FMT(to_buf(buf, n, &b, 0)); }
static int WINAPI _snprintf_s(char *buf, size_t n, size_t max, const char *fmt, ...)
{
    FMT(to_buf(buf, max < n ? max + 1 : n, &b, 0));
}
static int WINAPI _scprintf(const char *fmt, ...) { FMT(to_buf(NULL, 0, &b, 0)); }

static int WINAPI wprintf_(const WCHAR *fmt, ...) { WFMT(to_file_w(crt_stream(1), &b)); }
static int WINAPI fwprintf_(struct wfile *f, const WCHAR *fmt, ...) { WFMT(to_file_w(f, &b)); }
static int WINAPI swprintf_(WCHAR *buf, const WCHAR *fmt, ...) { WFMT(to_wbuf(buf, (size_t)INT_MAX, &b, 0)); }
static int WINAPI _snwprintf(WCHAR *buf, size_t n, const WCHAR *fmt, ...) { WFMT(to_wbuf(buf, n, &b, 0)); }
static int WINAPI swprintf_s(WCHAR *buf, size_t n, const WCHAR *fmt, ...) { WFMT(to_wbuf(buf, n, &b, 0)); }
static int WINAPI _scwprintf(const WCHAR *fmt, ...) { WFMT(to_wbuf(NULL, 0, &b, 0)); }

#define VFMT(call)                                \
    do {                                          \
        ms_va_list ap = args;                     \
        struct sb b = { 0 };                      \
        format(&b, fmt, &ap, false);              \
        return call;                              \
    } while (0)

#define VWFMT(call)                               \
    do {                                          \
        ms_va_list ap = args;                     \
        struct sb b = { 0 };                      \
        format_w(&b, fmt, &ap, true);             \
        return call;                              \
    } while (0)

static int WINAPI vprintf_(const char *fmt, ms_va_list args) { VFMT(to_file(crt_stream(1), &b)); }
static int WINAPI vfprintf_(struct wfile *f, const char *fmt, ms_va_list args) { VFMT(to_file(f, &b)); }
static int WINAPI vsprintf_(char *buf, const char *fmt, ms_va_list args) { VFMT(to_buf(buf, (size_t)INT_MAX, &b, 0)); }
static int WINAPI vsnprintf_(char *buf, size_t n, const char *fmt, ms_va_list args) { VFMT(to_buf(buf, n, &b, OPT_STANDARD_SNPRINTF)); }
static int WINAPI _vsnprintf(char *buf, size_t n, const char *fmt, ms_va_list args) { VFMT(to_buf(buf, n, &b, OPT_LEGACY_NULL_TERMINATION)); }
static int WINAPI vsprintf_s(char *buf, size_t n, const char *fmt, ms_va_list args) { VFMT(to_buf(buf, n, &b, 0)); }
static int WINAPI _vsnprintf_s(char *buf, size_t n, size_t max, const char *fmt, ms_va_list args)
{
    VFMT(to_buf(buf, max < n ? max + 1 : n, &b, 0));
}
static int WINAPI _vscprintf(const char *fmt, ms_va_list args) { VFMT(to_buf(NULL, 0, &b, 0)); }

static int WINAPI vwprintf_(const WCHAR *fmt, ms_va_list args) { VWFMT(to_file_w(crt_stream(1), &b)); }
static int WINAPI vfwprintf_(struct wfile *f, const WCHAR *fmt, ms_va_list args) { VWFMT(to_file_w(f, &b)); }
static int WINAPI vswprintf_(WCHAR *buf, const WCHAR *fmt, ms_va_list args) { VWFMT(to_wbuf(buf, (size_t)INT_MAX, &b, 0)); }
static int WINAPI _vsnwprintf(WCHAR *buf, size_t n, const WCHAR *fmt, ms_va_list args) { VWFMT(to_wbuf(buf, n, &b, 0)); }
static int WINAPI _vscwprintf(const WCHAR *fmt, ms_va_list args) { VWFMT(to_wbuf(NULL, 0, &b, 0)); }

static int WINAPI __stdio_common_vfprintf(uint64_t opts, struct wfile *f, const char *fmt, void *loc, ms_va_list args)
{
    VFMT(to_file(f, &b));
}

static int WINAPI __stdio_common_vsprintf(uint64_t opts, char *buf, size_t n, const char *fmt, void *loc, ms_va_list args)
{
    VFMT(to_buf(buf, n, &b, opts));
}

static int WINAPI __stdio_common_vsprintf_s(uint64_t opts, char *buf, size_t n, const char *fmt, void *loc, ms_va_list args)
{
    VFMT(to_buf(buf, n, &b, 0));
}

static int WINAPI __stdio_common_vsnprintf_s(uint64_t opts, char *buf, size_t n, size_t max, const char *fmt,
                                             void *loc, ms_va_list args)
{
    VFMT(to_buf(buf, max < n ? max + 1 : n, &b, 0));
}

static int WINAPI __stdio_common_vfwprintf(uint64_t opts, struct wfile *f, const WCHAR *fmt, void *loc, ms_va_list args)
{
    ms_va_list ap = args;
    struct sb b = { 0 };
    format_w(&b, fmt, &ap, opts & OPT_LEGACY_WIDE);
    return to_file_w(f, &b);
}

static int WINAPI __stdio_common_vswprintf(uint64_t opts, WCHAR *buf, size_t n, const WCHAR *fmt, void *loc,
                                           ms_va_list args)
{
    ms_va_list ap = args;
    struct sb b = { 0 };
    format_w(&b, fmt, &ap, opts & OPT_LEGACY_WIDE);
    return to_wbuf(buf, n, &b, opts);
}

BIT_TABLE(crt_format,
          BIT_AS("printf", printf_), BIT_AS("fprintf", fprintf_), BIT_AS("sprintf", sprintf_),
          BIT_AS("snprintf", snprintf_), BIT_FN(_snprintf), BIT_FN(sprintf_s), BIT_FN(_snprintf_s),
          BIT_FN(_scprintf), BIT_AS("printf_s", printf_), BIT_AS("fprintf_s", fprintf_),
          BIT_AS("wprintf", wprintf_), BIT_AS("fwprintf", fwprintf_), BIT_AS("swprintf", swprintf_),
          BIT_AS("_swprintf", swprintf_), BIT_FN(_snwprintf), BIT_FN(swprintf_s), BIT_FN(_scwprintf),
          BIT_AS("vprintf", vprintf_), BIT_AS("vfprintf", vfprintf_), BIT_AS("vsprintf", vsprintf_),
          BIT_AS("vsnprintf", vsnprintf_), BIT_FN(_vsnprintf), BIT_FN(vsprintf_s), BIT_FN(_vsnprintf_s),
          BIT_FN(_vscprintf), BIT_AS("vprintf_s", vprintf_), BIT_AS("vfprintf_s", vfprintf_),
          BIT_AS("vwprintf", vwprintf_), BIT_AS("vfwprintf", vfwprintf_), BIT_AS("vswprintf", vswprintf_),
          BIT_AS("_vswprintf", vswprintf_), BIT_FN(_vsnwprintf), BIT_FN(_vscwprintf),
          BIT_FN(__stdio_common_vfprintf), BIT_FN(__stdio_common_vsprintf),
          BIT_FN(__stdio_common_vsprintf_s), BIT_FN(__stdio_common_vsnprintf_s),
          BIT_FN(__stdio_common_vfwprintf), BIT_FN(__stdio_common_vswprintf));
