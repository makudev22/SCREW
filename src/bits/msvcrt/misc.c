#include "crt.h"

#include <screw/thread.h>

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>

#define W_SIGINT 2
#define W_SIGILL 4
#define W_SIGFPE 8
#define W_SIGSEGV 11
#define W_SIGTERM 15
#define W_SIGABRT 22

typedef void WINAPI (*sig_fn)(int);
typedef unsigned WINAPI (*thread_fn)(void *);
typedef void WINAPI (*thread_fn_void)(void *);

static __thread int errno_;
static __thread unsigned long doserrno_;
static sig_fn handlers[32];
static pthread_mutex_t locks[64];
static pthread_once_t locks_once = PTHREAD_ONCE_INIT;
static int mb_cur_max = 1;
static unsigned short ctype_table[257];
static const unsigned short *ctype_ptr = ctype_table + 1;

static const int errno_map[][2] = {
    { EPERM, 1 }, { ENOENT, 2 }, { ESRCH, 3 }, { EINTR, 4 }, { EIO, 5 }, { ENXIO, 6 },
    { E2BIG, 7 }, { ENOEXEC, 8 }, { EBADF, 9 }, { ECHILD, 10 }, { EAGAIN, 11 }, { ENOMEM, 12 },
    { EACCES, 13 }, { EFAULT, 14 }, { EBUSY, 16 }, { EEXIST, 17 }, { EXDEV, 18 }, { ENODEV, 19 },
    { ENOTDIR, 20 }, { EISDIR, 21 }, { EINVAL, 22 }, { ENFILE, 23 }, { EMFILE, 24 }, { ENOTTY, 25 },
    { EFBIG, 27 }, { ENOSPC, 28 }, { ESPIPE, 29 }, { EROFS, 30 }, { EMLINK, 31 }, { EPIPE, 32 },
    { EDOM, 33 }, { ERANGE, 34 }, { EDEADLK, 36 }, { ENAMETOOLONG, 38 }, { ENOLCK, 39 },
    { ENOSYS, 40 }, { ENOTEMPTY, 41 }, { EILSEQ, 42 },
};

void crt_errno_set(int e)
{
    for (size_t i = 0; i < sizeof errno_map / sizeof errno_map[0]; i++)
        if (errno_map[i][0] == e) {
            errno_ = errno_map[i][1];
            return;
        }
    errno_ = e;
}

int *crt_errno(void) { return &errno_; }

static int *WINAPI _errno(void) { return &errno_; }
static unsigned long *WINAPI __doserrno(void) { return &doserrno_; }

static int WINAPI _get_errno(int *out)
{
    *out = errno_;
    return 0;
}

static int WINAPI _set_errno(int v)
{
    errno_ = v;
    return 0;
}

static int host_signal(int w)
{
    switch (w) {
    case W_SIGINT: return SIGINT;
    case W_SIGILL: return SIGILL;
    case W_SIGFPE: return SIGFPE;
    case W_SIGSEGV: return SIGSEGV;
    case W_SIGTERM: return SIGTERM;
    case W_SIGABRT: return SIGABRT;
    default: return -1;
    }
}

static int win_signal(int h)
{
    switch (h) {
    case SIGINT: return W_SIGINT;
    case SIGILL: return W_SIGILL;
    case SIGFPE: return W_SIGFPE;
    case SIGSEGV: return W_SIGSEGV;
    case SIGTERM: return W_SIGTERM;
    case SIGABRT: return W_SIGABRT;
    default: return 0;
    }
}

static void dispatch(int sig)
{
    int w = win_signal(sig);
    sig_fn fn = handlers[w];
    handlers[w] = NULL;
    signal(sig, SIG_DFL);
    if (fn)
        fn(w);
}

static sig_fn WINAPI signal_(int w, sig_fn fn)
{
    int h = host_signal(w);
    if (h < 0) {
        errno_ = 22;
        return (sig_fn)(intptr_t)-1;
    }
    sig_fn old = handlers[w];
    handlers[w] = fn;
    if ((intptr_t)fn == 0)
        signal(h, SIG_DFL);
    else if ((intptr_t)fn == 1)
        signal(h, SIG_IGN);
    else
        signal(h, dispatch);
    return old;
}

static int WINAPI raise_(int w)
{
    int h = host_signal(w);
    if (h < 0)
        return -1;
    sig_fn fn = handlers[w];
    if ((intptr_t)fn > 1) {
        handlers[w] = NULL;
        fn(w);
        return 0;
    }
    if ((intptr_t)fn == 1)
        return 0;
    if (w == W_SIGABRT || w == W_SIGINT || w == W_SIGTERM) {
        crt_flush_all();
        process_exit(3);
    }
    return raise(h);
}

static void make_locks(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    for (int i = 0; i < 64; i++)
        pthread_mutex_init(&locks[i], &a);
    pthread_mutexattr_destroy(&a);
}

static void WINAPI _lock(int n)
{
    pthread_once(&locks_once, make_locks);
    pthread_mutex_lock(&locks[n & 63]);
}

static void WINAPI _unlock(int n)
{
    pthread_once(&locks_once, make_locks);
    pthread_mutex_unlock(&locks[n & 63]);
}

static WCHAR c_locale_name[] = { 'C', 0 };

static char *WINAPI setlocale_(int category, const char *name)
{
    static char current[64] = "C";
    if (name && *name)
        snprintf(current, sizeof current, "%s", name);
    return current;
}

static WCHAR *WINAPI _wsetlocale(int category, const WCHAR *name) { return c_locale_name; }

static struct lconv *WINAPI localeconv_(void) { return localeconv(); }

static int WINAPI ___mb_cur_max_func(void) { return mb_cur_max; }
static UINT WINAPI ___lc_codepage_func(void) { return 65001; }
static UINT WINAPI ___lc_collate_cp_func(void) { return 65001; }
static WCHAR **WINAPI ___lc_locale_name_func(void)
{
    static WCHAR *names[6] = { c_locale_name, c_locale_name, c_locale_name, c_locale_name, c_locale_name, c_locale_name };
    return names;
}

static void *WINAPI _create_locale(int category, const char *name) { return (void *)1; }
static void WINAPI _free_locale(void *loc) {}
static void *WINAPI _get_current_locale(void) { return (void *)1; }

__attribute__((constructor)) static void ctype_init(void)
{
    for (int c = 0; c < 256; c++) {
        unsigned short f = 0;
        if (c < 128) {
            if (isupper(c)) f |= 0x1;
            if (islower(c)) f |= 0x2;
            if (isdigit(c)) f |= 0x4;
            if (isspace(c)) f |= 0x8;
            if (ispunct(c)) f |= 0x10;
            if (iscntrl(c)) f |= 0x20;
            if (c == ' ') f |= 0x40;
            if (isxdigit(c)) f |= 0x80;
            if (isalpha(c)) f |= 0x100;
        }
        ctype_table[c + 1] = f;
    }
}

static const unsigned short *WINAPI __pctype_func(void) { return ctype_ptr; }
static const unsigned short **WINAPI __p__pctype(void) { return &ctype_ptr; }

static int WINAPI _isctype(int c, int mask) { return c >= -1 && c < 256 ? ctype_table[c + 1] & mask : 0; }

#define CT(name, mask) static int WINAPI name##_(int c) { return _isctype(c, mask); }
CT(isalpha, 0x100) CT(isdigit, 0x4) CT(isxdigit, 0x80) CT(isspace, 0x8) CT(isupper, 0x1)
CT(islower, 0x2) CT(ispunct, 0x10) CT(iscntrl, 0x20) CT(isalnum, 0x105) CT(isprint, 0x1D7)
CT(isgraph, 0x197)
static int WINAPI isblank_(int c) { return c == ' ' || c == '\t'; }
static int WINAPI tolower_(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int WINAPI toupper_(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int WINAPI isascii_(int c) { return (unsigned)c < 0x80; }
static int WINAPI toascii_(int c) { return c & 0x7F; }
static int WINAPI __iscsym(int c) { return isalnum_(c) || c == '_'; }
static int WINAPI __iscsymf(int c) { return isalpha_(c) || c == '_'; }

struct start {
    thread_fn fn;
    thread_fn_void fn_void;
    void *arg;
};

static DWORD WINAPI begin_entry(void *p)
{
    struct start s = *(struct start *)p;
    free(p);
    if (s.fn)
        return s.fn(s.arg);
    s.fn_void(s.arg);
    return 0;
}

static uintptr_t WINAPI _beginthreadex(void *security, unsigned stack, thread_fn fn, void *arg, unsigned flags,
                                       unsigned *tid)
{
    struct start *s = malloc(sizeof *s);
    *s = (struct start){ fn, NULL, arg };
    DWORD id;
    HANDLE h = thread_spawn(begin_entry, s, stack, flags & 4, &id);
    if (tid)
        *tid = id;
    return (uintptr_t)h;
}

static uintptr_t WINAPI _beginthread(thread_fn_void fn, unsigned stack, void *arg)
{
    struct start *s = malloc(sizeof *s);
    *s = (struct start){ NULL, fn, arg };
    HANDLE h = thread_spawn(begin_entry, s, stack, false, NULL);
    return h ? (uintptr_t)h : (uintptr_t)-1;
}

static void WINAPI _endthreadex(unsigned code) { thread_exit(code); }
static void WINAPI _endthread(void) { thread_exit(0); }

static void WINAPI _invalid_parameter_noinfo(void) { errno_ = 22; }
static _Noreturn void WINAPI _invalid_parameter_noinfo_noreturn(void)
{
    fputs("invalid parameter passed to a C runtime function\n", stderr);
    crt_flush_all();
    process_exit(0xC0000417);
}
static void *WINAPI _set_invalid_parameter_handler(void *h) { return NULL; }
static void *WINAPI _set_purecall_handler(void *h) { return NULL; }
static unsigned WINAPI _set_abort_behavior(unsigned flags, unsigned mask) { return 0; }
static int WINAPI _set_error_mode(int mode) { return 0; }
static void WINAPI __crt_debugger_hook(int r) {}
static int WINAPI _CrtSetReportMode(int type, int mode) { return 0; }
static int WINAPI _CrtDbgReport(int type, const char *file, int line, const char *mod, const char *fmt, ...) { return 0; }
static void *WINAPI _set_new_handler(void *h) { return NULL; }
static int WINAPI _query_new_mode(void) { return 0; }
static int WINAPI _callnewh(size_t n) { return 0; }
static void *WINAPI __sys_errlist(void) { return NULL; }
static int *WINAPI __sys_nerr(void) { static int n = 43; return &n; }
static int WINAPI _getpid(void) { return getpid(); }
static unsigned WINAPI _get_heap_handle(void) { return 1; }

static int WINAPI _get_pgmptr(char **out)
{
    *out = (char *)process_exe_windows();
    return 0;
}

static _Noreturn void WINAPI _assert(const char *expr, const char *file, unsigned line)
{
    fprintf(stderr, "Assertion failed: %s, file %s, line %u\n", expr, file, line);
    crt_flush_all();
    process_exit(3);
}

static _Noreturn void WINAPI _wassert(const WCHAR *expr, const WCHAR *file, unsigned line)
{
    char *e = wide_to_host(expr), *f = wide_to_host(file);
    _assert(e, f, line);
}

static _Noreturn void WINAPI __report_gsfailure(uint64_t cookie)
{
    fputs("stack buffer overrun detected\n", stderr);
    crt_flush_all();
    process_exit(0xC0000409);
}

static _Noreturn void WINAPI _purecall(void)
{
    fputs("pure virtual function call\n", stderr);
    crt_flush_all();
    process_exit(3);
}

BIT_TABLE(crt_misc,
          BIT_FN(_errno), BIT_FN(__doserrno), BIT_FN(_get_errno), BIT_FN(_set_errno),
          BIT_AS("signal", signal_), BIT_AS("raise", raise_),
          BIT_FN(_lock), BIT_FN(_unlock),
          BIT_AS("setlocale", setlocale_), BIT_FN(_wsetlocale), BIT_AS("localeconv", localeconv_),
          BIT_FN(___mb_cur_max_func), BIT_AS("__p___mb_cur_max", ___mb_cur_max_func),
          BIT_DATA("__mb_cur_max", mb_cur_max),
          BIT_FN(___lc_codepage_func), BIT_FN(___lc_collate_cp_func), BIT_FN(___lc_locale_name_func),
          BIT_FN(_create_locale), BIT_FN(_free_locale), BIT_FN(_get_current_locale),
          BIT_FN(__pctype_func), BIT_FN(__p__pctype), BIT_DATA("_pctype", ctype_ptr), BIT_FN(_isctype),
          BIT_AS("isalpha", isalpha_), BIT_AS("isdigit", isdigit_), BIT_AS("isxdigit", isxdigit_),
          BIT_AS("isspace", isspace_), BIT_AS("isupper", isupper_), BIT_AS("islower", islower_),
          BIT_AS("ispunct", ispunct_), BIT_AS("iscntrl", iscntrl_), BIT_AS("isalnum", isalnum_),
          BIT_AS("isprint", isprint_), BIT_AS("isgraph", isgraph_), BIT_AS("isblank", isblank_),
          BIT_AS("tolower", tolower_), BIT_AS("toupper", toupper_),
          BIT_AS("_tolower", tolower_), BIT_AS("_toupper", toupper_),
          BIT_AS("__isascii", isascii_), BIT_AS("isascii", isascii_), BIT_AS("__toascii", toascii_),
          BIT_FN(__iscsym), BIT_FN(__iscsymf),
          BIT_FN(_beginthreadex), BIT_FN(_beginthread), BIT_FN(_endthreadex), BIT_FN(_endthread),
          BIT_FN(_invalid_parameter_noinfo), BIT_FN(_invalid_parameter_noinfo_noreturn),
          BIT_FN(_set_invalid_parameter_handler), BIT_FN(_set_purecall_handler),
          BIT_FN(_set_abort_behavior), BIT_FN(_set_error_mode), BIT_FN(__crt_debugger_hook),
          BIT_FN(_CrtSetReportMode), BIT_FN(_CrtDbgReport),
          BIT_FN(_set_new_handler), BIT_FN(_query_new_mode), BIT_FN(_callnewh),
          BIT_FN(__sys_errlist), BIT_FN(__sys_nerr), BIT_FN(_getpid), BIT_FN(_get_heap_handle),
          BIT_FN(_get_pgmptr),
          BIT_FN(_assert), BIT_FN(_wassert), BIT_FN(__report_gsfailure), BIT_FN(_purecall));
