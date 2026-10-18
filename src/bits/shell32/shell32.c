#include <screw/bit.h>
#include <screw/path.h>
#include <screw/process.h>
#include <screw/teb.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef long HRESULT;
#define S_OK 0
#define E_FAIL 0x80004005u

#define CSIDL_APPDATA 0x1a
#define CSIDL_LOCAL_APPDATA 0x1c
#define CSIDL_PERSONAL 0x05
#define CSIDL_PROFILE 0x28
#define CSIDL_WINDOWS 0x24
#define CSIDL_SYSTEM 0x25
#define CSIDL_PROGRAM_FILES 0x26
#define CSIDL_FLAG_CREATE 0x8000

static LPWSTR *WINAPI CommandLineToArgvW(LPCWSTR cmdline, int *argc)
{
    const WCHAR *p = cmdline;
    WCHAR *buf = malloc((wstr_len(cmdline) + 1) * sizeof(WCHAR));
    int count = 0;
    size_t caps = 8;
    LPWSTR *argv = malloc(caps * sizeof(LPWSTR));
    WCHAR *out = buf;

    for (;;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if ((size_t)count + 2 > caps) {
            caps *= 2;
            argv = realloc(argv, caps * sizeof(LPWSTR));
        }
        argv[count++] = out;
        bool quoted = false;
        while (*p) {
            if (!quoted && (*p == ' ' || *p == '\t'))
                break;
            if (*p == '"') {
                if (quoted && p[1] == '"') {
                    *out++ = '"';
                    p += 2;
                    continue;
                }
                quoted = !quoted;
                p++;
                continue;
            }
            if (*p == '\\') {
                int slashes = 0;
                while (*p == '\\') {
                    slashes++;
                    p++;
                }
                if (*p == '"') {
                    for (int i = 0; i < slashes / 2; i++)
                        *out++ = '\\';
                    if (slashes % 2) {
                        *out++ = '"';
                        p++;
                    }
                } else {
                    for (int i = 0; i < slashes; i++)
                        *out++ = '\\';
                }
                continue;
            }
            *out++ = *p++;
        }
        *out++ = 0;
    }
    if (argc)
        *argc = count;
    argv[count] = NULL;
    return argv;
}

static const char *folder(int id)
{
    switch (id & 0xFF) {
    case CSIDL_APPDATA: return env_get("APPDATA");
    case CSIDL_LOCAL_APPDATA: return env_get("LOCALAPPDATA");
    case CSIDL_PROFILE: return env_get("USERPROFILE");
    case CSIDL_PERSONAL: return env_get("USERPROFILE");
    case CSIDL_WINDOWS: return "C:\\windows";
    case CSIDL_SYSTEM: return "C:\\windows\\system32";
    case CSIDL_PROGRAM_FILES: return env_get("ProgramFiles");
    default: return NULL;
    }
}

static void ensure(const char *win)
{
    char *host = path_to_host(win);
    if (host) {
        char buf[4096];
        snprintf(buf, sizeof buf, "%s", host);
        for (char *s = buf + 1; *s; s++)
            if (*s == '/') {
                *s = 0;
                mkdir(buf, 0755);
                *s = '/';
            }
        mkdir(buf, 0755);
        free(host);
    }
}

static HRESULT WINAPI SHGetFolderPathA(HWND wnd, int id, HANDLE token, DWORD flags, LPSTR out)
{
    const char *f = folder(id);
    if (!f)
        return (HRESULT)E_FAIL;
    if (id & CSIDL_FLAG_CREATE)
        ensure(f);
    strcpy(out, f);
    return S_OK;
}

static HRESULT WINAPI SHGetFolderPathW(HWND wnd, int id, HANDLE token, DWORD flags, LPWSTR out)
{
    const char *f = folder(id);
    if (!f)
        return (HRESULT)E_FAIL;
    if (id & CSIDL_FLAG_CREATE)
        ensure(f);
    utf8_to_utf16(f, -1, out, 260);
    return S_OK;
}

static BOOL WINAPI SHGetSpecialFolderPathA(HWND wnd, LPSTR out, int id, BOOL create)
{
    return SHGetFolderPathA(wnd, id | (create ? CSIDL_FLAG_CREATE : 0), NULL, 0, out) == S_OK;
}

static BOOL WINAPI SHGetSpecialFolderPathW(HWND wnd, LPWSTR out, int id, BOOL create)
{
    return SHGetFolderPathW(wnd, id | (create ? CSIDL_FLAG_CREATE : 0), NULL, 0, out) == S_OK;
}

static int WINAPI SHCreateDirectoryExA(HWND wnd, LPCSTR path, void *sa)
{
    ensure(path);
    return 0;
}

static intptr_t WINAPI ShellExecuteA(HWND wnd, LPCSTR verb, LPCSTR file, LPCSTR params, LPCSTR dir, int show)
{
    return 42;
}

static intptr_t WINAPI ShellExecuteW(HWND wnd, LPCWSTR verb, LPCWSTR file, LPCWSTR params, LPCWSTR dir, int show)
{
    return 42;
}

static DWORD WINAPI SHGetFileInfoA(LPCSTR path, DWORD attrs, void *info, UINT size, UINT flags) { return 0; }
static void WINAPI DragAcceptFiles(HWND wnd, BOOL accept) {}
static void WINAPI DragFinish(HANDLE drop) {}
static UINT WINAPI DragQueryFileA(HANDLE drop, UINT i, LPSTR file, UINT size) { return 0; }

BIT_TABLE(shell32_exports,
          BIT_FN(CommandLineToArgvW), BIT_FN(SHGetFolderPathA), BIT_FN(SHGetFolderPathW),
          BIT_FN(SHGetSpecialFolderPathA), BIT_FN(SHGetSpecialFolderPathW), BIT_FN(SHCreateDirectoryExA),
          BIT_FN(ShellExecuteA), BIT_FN(ShellExecuteW), BIT_FN(SHGetFileInfoA),
          BIT_FN(DragAcceptFiles), BIT_FN(DragFinish), BIT_FN(DragQueryFileA));

BIT_DEFINE(bit_shell32, "shell32.dll", &shell32_exports);
