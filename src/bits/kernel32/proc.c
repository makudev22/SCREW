#include "k32.h"

#include <screw/process.h>
#include <screw/sync.h>

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CREATE_SUSPENDED 0x4u
#define STARTF_USESTDHANDLES 0x100u
#define STILL_ACTIVE 259u

extern char **environ;

typedef struct {
    DWORD size;
    LPVOID reserved;
    LPVOID desktop;
    LPVOID title;
    DWORD x, y, width, height, columns, rows, fill, flags;
    WORD show;
    WORD reserved2_size;
    BYTE *reserved2;
    HANDLE input;
    HANDLE output;
    HANDLE error;
} STARTUPINFO;

typedef struct {
    HANDLE process;
    HANDLE thread;
    DWORD pid;
    DWORD tid;
} PROCESS_INFORMATION;

struct child {
    struct waitable *w;
    pid_t pid;
};

static void *reap(void *p)
{
    struct child *c = p;
    int status = 0;
    while (waitpid(c->pid, &status, 0) < 0 && errno == EINTR)
        ;
    DWORD code = WIFEXITED(status) ? WEXITSTATUS(status) : 0xC0000005u;
    waitable_finish(c->w, code);
    object_unref(&c->w->base);
    free(c);
    return NULL;
}

static bool is_pe(const char *host)
{
    int fd = open(host, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    char mz[2] = { 0 };
    bool r = read(fd, mz, 2) == 2 && mz[0] == 'M' && mz[1] == 'Z';
    close(fd);
    return r;
}

static char *locate(const char *name)
{
    char *host = path_to_host(name);
    struct stat st;
    if (host && !stat(host, &st) && S_ISREG(st.st_mode))
        return host;
    if (host && !strchr(name, '.')) {
        char buf[PATH_MAX];
        snprintf(buf, sizeof buf, "%s.exe", name);
        free(host);
        host = path_to_host(buf);
        if (host && !stat(host, &st) && S_ISREG(st.st_mode))
            return host;
    }
    free(host);
    return NULL;
}

static int fd_of(HANDLE h, int fallback)
{
    struct file *f = file_get(h);
    return f ? f->fd : fallback;
}

static BOOL spawn(const char *app, const char *cmdline, BOOL inherit, STARTUPINFO *si,
                  const char *cwd, PROCESS_INFORMATION *pi)
{
    if (!app && !cmdline)
        return fail(ERROR_INVALID_PARAMETER);
    int argc;
    char **argv = cmdline_split(cmdline && *cmdline ? cmdline : app, &argc);
    if (!argc)
        return fail(ERROR_INVALID_PARAMETER);

    char *host = locate(app ? app : argv[0]);
    if (!host) {
        for (int i = 0; i < argc; i++)
            free(argv[i]);
        free(argv);
        return fail(ERROR_FILE_NOT_FOUND);
    }

    char **args = calloc(argc + 3, sizeof *args);
    int n = 0;
    char self[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", self, sizeof self - 1);
    self[len > 0 ? len : 0] = 0;
    const char *program = host;
    if (is_pe(host)) {
        args[n++] = self;
        program = self;
    }
    args[n++] = host;
    for (int i = 1; i < argc; i++)
        args[n++] = argv[i];

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (si && (si->flags & STARTF_USESTDHANDLES)) {
        posix_spawn_file_actions_adddup2(&fa, fd_of(si->input, 0), 0);
        posix_spawn_file_actions_adddup2(&fa, fd_of(si->output, 1), 1);
        posix_spawn_file_actions_adddup2(&fa, fd_of(si->error, 2), 2);
    }
    char *host_cwd = cwd ? path_to_host(cwd) : NULL;
    if (host_cwd)
        posix_spawn_file_actions_addchdir_np(&fa, host_cwd);

    pid_t pid;
    int r = posix_spawn(&pid, program, &fa, NULL, args, environ);
    posix_spawn_file_actions_destroy(&fa);
    free(host_cwd);
    free(args);
    free(host);
    for (int i = 0; i < argc; i++)
        free(argv[i]);
    free(argv);
    if (r) {
        errno = r;
        return fail_errno();
    }

    struct child *c = malloc(sizeof *c);
    c->w = waitable_new(WAIT_THREAD, 0, 1, true);
    c->pid = pid;
    object_ref(&c->w->base);
    pthread_t t;
    pthread_create(&t, NULL, reap, c);
    pthread_detach(t);

    pi->process = handle_new(&c->w->base);
    pi->thread = handle_dup(pi->process);
    pi->pid = pid;
    pi->tid = pid;
    return TRUE;
}

static BOOL WINAPI CreateProcessA(LPCSTR app, LPSTR cmdline, void *psa, void *tsa, BOOL inherit, DWORD flags,
                                  LPVOID env, LPCSTR cwd, STARTUPINFO *si, PROCESS_INFORMATION *pi)
{
    return spawn(app, cmdline, inherit, si, cwd, pi);
}

static BOOL WINAPI CreateProcessW(LPCWSTR app, LPWSTR cmdline, void *psa, void *tsa, BOOL inherit, DWORD flags,
                                  LPVOID env, LPCWSTR cwd, STARTUPINFO *si, PROCESS_INFORMATION *pi)
{
    char *a = wide_to_host(app), *c = wide_to_host(cmdline), *d = wide_to_host(cwd);
    BOOL r = spawn(a, c, inherit, si, d, pi);
    free(a);
    free(c);
    free(d);
    return r;
}

static BOOL WINAPI GetExitCodeProcess(HANDLE h, LPDWORD code)
{
    if (h == CURRENT_PROCESS) {
        *code = STILL_ACTIVE;
        return TRUE;
    }
    struct waitable *w = waitable_get(h);
    if (!w)
        return fail(ERROR_INVALID_HANDLE);
    *code = __atomic_load_n(&w->count, __ATOMIC_ACQUIRE) ? w->exit_code : STILL_ACTIVE;
    return TRUE;
}

static HANDLE WINAPI OpenProcess(DWORD access, BOOL inherit, DWORD pid)
{
    if (pid == (DWORD)getpid())
        return CURRENT_PROCESS;
    last_error_set(ERROR_ACCESS_DENIED);
    return NULL;
}

static UINT WINAPI WinExec(LPCSTR cmd, UINT show)
{
    PROCESS_INFORMATION pi;
    if (!spawn(NULL, cmd, FALSE, NULL, NULL, &pi))
        return 2;
    handle_close(pi.process);
    handle_close(pi.thread);
    return 33;
}

BIT_TABLE(k32_proc,
          BIT_FN(CreateProcessA), BIT_FN(CreateProcessW),
          BIT_FN(GetExitCodeProcess), BIT_FN(OpenProcess), BIT_FN(WinExec));
