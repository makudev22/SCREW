#include "k32.h"

#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define STD_INPUT ((DWORD)-10)
#define STD_OUTPUT ((DWORD)-11)
#define STD_ERROR ((DWORD)-12)

#define ENABLE_PROCESSED_INPUT 0x1
#define ENABLE_LINE_INPUT 0x2
#define ENABLE_ECHO_INPUT 0x4
#define ENABLE_PROCESSED_OUTPUT 0x1
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x4

#define CP_UTF8 65001

typedef struct {
    short x, y;
} COORD;

typedef struct {
    short left, top, right, bottom;
} SMALL_RECT;

typedef struct {
    COORD size;
    COORD cursor;
    WORD attributes;
    SMALL_RECT window;
    COORD max;
} SCREEN_INFO;

typedef BOOL WINAPI (*ctrl_handler)(DWORD type);

static WORD text_attr = 0x07;
static ctrl_handler handlers[16];
static int handler_count;
static bool ignore_ctrl_c;

static int std_index(DWORD which)
{
    return which == STD_INPUT ? 0 : which == STD_OUTPUT ? 1 : which == STD_ERROR ? 2 : -1;
}

static HANDLE WINAPI GetStdHandle(DWORD which) { return std_handle(std_index(which)); }

static BOOL WINAPI SetStdHandle(DWORD which, HANDLE h)
{
    int i = std_index(which);
    if (i < 0)
        return fail(ERROR_INVALID_PARAMETER);
    std_handle_set(i, h);
    return TRUE;
}

static BOOL write_all(HANDLE h, const char *s, size_t n)
{
    struct file *f = file_get(h);
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    while (n) {
        ssize_t w = write(f->fd, s, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w < 0)
            return fail_errno();
        s += w;
        n -= w;
    }
    return TRUE;
}

static BOOL WINAPI WriteConsoleA(HANDLE h, const void *buf, DWORD n, LPDWORD done, void *reserved)
{
    BOOL r = write_all(h, buf, n);
    if (done)
        *done = r ? n : 0;
    return r;
}

static BOOL WINAPI WriteConsoleW(HANDLE h, const void *buf, DWORD n, LPDWORD done, void *reserved)
{
    int len = utf16_to_utf8(buf, n, NULL, 0);
    char *u = malloc(len + 1);
    utf16_to_utf8(buf, n, u, len);
    BOOL r = write_all(h, u, len);
    free(u);
    if (done)
        *done = r ? n : 0;
    return r;
}

static BOOL WINAPI ReadConsoleA(HANDLE h, LPVOID buf, DWORD n, LPDWORD done, void *ctl)
{
    struct file *f = file_get(h);
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    ssize_t r = read(f->fd, buf, n);
    if (r < 0)
        return fail_errno();
    if (done)
        *done = r;
    return TRUE;
}

static BOOL WINAPI ReadConsoleW(HANDLE h, LPVOID buf, DWORD n, LPDWORD done, void *ctl)
{
    struct file *f = file_get(h);
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    char tmp[4096];
    size_t cap = n < sizeof tmp ? n : sizeof tmp;
    ssize_t r = read(f->fd, tmp, cap);
    if (r < 0)
        return fail_errno();
    int w = utf8_to_utf16(tmp, r, buf, n);
    if (done)
        *done = w < 0 ? 0 : w;
    return TRUE;
}

static BOOL WINAPI GetConsoleMode(HANDLE h, LPDWORD mode)
{
    struct file *f = file_get(h);
    if (!f || !isatty(f->fd))
        return fail(ERROR_INVALID_HANDLE);
    if (f == file_get(std_handle(0))) {
        struct termios t;
        tcgetattr(f->fd, &t);
        *mode = ENABLE_PROCESSED_INPUT | (t.c_lflag & ICANON ? ENABLE_LINE_INPUT : 0) |
                (t.c_lflag & ECHO ? ENABLE_ECHO_INPUT : 0);
    } else {
        *mode = ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    }
    return TRUE;
}

static BOOL WINAPI SetConsoleMode(HANDLE h, DWORD mode)
{
    struct file *f = file_get(h);
    if (!f || !isatty(f->fd))
        return fail(ERROR_INVALID_HANDLE);
    if (f == file_get(std_handle(0))) {
        struct termios t;
        tcgetattr(f->fd, &t);
        t.c_lflag = (t.c_lflag & ~(ICANON | ECHO)) | (mode & ENABLE_LINE_INPUT ? ICANON : 0) |
                    (mode & ENABLE_ECHO_INPUT ? ECHO : 0);
        tcsetattr(f->fd, TCSANOW, &t);
    }
    return TRUE;
}

static UINT WINAPI GetConsoleCP(void) { return CP_UTF8; }
static UINT WINAPI GetConsoleOutputCP(void) { return CP_UTF8; }
static BOOL WINAPI SetConsoleCP(UINT cp) { return TRUE; }
static BOOL WINAPI SetConsoleOutputCP(UINT cp) { return TRUE; }

static void title(const char *t)
{
    if (isatty(1))
        dprintf(1, "\033]0;%s\007", t);
}

static BOOL WINAPI SetConsoleTitleA(LPCSTR t)
{
    title(t);
    return TRUE;
}

static BOOL WINAPI SetConsoleTitleW(LPCWSTR t)
{
    char *u = wide_to_host(t);
    title(u);
    free(u);
    return TRUE;
}

static DWORD WINAPI GetConsoleTitleA(LPSTR out, DWORD size) { return copy_out_a("Screw", out, size); }
static DWORD WINAPI GetConsoleTitleW(LPWSTR out, DWORD size) { return copy_out_w("Screw", out, size); }

static BOOL WINAPI GetConsoleScreenBufferInfo(HANDLE h, SCREEN_INFO *info)
{
    struct file *f = file_get(h);
    struct winsize ws;
    if (!f || ioctl(f->fd, TIOCGWINSZ, &ws))
        return fail(ERROR_INVALID_HANDLE);
    memset(info, 0, sizeof *info);
    info->size = (COORD){ ws.ws_col, ws.ws_row };
    info->attributes = text_attr;
    info->window = (SMALL_RECT){ 0, 0, ws.ws_col - 1, ws.ws_row - 1 };
    info->max = info->size;
    return TRUE;
}

static BOOL WINAPI SetConsoleTextAttribute(HANDLE h, WORD attr)
{
    static const int ansi[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    struct file *f = file_get(h);
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    text_attr = attr;
    if (isatty(f->fd)) {
        int fg = ansi[attr & 7] + (attr & 8 ? 90 : 30);
        int bg = ansi[(attr >> 4) & 7] + (attr & 0x80 ? 100 : 40);
        dprintf(f->fd, "\033[0;%d;%dm", fg, bg);
    }
    return TRUE;
}

static BOOL WINAPI SetConsoleCursorPosition(HANDLE h, COORD pos)
{
    struct file *f = file_get(h);
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    if (isatty(f->fd))
        dprintf(f->fd, "\033[%d;%dH", pos.y + 1, pos.x + 1);
    return TRUE;
}

static BOOL WINAPI FillConsoleOutputCharacterA(HANDLE h, char c, DWORD n, COORD at, LPDWORD done)
{
    struct file *f = file_get(h);
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    if (isatty(f->fd)) {
        if (c == ' ' && !at.x && !at.y)
            dprintf(f->fd, "\033[2J\033[H");
        else
            for (DWORD i = 0; i < n; i++)
                write(f->fd, &c, 1);
    }
    if (done)
        *done = n;
    return TRUE;
}

static BOOL WINAPI FillConsoleOutputCharacterW(HANDLE h, WCHAR c, DWORD n, COORD at, LPDWORD done)
{
    return FillConsoleOutputCharacterA(h, c < 0x80 ? (char)c : '?', n, at, done);
}

static BOOL WINAPI FillConsoleOutputAttribute(HANDLE h, WORD attr, DWORD n, COORD at, LPDWORD done)
{
    if (done)
        *done = n;
    return TRUE;
}

static void on_interrupt(int sig)
{
    DWORD type = sig == SIGINT ? 0 : 1;
    for (int i = handler_count - 1; i >= 0; i--)
        if (handlers[i](type))
            return;
    if (sig == SIGINT && ignore_ctrl_c)
        return;
    signal(sig, SIG_DFL);
    raise(sig);
}

static BOOL WINAPI SetConsoleCtrlHandler(ctrl_handler h, BOOL add)
{
    if (!h) {
        ignore_ctrl_c = add;
    } else if (add) {
        if (handler_count == 16)
            return fail(ERROR_NOT_ENOUGH_MEMORY);
        handlers[handler_count++] = h;
    } else {
        for (int i = 0; i < handler_count; i++)
            if (handlers[i] == h) {
                handlers[i] = handlers[--handler_count];
                break;
            }
    }
    struct sigaction sa = { .sa_handler = on_interrupt };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    return TRUE;
}

static BOOL WINAPI AllocConsole(void) { return TRUE; }
static BOOL WINAPI FreeConsole(void) { return TRUE; }
static BOOL WINAPI AttachConsole(DWORD pid) { return TRUE; }
static HWND WINAPI GetConsoleWindow(void) { return NULL; }

static BOOL WINAPI FlushConsoleInputBuffer(HANDLE h)
{
    struct file *f = file_get(h);
    if (f)
        tcflush(f->fd, TCIFLUSH);
    return TRUE;
}

static BOOL WINAPI GetNumberOfConsoleInputEvents(HANDLE h, LPDWORD n)
{
    struct file *f = file_get(h);
    int avail = 0;
    if (!f || ioctl(f->fd, FIONREAD, &avail))
        return fail(ERROR_INVALID_HANDLE);
    *n = avail;
    return TRUE;
}

BIT_TABLE(k32_console,
          BIT_FN(GetStdHandle), BIT_FN(SetStdHandle),
          BIT_FN(WriteConsoleA), BIT_FN(WriteConsoleW),
          BIT_FN(ReadConsoleA), BIT_FN(ReadConsoleW),
          BIT_FN(GetConsoleMode), BIT_FN(SetConsoleMode),
          BIT_FN(GetConsoleCP), BIT_FN(GetConsoleOutputCP),
          BIT_FN(SetConsoleCP), BIT_FN(SetConsoleOutputCP),
          BIT_FN(SetConsoleTitleA), BIT_FN(SetConsoleTitleW),
          BIT_FN(GetConsoleTitleA), BIT_FN(GetConsoleTitleW),
          BIT_FN(GetConsoleScreenBufferInfo), BIT_FN(SetConsoleTextAttribute),
          BIT_FN(SetConsoleCursorPosition),
          BIT_FN(FillConsoleOutputCharacterA), BIT_FN(FillConsoleOutputCharacterW),
          BIT_FN(FillConsoleOutputAttribute),
          BIT_FN(SetConsoleCtrlHandler),
          BIT_FN(AllocConsole), BIT_FN(FreeConsole), BIT_FN(AttachConsole),
          BIT_FN(GetConsoleWindow),
          BIT_FN(FlushConsoleInputBuffer), BIT_FN(GetNumberOfConsoleInputEvents));
