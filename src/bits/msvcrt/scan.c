#include "crt.h"

#define MAX_ARGS 32
#define WIDE_BUF 4096

struct fixup {
    int slot;
    WCHAR *dst;
    bool single;
};

struct plan {
    char fmt[1024];
    void *args[MAX_ARGS];
    int count;
    struct fixup fix[MAX_ARGS];
    int fixes;
    char *wide[MAX_ARGS];
};

static void emit(struct plan *p, size_t *o, const char *s, size_t n)
{
    if (*o + n < sizeof p->fmt) {
        memcpy(p->fmt + *o, s, n);
        *o += n;
    }
}

static void build(struct plan *p, const char *fmt, ms_va_list *ap, bool wide_default, bool secure)
{
    size_t o = 0;
    memset(p, 0, sizeof *p);
    for (const char *f = fmt; *f;) {
        if (*f != '%') {
            emit(p, &o, f++, 1);
            continue;
        }
        emit(p, &o, f++, 1);
        if (*f == '%') {
            emit(p, &o, f++, 1);
            continue;
        }
        bool skip = *f == '*';
        if (skip)
            emit(p, &o, f++, 1);
        while (*f >= '0' && *f <= '9')
            emit(p, &o, f++, 1);

        bool longm = false, shortm = false, wide = false;
        const char *len = "";
        if (f[0] == 'I' && f[1] == '6' && f[2] == '4')
            len = "ll", f += 3;
        else if (f[0] == 'I' && f[1] == '3' && f[2] == '2')
            f += 3;
        else if (f[0] == 'l' && f[1] == 'l')
            len = "ll", f += 2;
        else if (f[0] == 'h' && f[1] == 'h')
            len = "hh", f += 2;
        else if (f[0] == 'h')
            shortm = true, f++;
        else if (f[0] == 'l' || f[0] == 'w')
            longm = true, f++;
        else if (f[0] == 'L')
            longm = true, f++;
        else if (f[0] == 'I' || f[0] == 'z' || f[0] == 'j' || f[0] == 't')
            len = "ll", f++;

        char c = *f;
        if (strchr("eEfgGaA", c) && longm)
            len = "l";
        else if (strchr("diouxXn", c) && shortm)
            len = "h";
        else if (strchr("sc[", c) || c == 'S' || c == 'C')
            wide = c == 'S' || c == 'C' ? !wide_default : shortm ? false : longm || wide_default;

        emit(p, &o, len, strlen(len));
        if (c == '[') {
            const char *e = f + 1;
            if (*e == '^')
                e++;
            if (*e == ']')
                e++;
            while (*e && *e != ']')
                e++;
            emit(p, &o, f, e - f + (*e ? 1 : 0));
            f = *e ? e + 1 : e;
        } else if (c) {
            char hc = c == 'S' ? 's' : c == 'C' ? 'c' : c;
            emit(p, &o, &hc, 1);
            f++;
        }
        if (skip || !c || p->count == MAX_ARGS)
            continue;

        void *arg = ms_va_arg(*ap, void *);
        if (secure && strchr("sScC[", c))
            (void)ms_va_arg(*ap, unsigned);
        if (wide && (c == 's' || c == 'S' || c == '[' || c == 'c' || c == 'C')) {
            p->wide[p->count] = calloc(1, WIDE_BUF);
            p->fix[p->fixes++] = (struct fixup){ p->count, arg, c == 'c' || c == 'C' };
            arg = p->wide[p->count];
        }
        p->args[p->count++] = arg;
    }
    p->fmt[o] = 0;
}

static int finish(struct plan *p, int r)
{
    for (int i = 0; i < p->fixes; i++) {
        struct fixup *x = &p->fix[i];
        if (x->slot < r || r < 0) {
            if (x->single)
                utf8_to_utf16(p->wide[x->slot], 1, x->dst, 1);
            else
                utf8_to_utf16(p->wide[x->slot], -1, x->dst, WIDE_BUF);
        }
    }
    for (int i = 0; i < MAX_ARGS; i++)
        free(p->wide[i]);
    return r;
}

#define A(p) p.args[0], p.args[1], p.args[2], p.args[3], p.args[4], p.args[5], p.args[6], p.args[7],         \
             p.args[8], p.args[9], p.args[10], p.args[11], p.args[12], p.args[13], p.args[14], p.args[15], \
             p.args[16], p.args[17], p.args[18], p.args[19], p.args[20], p.args[21], p.args[22],           \
             p.args[23], p.args[24], p.args[25], p.args[26], p.args[27], p.args[28], p.args[29],           \
             p.args[30], p.args[31]

static int scan_str(const char *src, const char *fmt, ms_va_list *ap, bool wide_default, bool secure)
{
    struct plan p;
    build(&p, fmt, ap, wide_default, secure);
    return finish(&p, sscanf(src, p.fmt, A(p)));
}

static int scan_file(struct wfile *f, const char *fmt, ms_va_list *ap, bool wide_default, bool secure)
{
    struct plan p;
    build(&p, fmt, ap, wide_default, secure);
    return finish(&p, fscanf(crt_host(f), p.fmt, A(p)));
}

static int scan_wstr(const WCHAR *src, const WCHAR *fmt, ms_va_list *ap)
{
    char *s = wide_to_host(src), *f = wide_to_host(fmt);
    int r = scan_str(s, f, ap, true, false);
    free(s);
    free(f);
    return r;
}

static int WINAPI sscanf_(const char *src, const char *fmt, ...)
{
    ms_va_list ap;
    ms_va_start(ap, fmt);
    int r = scan_str(src, fmt, &ap, false, false);
    ms_va_end(ap);
    return r;
}

static int WINAPI scanf_(const char *fmt, ...)
{
    ms_va_list ap;
    ms_va_start(ap, fmt);
    fflush(stdout);
    int r = scan_file(crt_stream(0), fmt, &ap, false, false);
    ms_va_end(ap);
    return r;
}

static int WINAPI fscanf_(struct wfile *f, const char *fmt, ...)
{
    ms_va_list ap;
    ms_va_start(ap, fmt);
    int r = scan_file(f, fmt, &ap, false, false);
    ms_va_end(ap);
    return r;
}

static int WINAPI sscanf_s_(const char *src, const char *fmt, ...)
{
    ms_va_list ap;
    ms_va_start(ap, fmt);
    int r = scan_str(src, fmt, &ap, false, true);
    ms_va_end(ap);
    return r;
}

static int WINAPI scanf_s_(const char *fmt, ...)
{
    ms_va_list ap;
    ms_va_start(ap, fmt);
    fflush(stdout);
    int r = scan_file(crt_stream(0), fmt, &ap, false, true);
    ms_va_end(ap);
    return r;
}

static int WINAPI fscanf_s_(struct wfile *f, const char *fmt, ...)
{
    ms_va_list ap;
    ms_va_start(ap, fmt);
    int r = scan_file(f, fmt, &ap, false, true);
    ms_va_end(ap);
    return r;
}

static int WINAPI swscanf_(const WCHAR *src, const WCHAR *fmt, ...)
{
    ms_va_list ap;
    ms_va_start(ap, fmt);
    int r = scan_wstr(src, fmt, &ap);
    ms_va_end(ap);
    return r;
}

static int WINAPI vsscanf_(const char *src, const char *fmt, ms_va_list args)
{
    ms_va_list ap = args;
    return scan_str(src, fmt, &ap, false, false);
}

static int WINAPI vfscanf_(struct wfile *f, const char *fmt, ms_va_list args)
{
    ms_va_list ap = args;
    return scan_file(f, fmt, &ap, false, false);
}

static int WINAPI vscanf_(const char *fmt, ms_va_list args)
{
    ms_va_list ap = args;
    fflush(stdout);
    return scan_file(crt_stream(0), fmt, &ap, false, false);
}

static int WINAPI __stdio_common_vsscanf(uint64_t opts, const char *src, size_t n, const char *fmt, void *loc,
                                         ms_va_list args)
{
    ms_va_list ap = args;
    if (n == (size_t)-1)
        return scan_str(src, fmt, &ap, false, opts & 1);
    char *copy = strndup(src, n);
    int r = scan_str(copy, fmt, &ap, false, opts & 1);
    free(copy);
    return r;
}

static int WINAPI __stdio_common_vfscanf(uint64_t opts, struct wfile *f, const char *fmt, void *loc,
                                         ms_va_list args)
{
    ms_va_list ap = args;
    if (f == crt_stream(0))
        fflush(stdout);
    return scan_file(f, fmt, &ap, false, opts & 1);
}

static int WINAPI __stdio_common_vswscanf(uint64_t opts, const WCHAR *src, size_t n, const WCHAR *fmt, void *loc,
                                          ms_va_list args)
{
    ms_va_list ap = args;
    return scan_wstr(src, fmt, &ap);
}

BIT_TABLE(crt_scan,
          BIT_AS("sscanf", sscanf_), BIT_AS("scanf", scanf_), BIT_AS("fscanf", fscanf_),
          BIT_AS("sscanf_s", sscanf_s_), BIT_AS("scanf_s", scanf_s_), BIT_AS("fscanf_s", fscanf_s_),
          BIT_AS("swscanf", swscanf_), BIT_AS("vsscanf", vsscanf_), BIT_AS("vfscanf", vfscanf_),
          BIT_AS("vscanf", vscanf_),
          BIT_FN(__stdio_common_vsscanf), BIT_FN(__stdio_common_vfscanf), BIT_FN(__stdio_common_vswscanf));
