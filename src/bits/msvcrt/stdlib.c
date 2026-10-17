#include "crt.h"

#include <screw/path.h>

#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <unistd.h>

typedef int WINAPI (*compare_fn)(const void *, const void *);
typedef int WINAPI (*compare_ctx_fn)(void *, const void *, const void *);

typedef struct {
    int quot;
    int rem;
} div_t32;

typedef struct {
    int64_t quot;
    int64_t rem;
} div_t64;

static __thread uint32_t seed = 1;

static void *WINAPI malloc_(size_t n) { return malloc(n ? n : 1); }
static void *WINAPI calloc_(size_t n, size_t s) { return calloc(n ? n : 1, s ? s : 1); }
static void *WINAPI realloc_(void *p, size_t n) { return realloc(p, n ? n : 1); }
static void WINAPI free_(void *p) { free(p); }
static size_t WINAPI _msize(void *p) { return malloc_usable_size(p); }

static void *WINAPI _recalloc(void *p, size_t n, size_t s)
{
    size_t old = p ? malloc_usable_size(p) : 0, size = n * s;
    void *r = realloc(p, size ? size : 1);
    if (r && size > old)
        memset((char *)r + old, 0, size - old);
    return r;
}

static void *WINAPI _aligned_malloc(size_t n, size_t align)
{
    void *p = NULL;
    if (align < sizeof(void *))
        align = sizeof(void *);
    return posix_memalign(&p, align, n ? n : 1) ? NULL : p;
}

static void WINAPI _aligned_free(void *p) { free(p); }

static void *WINAPI _aligned_realloc(void *p, size_t n, size_t align)
{
    if (!p)
        return _aligned_malloc(n, align);
    void *r = _aligned_malloc(n, align);
    if (r) {
        size_t old = malloc_usable_size(p);
        memcpy(r, p, old < n ? old : n);
        free(p);
    }
    return r;
}

static size_t WINAPI _aligned_msize(void *p, size_t align, size_t offset) { return malloc_usable_size(p); }

static LONG clamp32(long v)
{
    if (v > INT32_MAX || v < INT32_MIN) {
        crt_errno_set(ERANGE);
        return v > 0 ? INT32_MAX : INT32_MIN;
    }
    return v;
}

static LONG WINAPI strtol_(const char *s, char **end, int base)
{
    errno = 0;
    long v = strtol(s, end, base);
    if (errno)
        crt_errno_set(errno);
    return clamp32(v);
}

static ULONG WINAPI strtoul_(const char *s, char **end, int base)
{
    errno = 0;
    unsigned long v = strtoul(s, end, base);
    if (v > UINT32_MAX && v < 0xFFFFFFFF00000001ul) {
        crt_errno_set(ERANGE);
        return UINT32_MAX;
    }
    return (ULONG)v;
}

static int64_t WINAPI strtoll_(const char *s, char **end, int base) { return strtoll(s, end, base); }
static uint64_t WINAPI strtoull_(const char *s, char **end, int base) { return strtoull(s, end, base); }
static double WINAPI strtod_(const char *s, char **end) { return strtod(s, end); }
static float WINAPI strtof_(const char *s, char **end) { return strtof(s, end); }
static int WINAPI atoi_(const char *s) { return (int)strtol(s, NULL, 10); }
static int64_t WINAPI atoll_(const char *s) { return strtoll(s, NULL, 10); }
static double WINAPI atof_(const char *s) { return strtod(s, NULL); }

static int WINAPI abs_(int v) { return v < 0 ? -v : v; }
static int64_t WINAPI llabs_(int64_t v) { return v < 0 ? -v : v; }
static div_t32 WINAPI div_(int a, int b) { return (div_t32){ a / b, a % b }; }
static div_t64 WINAPI lldiv_(int64_t a, int64_t b) { return (div_t64){ a / b, a % b }; }

static int sort_thunk(const void *a, const void *b, void *fn) { return ((compare_fn)fn)(a, b); }

struct ctx_sort {
    compare_ctx_fn fn;
    void *ctx;
};

static int sort_ctx_thunk(const void *a, const void *b, void *p)
{
    struct ctx_sort *c = p;
    return c->fn(c->ctx, a, b);
}

static void WINAPI qsort_(void *base, size_t n, size_t size, compare_fn cmp) { qsort_r(base, n, size, sort_thunk, cmp); }

static void WINAPI qsort_s(void *base, size_t n, size_t size, compare_ctx_fn cmp, void *ctx)
{
    struct ctx_sort c = { cmp, ctx };
    qsort_r(base, n, size, sort_ctx_thunk, &c);
}

static void *WINAPI bsearch_(const void *key, const void *base, size_t n, size_t size, compare_fn cmp)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        const char *p = (const char *)base + mid * size;
        int c = cmp(key, p);
        if (!c)
            return (void *)p;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

static void *WINAPI _lfind(const void *key, const void *base, unsigned *n, unsigned size, compare_fn cmp)
{
    for (unsigned i = 0; i < *n; i++)
        if (!cmp(key, (const char *)base + i * size))
            return (char *)base + i * size;
    return NULL;
}

static void WINAPI srand_(unsigned s) { seed = s; }

static int WINAPI rand_(void)
{
    seed = seed * 214013u + 2531011u;
    return (seed >> 16) & 0x7FFF;
}

static int WINAPI rand_s(unsigned *out) { return getrandom(out, sizeof *out, 0) == sizeof *out ? 0 : EINVAL; }

static char *WINAPI getenv_(const char *name) { return (char *)env_get(name); }

static WCHAR *WINAPI _wgetenv(const WCHAR *name)
{
    static __thread WCHAR *last;
    char *n = wide_to_host(name);
    const char *v = env_get(n);
    free(n);
    free(last);
    last = v ? host_to_wide(v) : NULL;
    return last;
}

static int WINAPI getenv_s(size_t *len, char *buf, size_t size, const char *name)
{
    const char *v = env_get(name);
    size_t n = v ? strlen(v) + 1 : 0;
    if (len)
        *len = n;
    if (!v) {
        if (buf && size)
            buf[0] = 0;
        return 0;
    }
    if (buf && size >= n)
        memcpy(buf, v, n);
    return buf && size < n ? ERANGE : 0;
}

static int WINAPI _dupenv_s(char **out, size_t *len, const char *name)
{
    const char *v = env_get(name);
    *out = v ? strdup(v) : NULL;
    if (len)
        *len = v ? strlen(v) + 1 : 0;
    return 0;
}

static int put_env(const char *s)
{
    const char *eq = strchr(s, '=');
    if (!eq || eq == s)
        return -1;
    char *name = strndup(s, eq - s);
    env_set(name, eq[1] ? eq + 1 : NULL);
    free(name);
    return 0;
}

static int WINAPI _putenv(const char *s) { return put_env(s); }

static int WINAPI _wputenv(const WCHAR *s)
{
    char *u = wide_to_host(s);
    int r = put_env(u);
    free(u);
    return r;
}

static int WINAPI _putenv_s(const char *name, const char *value)
{
    env_set(name, value && *value ? value : NULL);
    return 0;
}

static int run_command(const char *cmd)
{
    if (!cmd)
        return 1;
    while (*cmd == ' ' || *cmd == '@')
        cmd++;
    fflush(NULL);
    if (!strcasecmp(cmd, "cls")) {
        fputs("\033[2J\033[H", stdout);
        fflush(stdout);
        return 0;
    }
    if (!strcasecmp(cmd, "pause")) {
        fputs("Press any key to continue . . . ", stdout);
        fflush(stdout);
        int c = getchar();
        (void)c;
        putchar('\n');
        return 0;
    }
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int WINAPI system_(const char *cmd) { return run_command(cmd); }

static int WINAPI _wsystem(const WCHAR *cmd)
{
    char *u = wide_to_host(cmd);
    int r = run_command(u);
    free(u);
    return r;
}

static char *to_num(uint64_t v, bool neg, char *out, int radix)
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
    return strcpy(out, p);
}

static char *WINAPI _itoa(int v, char *out, int radix)
{
    bool neg = radix == 10 && v < 0;
    return to_num(neg ? -(int64_t)v : (uint32_t)v, neg, out, radix);
}

static char *WINAPI _i64toa(int64_t v, char *out, int radix)
{
    bool neg = radix == 10 && v < 0;
    return to_num(neg ? -(uint64_t)v : (uint64_t)v, neg, out, radix);
}

static char *WINAPI _ultoa(ULONG v, char *out, int radix) { return to_num(v, false, out, radix); }
static char *WINAPI _ui64toa(uint64_t v, char *out, int radix) { return to_num(v, false, out, radix); }

static int WINAPI _itoa_s(int v, char *out, size_t size, int radix)
{
    char tmp[72];
    _itoa(v, tmp, radix);
    if (strlen(tmp) >= size)
        return ERANGE;
    strcpy(out, tmp);
    return 0;
}

static int WINAPI _i64toa_s(int64_t v, char *out, size_t size, int radix)
{
    char tmp[72];
    _i64toa(v, tmp, radix);
    if (strlen(tmp) >= size)
        return ERANGE;
    strcpy(out, tmp);
    return 0;
}

static char *WINAPI _fullpath(char *out, const char *path, size_t size)
{
    char *host = path_to_host(path);
    char abs[PATH_MAX];
    if (!host)
        return NULL;
    if (host[0] == '/' || !getcwd(abs, sizeof abs))
        snprintf(abs, sizeof abs, "%s", host);
    else
        snprintf(abs + strlen(abs), sizeof abs - strlen(abs), "/%s", host);
    free(host);
    char *win = path_to_windows(abs);
    if (!out) {
        return win;
    }
    if (strlen(win) >= size) {
        free(win);
        return NULL;
    }
    strcpy(out, win);
    free(win);
    return out;
}

static void WINAPI _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext)
{
    const char *p = path;
    if (drive) {
        drive[0] = 0;
        if (p[0] && p[1] == ':') {
            drive[0] = p[0];
            drive[1] = ':';
            drive[2] = 0;
        }
    }
    if (p[0] && p[1] == ':')
        p += 2;
    const char *slash = NULL;
    for (const char *q = p; *q; q++)
        if (*q == '\\' || *q == '/')
            slash = q;
    const char *base = slash ? slash + 1 : p;
    if (dir) {
        size_t n = slash ? (size_t)(slash - p + 1) : 0;
        memcpy(dir, p, n);
        dir[n] = 0;
    }
    const char *dot = strrchr(base, '.');
    if (fname) {
        size_t n = dot ? (size_t)(dot - base) : strlen(base);
        memcpy(fname, base, n);
        fname[n] = 0;
    }
    if (ext)
        strcpy(ext, dot ? dot : "");
}

static void WINAPI _makepath(char *out, const char *drive, const char *dir, const char *fname, const char *ext)
{
    out[0] = 0;
    if (drive && *drive) {
        strncat(out, drive, 1);
        strcat(out, ":");
    }
    if (dir && *dir) {
        strcat(out, dir);
        char last = dir[strlen(dir) - 1];
        if (last != '\\' && last != '/')
            strcat(out, "\\");
    }
    if (fname)
        strcat(out, fname);
    if (ext && *ext) {
        if (*ext != '.')
            strcat(out, ".");
        strcat(out, ext);
    }
}

static uint16_t WINAPI _byteswap_ushort(uint16_t v) { return __builtin_bswap16(v); }
static uint32_t WINAPI _byteswap_ulong(uint32_t v) { return __builtin_bswap32(v); }
static uint64_t WINAPI _byteswap_uint64(uint64_t v) { return __builtin_bswap64(v); }
static unsigned WINAPI _rotl(unsigned v, int s) { s &= 31; return (v << s) | (v >> ((32 - s) & 31)); }
static unsigned WINAPI _rotr(unsigned v, int s) { s &= 31; return (v >> s) | (v << ((32 - s) & 31)); }
static uint64_t WINAPI _rotl64(uint64_t v, int s) { s &= 63; return (v << s) | (v >> ((64 - s) & 63)); }
static uint64_t WINAPI _rotr64(uint64_t v, int s) { s &= 63; return (v >> s) | (v << ((64 - s) & 63)); }

BIT_TABLE(crt_stdlib,
          BIT_AS("malloc", malloc_), BIT_AS("calloc", calloc_), BIT_AS("realloc", realloc_),
          BIT_AS("free", free_), BIT_FN(_msize), BIT_FN(_recalloc),
          BIT_FN(_aligned_malloc), BIT_FN(_aligned_free), BIT_FN(_aligned_realloc), BIT_FN(_aligned_msize),
          BIT_AS("strtol", strtol_), BIT_AS("strtoul", strtoul_), BIT_AS("strtoll", strtoll_),
          BIT_AS("_strtoi64", strtoll_), BIT_AS("strtoull", strtoull_), BIT_AS("_strtoui64", strtoull_),
          BIT_AS("strtod", strtod_), BIT_AS("strtof", strtof_), BIT_AS("strtold", strtod_),
          BIT_AS("atoi", atoi_), BIT_AS("atol", atoi_), BIT_AS("atoll", atoll_), BIT_AS("_atoi64", atoll_),
          BIT_AS("atof", atof_),
          BIT_AS("abs", abs_), BIT_AS("labs", abs_), BIT_AS("llabs", llabs_), BIT_AS("_abs64", llabs_),
          BIT_AS("div", div_), BIT_AS("ldiv", div_), BIT_AS("lldiv", lldiv_),
          BIT_AS("qsort", qsort_), BIT_FN(qsort_s), BIT_AS("bsearch", bsearch_), BIT_FN(_lfind),
          BIT_AS("srand", srand_), BIT_AS("rand", rand_), BIT_FN(rand_s),
          BIT_AS("getenv", getenv_), BIT_FN(_wgetenv), BIT_FN(getenv_s), BIT_FN(_dupenv_s),
          BIT_FN(_putenv), BIT_FN(_wputenv), BIT_FN(_putenv_s),
          BIT_AS("system", system_), BIT_FN(_wsystem),
          BIT_FN(_itoa), BIT_AS("_ltoa", _itoa), BIT_FN(_ultoa), BIT_FN(_i64toa), BIT_FN(_ui64toa),
          BIT_FN(_itoa_s), BIT_AS("_ltoa_s", _itoa_s), BIT_FN(_i64toa_s),
          BIT_FN(_fullpath), BIT_FN(_splitpath), BIT_FN(_makepath),
          BIT_FN(_byteswap_ushort), BIT_FN(_byteswap_ulong), BIT_FN(_byteswap_uint64),
          BIT_FN(_rotl), BIT_FN(_rotr), BIT_AS("_lrotl", _rotl), BIT_AS("_lrotr", _rotr),
          BIT_FN(_rotl64), BIT_FN(_rotr64));
