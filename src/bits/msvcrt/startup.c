#include "crt.h"

#include <screw/log.h>

#include <pthread.h>

struct onexit_table {
    crt_fn *first;
    crt_fn *last;
    crt_fn *end;
};

static int app_type;
static int commode;
static int fmode;
static int new_mode;
static char **initenv;
static WCHAR **winitenv;
static char *acmdln;
static WCHAR *wcmdln;
static int argc_;
static char **argv_;
static WCHAR **wargv_;
static char **environ_;
static WCHAR **wenviron_;
static void *matherr;

static crt_fn *exit_fns;
static size_t exit_count, exit_cap;
static pthread_mutex_t exit_lock = PTHREAD_MUTEX_INITIALIZER;

void crt_init(void)
{
    if (argv_)
        return;
    argc_ = process_argc();
    argv_ = process_argv();
    wargv_ = process_wargv();
    environ_ = env_vector();
    size_t n = 0;
    while (environ_[n])
        n++;
    wenviron_ = calloc(n + 1, sizeof *wenviron_);
    for (size_t i = 0; i < n; i++)
        wenviron_[i] = host_to_wide(environ_[i]);
    initenv = environ_;
    winitenv = wenviron_;
    acmdln = (char *)process_cmdline();
    wcmdln = (WCHAR *)process_wcmdline();
}

static void push_exit(crt_fn fn)
{
    pthread_mutex_lock(&exit_lock);
    if (exit_count == exit_cap) {
        exit_cap = exit_cap ? exit_cap * 2 : 32;
        exit_fns = realloc(exit_fns, exit_cap * sizeof *exit_fns);
    }
    exit_fns[exit_count++] = fn;
    pthread_mutex_unlock(&exit_lock);
}

void crt_run_atexit(void)
{
    for (;;) {
        pthread_mutex_lock(&exit_lock);
        crt_fn fn = exit_count ? exit_fns[--exit_count] : NULL;
        pthread_mutex_unlock(&exit_lock);
        if (!fn)
            break;
        fn();
    }
}

void crt_exit(int code)
{
    crt_run_atexit();
    crt_flush_all();
    process_exit(code);
}

static int WINAPI __getmainargs(int *argc, char ***argv, char ***env, int glob, void *info)
{
    crt_init();
    *argc = argc_;
    *argv = argv_;
    *env = environ_;
    return 0;
}

static int WINAPI __wgetmainargs(int *argc, WCHAR ***argv, WCHAR ***env, int glob, void *info)
{
    crt_init();
    *argc = argc_;
    *argv = wargv_;
    *env = wenviron_;
    return 0;
}

static void WINAPI _initterm(crt_fn *begin, crt_fn *end)
{
    for (; begin < end; begin++)
        if (*begin)
            (*begin)();
}

static int WINAPI _initterm_e(crt_ifn *begin, crt_ifn *end)
{
    for (; begin < end; begin++)
        if (*begin) {
            int r = (*begin)();
            if (r)
                return r;
        }
    return 0;
}

static void WINAPI __set_app_type(int type) { app_type = type; }
static void WINAPI _set_app_type(int type) { app_type = type; }
static void WINAPI __setusermatherr(void *fn) { matherr = fn; }
static int WINAPI _set_new_mode(int mode)
{
    int old = new_mode;
    new_mode = mode;
    return old;
}
static int WINAPI _configthreadlocale(int mode) { return 1; }

static int WINAPI atexit_(crt_fn fn)
{
    push_exit(fn);
    return 0;
}

static crt_fn WINAPI _onexit(crt_fn fn)
{
    push_exit(fn);
    return fn;
}

static int WINAPI _initialize_onexit_table(struct onexit_table *t)
{
    t->first = t->last = t->end = NULL;
    return 0;
}

static int WINAPI _register_onexit_function(struct onexit_table *t, crt_fn fn)
{
    if (!t)
        return -1;
    if (t->last == t->end) {
        size_t n = t->last - t->first, cap = n ? n * 2 : 32;
        crt_fn *p = realloc(t->first, cap * sizeof *p);
        if (!p)
            return -1;
        t->first = p;
        t->last = p + n;
        t->end = p + cap;
    }
    *t->last++ = fn;
    return 0;
}

static int WINAPI _execute_onexit_table(struct onexit_table *t)
{
    if (!t)
        return -1;
    crt_fn *first = t->first, *last = t->last;
    t->first = t->last = t->end = NULL;
    while (last > first)
        if (*--last)
            (*last)();
    free(first);
    return 0;
}

static _Noreturn void WINAPI exit_(int code) { crt_exit(code); }

static _Noreturn void WINAPI _exit_(int code)
{
    crt_flush_all();
    process_exit(code);
}

static void WINAPI _cexit(void)
{
    crt_run_atexit();
    crt_flush_all();
}

static void WINAPI _c_exit(void) { crt_flush_all(); }

static _Noreturn void WINAPI _amsg_exit(int code) { die("C runtime error R60%02d", code); }

static _Noreturn void WINAPI abort_(void)
{
    fputs("\nThis application has requested the Runtime to terminate it in an unusual way.\n", stderr);
    crt_flush_all();
    process_exit(3);
}

static int *WINAPI __p___argc(void) { crt_init(); return &argc_; }
static char ***WINAPI __p___argv(void) { crt_init(); return &argv_; }
static WCHAR ***WINAPI __p___wargv(void) { crt_init(); return &wargv_; }
static char ***WINAPI __p__environ(void) { crt_init(); return &environ_; }
static WCHAR ***WINAPI __p__wenviron(void) { crt_init(); return &wenviron_; }
static int *WINAPI __p__commode(void) { return &commode; }
static int *WINAPI __p__fmode(void) { return &fmode; }
static char **WINAPI __p__acmdln(void) { crt_init(); return &acmdln; }
static WCHAR **WINAPI __p__wcmdln(void) { crt_init(); return &wcmdln; }

static int WINAPI _configure_narrow_argv(int mode) { crt_init(); return 0; }
static int WINAPI _configure_wide_argv(int mode) { crt_init(); return 0; }
static int WINAPI _initialize_narrow_environment(void) { crt_init(); return 0; }
static int WINAPI _initialize_wide_environment(void) { crt_init(); return 0; }
static char **WINAPI _get_initial_narrow_environment(void) { crt_init(); return environ_; }
static WCHAR **WINAPI _get_initial_wide_environment(void) { crt_init(); return wenviron_; }

static char *WINAPI _get_narrow_winmain_command_line(void)
{
    crt_init();
    char *p = acmdln;
    bool quoted = false;
    for (; *p && (quoted || (*p != ' ' && *p != '\t')); p++)
        if (*p == '"')
            quoted = !quoted;
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

static WCHAR *WINAPI _get_wide_winmain_command_line(void)
{
    crt_init();
    WCHAR *p = wcmdln;
    bool quoted = false;
    for (; *p && (quoted || (*p != ' ' && *p != '\t')); p++)
        if (*p == '"')
            quoted = !quoted;
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

static int WINAPI _seh_filter_exe(DWORD code, void *info) { return 0; }
static void WINAPI _register_thread_local_exe_atexit_callback(void *cb) {}

static int WINAPI _crt_atexit(crt_fn fn) { return atexit_(fn); }
static int WINAPI _crt_at_quick_exit(crt_fn fn) { return atexit_(fn); }

static int WINAPI __lconv_init(void) { return 0; }

BIT_TABLE(crt_startup,
          BIT_FN(__getmainargs), BIT_FN(__wgetmainargs),
          BIT_FN(_initterm), BIT_FN(_initterm_e),
          BIT_FN(__set_app_type), BIT_FN(_set_app_type), BIT_FN(__setusermatherr),
          BIT_FN(_set_new_mode), BIT_FN(_configthreadlocale),
          BIT_AS("atexit", atexit_), BIT_FN(_onexit),
          BIT_FN(_initialize_onexit_table), BIT_FN(_register_onexit_function),
          BIT_FN(_execute_onexit_table), BIT_FN(_crt_atexit), BIT_FN(_crt_at_quick_exit),
          BIT_AS("exit", exit_), BIT_AS("_exit", _exit_), BIT_AS("_Exit", _exit_),
          BIT_AS("quick_exit", exit_),
          BIT_FN(_cexit), BIT_FN(_c_exit), BIT_FN(_amsg_exit), BIT_AS("abort", abort_),
          BIT_FN(__p___argc), BIT_FN(__p___argv), BIT_FN(__p___wargv),
          BIT_FN(__p__environ), BIT_FN(__p__wenviron),
          BIT_FN(__p__commode), BIT_FN(__p__fmode), BIT_FN(__p__acmdln), BIT_FN(__p__wcmdln),
          BIT_FN(_configure_narrow_argv), BIT_FN(_configure_wide_argv),
          BIT_FN(_initialize_narrow_environment), BIT_FN(_initialize_wide_environment),
          BIT_FN(_get_initial_narrow_environment), BIT_FN(_get_initial_wide_environment),
          BIT_FN(_get_narrow_winmain_command_line), BIT_FN(_get_wide_winmain_command_line),
          BIT_FN(_seh_filter_exe), BIT_FN(_register_thread_local_exe_atexit_callback),
          BIT_FN(__lconv_init),
          BIT_DATA("_commode", commode), BIT_DATA("_fmode", fmode),
          BIT_DATA("__initenv", initenv), BIT_DATA("__winitenv", winitenv),
          BIT_DATA("_acmdln", acmdln), BIT_DATA("_wcmdln", wcmdln),
          BIT_DATA("__argc", argc_), BIT_DATA("__argv", argv_), BIT_DATA("__wargv", wargv_),
          BIT_DATA("_environ", environ_), BIT_DATA("_wenviron", wenviron_));
