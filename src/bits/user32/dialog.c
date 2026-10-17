#include "u32.h"

#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define MB_OK 0x0
#define MB_OKCANCEL 0x1
#define MB_ABORTRETRYIGNORE 0x2
#define MB_YESNOCANCEL 0x3
#define MB_YESNO 0x4
#define MB_RETRYCANCEL 0x5
#define MB_ICONERROR 0x10
#define MB_ICONQUESTION 0x20
#define MB_ICONWARNING 0x30

#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7

static bool have(const char *tool)
{
    char path[512];
    const char *env = getenv("PATH");
    if (!env)
        return false;
    char *copy = strdup(env), *save;
    bool found = false;
    for (char *dir = strtok_r(copy, ":", &save); dir && !found; dir = strtok_r(NULL, ":", &save)) {
        snprintf(path, sizeof path, "%s/%s", dir, tool);
        found = !access(path, X_OK);
    }
    free(copy);
    return found;
}

static int run(char *const argv[])
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool asks(UINT kind) { return kind != MB_OK; }

static int positive(UINT kind)
{
    switch (kind) {
    case MB_YESNO:
    case MB_YESNOCANCEL: return IDYES;
    case MB_RETRYCANCEL: return IDRETRY;
    case MB_ABORTRETRYIGNORE: return IDRETRY;
    default: return IDOK;
    }
}

static int negative(UINT kind)
{
    switch (kind) {
    case MB_YESNO:
    case MB_YESNOCANCEL: return IDNO;
    case MB_ABORTRETRYIGNORE: return IDABORT;
    default: return IDCANCEL;
    }
}

static int with_zenity(const char *text, const char *title, UINT kind, UINT icon)
{
    const char *mode = asks(kind) ? "--question" : icon == MB_ICONERROR ? "--error"
                       : icon == MB_ICONWARNING ? "--warning" : "--info";
    char *argv[] = { "zenity", (char *)mode, "--no-markup", "--title", (char *)title, "--text", (char *)text, NULL };
    int r = run(argv);
    if (r < 0 || r == 127)
        return -1;
    return r == 0 ? positive(kind) : negative(kind);
}

static int with_kdialog(const char *text, const char *title, UINT kind, UINT icon)
{
    const char *mode = asks(kind) ? "--yesno" : icon == MB_ICONERROR ? "--error"
                       : icon == MB_ICONWARNING ? "--sorry" : "--msgbox";
    char *argv[] = { "kdialog", "--title", (char *)title, (char *)mode, (char *)text, NULL };
    int r = run(argv);
    if (r < 0 || r == 127)
        return -1;
    return r == 0 ? positive(kind) : negative(kind);
}

static int in_terminal(const char *text, const char *title, UINT kind)
{
    fprintf(stderr, "\n[%s]\n%s\n", title, text);
    if (!asks(kind) || !isatty(0))
        return positive(kind);
    fputs("[y/n] ", stderr);
    int c = getchar();
    return c == 'y' || c == 'Y' ? positive(kind) : negative(kind);
}

static int show(const char *text, const char *title, UINT type)
{
    text = text ? text : "";
    title = title ? title : "Error";
    UINT kind = type & 0xF, icon = type & 0xF0;
    int r = -1;
    if (getenv("DISPLAY") || getenv("WAYLAND_DISPLAY")) {
        if (have("zenity"))
            r = with_zenity(text, title, kind, icon);
        if (r < 0 && have("kdialog"))
            r = with_kdialog(text, title, kind, icon);
    }
    return r < 0 ? in_terminal(text, title, kind) : r;
}

static int WINAPI MessageBoxA(HWND h, LPCSTR text, LPCSTR title, UINT type) { return show(text, title, type); }

static int WINAPI MessageBoxW(HWND h, LPCWSTR text, LPCWSTR title, UINT type)
{
    char *t = wide_to_host(text), *c = wide_to_host(title);
    int r = show(t, c, type);
    free(t);
    free(c);
    return r;
}

static int WINAPI MessageBoxExA(HWND h, LPCSTR text, LPCSTR title, UINT type, WORD lang)
{
    return show(text, title, type);
}

static int WINAPI MessageBoxExW(HWND h, LPCWSTR text, LPCWSTR title, UINT type, WORD lang)
{
    return MessageBoxW(h, text, title, type);
}

BIT_TABLE(u32_dialog,
          BIT_FN(MessageBoxA), BIT_FN(MessageBoxW), BIT_FN(MessageBoxExA), BIT_FN(MessageBoxExW));
