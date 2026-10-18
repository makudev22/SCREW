#include "k32.h"

#include <screw/except.h>
#include <screw/log.h>

#include <cpuid.h>
#include <stdio.h>
#include <unistd.h>

#define WIN_MAJOR 10
#define WIN_MINOR 0
#define WIN_BUILD 19045

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x100u
#define FORMAT_MESSAGE_FROM_STRING 0x400u
#define FORMAT_MESSAGE_FROM_SYSTEM 0x1000u

typedef struct {
    WORD architecture;
    WORD reserved;
    DWORD page_size;
    LPVOID min_address;
    LPVOID max_address;
    DWORD_PTR active_mask;
    DWORD processors;
    DWORD processor_type;
    DWORD granularity;
    WORD level;
    WORD revision;
} SYSTEM_INFO;

typedef struct {
    DWORD size;
    DWORD major;
    DWORD minor;
    DWORD build;
    DWORD platform;
    char csd[128];
    WORD sp_major;
    WORD sp_minor;
    WORD suite;
    BYTE product;
    BYTE reserved;
} OSVERSIONINFOEXA;

typedef struct {
    DWORD size;
    DWORD major;
    DWORD minor;
    DWORD build;
    DWORD platform;
    WCHAR csd[128];
    WORD sp_major;
    WORD sp_minor;
    WORD suite;
    BYTE product;
    BYTE reserved;
} OSVERSIONINFOEXW;

typedef struct {
    ULONG_PTR mask;
    int relationship;
    BYTE flags;
    BYTE pad[15];
} PROCESSOR_INFO;

typedef LONG WINAPI (*exception_filter)(void *pointers);

static exception_filter top_filter;
static UINT error_mode;

static void WINAPI GetSystemInfo(SYSTEM_INFO *si)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    memset(si, 0, sizeof *si);
    si->architecture = 9;
    si->page_size = 4096;
    si->min_address = (LPVOID)0x10000;
    si->max_address = (LPVOID)0x7FFFFFFEFFFF;
    si->active_mask = n >= 64 ? ~(DWORD_PTR)0 : ((DWORD_PTR)1 << n) - 1;
    si->processors = n;
    si->processor_type = 8664;
    si->granularity = 65536;
    si->level = 6;
}

static void WINAPI GetNativeSystemInfo(SYSTEM_INFO *si) { GetSystemInfo(si); }

static DWORD WINAPI GetVersion(void) { return (WIN_BUILD << 16) | (WIN_MINOR << 8) | WIN_MAJOR; }

static BOOL WINAPI GetVersionExA(OSVERSIONINFOEXA *v)
{
    DWORD size = v->size;
    memset((char *)v + 4, 0, size - 4);
    v->major = WIN_MAJOR;
    v->minor = WIN_MINOR;
    v->build = WIN_BUILD;
    v->platform = 2;
    if (size >= sizeof *v)
        v->product = 1;
    return TRUE;
}

static BOOL WINAPI GetVersionExW(OSVERSIONINFOEXW *v)
{
    DWORD size = v->size;
    memset((char *)v + 4, 0, size - 4);
    v->major = WIN_MAJOR;
    v->minor = WIN_MINOR;
    v->build = WIN_BUILD;
    v->platform = 2;
    if (size >= sizeof *v)
        v->product = 1;
    return TRUE;
}

static BOOL WINAPI VerifyVersionInfoA(void *info, DWORD type, ULONGLONG cond) { return TRUE; }
static BOOL WINAPI VerifyVersionInfoW(void *info, DWORD type, ULONGLONG cond) { return TRUE; }
static ULONGLONG WINAPI VerSetConditionMask(ULONGLONG mask, DWORD type, BYTE cond) { return mask | ((ULONGLONG)cond << (type * 3)); }

static BOOL WINAPI GetProductInfo(DWORD a, DWORD b, DWORD c, DWORD d, LPDWORD type)
{
    *type = 0x30;
    return TRUE;
}

static BOOL WINAPI IsProcessorFeaturePresent(DWORD feature)
{
    unsigned a, b, c, d;
    __cpuid(1, a, b, c, d);
    switch (feature) {
    case 3: return TRUE;
    case 6: return !!(d & (1u << 25));
    case 7: return FALSE;
    case 8: return TRUE;
    case 10: return !!(d & (1u << 26));
    case 12: return TRUE;
    case 13: return !!(c & 1);
    case 17: return TRUE;
    case 23: return TRUE;
    case 36: return !!(c & (1u << 9));
    case 37: return !!(c & (1u << 19));
    case 38: return !!(c & (1u << 20));
    case 39: {
        __cpuid_count(7, 0, a, b, c, d);
        return !!(b & (1u << 5));
    }
    default: return FALSE;
    }
}

static DWORD WINAPI GetLastError(void) { return last_error_get(); }
static void WINAPI SetLastError(DWORD e) { last_error_set(e); }
static void WINAPI RestoreLastError(DWORD e) { last_error_set(e); }

static BOOL computer_name(char *buf, size_t size)
{
    if (gethostname(buf, size))
        snprintf(buf, size, "SCREW");
    for (char *p = buf; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p -= 32;
    return TRUE;
}

static BOOL WINAPI GetComputerNameA(LPSTR out, LPDWORD size)
{
    char name[256];
    computer_name(name, sizeof name);
    DWORD r = copy_out_a(name, out, *size);
    if (r >= *size) {
        *size = r;
        return FALSE;
    }
    *size = r;
    return TRUE;
}

static BOOL WINAPI GetComputerNameW(LPWSTR out, LPDWORD size)
{
    char name[256];
    computer_name(name, sizeof name);
    DWORD r = copy_out_w(name, out, *size);
    if (r >= *size) {
        *size = r;
        return FALSE;
    }
    *size = r;
    return TRUE;
}

static BOOL WINAPI GetComputerNameExA(int type, LPSTR out, LPDWORD size) { return GetComputerNameA(out, size); }
static BOOL WINAPI GetComputerNameExW(int type, LPWSTR out, LPDWORD size) { return GetComputerNameW(out, size); }

static UINT WINAPI GetSystemDirectoryA(LPSTR out, UINT size) { return copy_out_a("C:\\windows\\system32", out, size); }
static UINT WINAPI GetSystemDirectoryW(LPWSTR out, UINT size) { return copy_out_w("C:\\windows\\system32", out, size); }
static UINT WINAPI GetWindowsDirectoryA(LPSTR out, UINT size) { return copy_out_a("C:\\windows", out, size); }
static UINT WINAPI GetWindowsDirectoryW(LPWSTR out, UINT size) { return copy_out_w("C:\\windows", out, size); }
static UINT WINAPI GetSystemWindowsDirectoryA(LPSTR out, UINT size) { return GetWindowsDirectoryA(out, size); }
static UINT WINAPI GetSystemWindowsDirectoryW(LPWSTR out, UINT size) { return GetWindowsDirectoryW(out, size); }

static BOOL WINAPI IsDebuggerPresent(void) { return FALSE; }

static BOOL WINAPI CheckRemoteDebuggerPresent(HANDLE proc, BOOL *present)
{
    *present = FALSE;
    return TRUE;
}

static void WINAPI OutputDebugStringA(LPCSTR s) { trace("debug: %s", s); }

static void WINAPI OutputDebugStringW(LPCWSTR s)
{
    char *u = wide_to_host(s);
    trace("debug: %s", u);
    free(u);
}

static void WINAPI DebugBreak(void) { __builtin_trap(); }

static exception_filter WINAPI SetUnhandledExceptionFilter(exception_filter f)
{
    exception_filter prev = top_filter;
    top_filter = f;
    return prev;
}

static LONG WINAPI UnhandledExceptionFilter(void *pointers)
{
    return top_filter ? top_filter(pointers) : 0;
}

static UINT WINAPI SetErrorMode(UINT mode)
{
    UINT prev = error_mode;
    error_mode = mode;
    return prev;
}

static UINT WINAPI GetErrorMode(void) { return error_mode; }

static BOOL WINAPI SetThreadErrorMode(DWORD mode, LPDWORD old)
{
    if (old)
        *old = error_mode;
    error_mode = mode;
    return TRUE;
}

LONG screw_unhandled(EXCEPTION_RECORD *record, CONTEXT *context)
{
    if (top_filter) {
        void *pointers[2] = { record, context };
        if (top_filter(pointers) == -1)
            return -1;
    }
    die("unhandled exception 0x%08X at %p", record->exception_code, record->exception_address);
}

static void WINAPI RaiseException(DWORD code, DWORD flags, DWORD argc, const ULONG_PTR *args)
{
    if (code == 0x406D1388)
        return;
    screw_raise_exception(code, flags, argc, args);
}

static const char *message_for(DWORD id)
{
    switch (id) {
    case ERROR_SUCCESS: return "The operation completed successfully.";
    case ERROR_INVALID_FUNCTION: return "Incorrect function.";
    case ERROR_FILE_NOT_FOUND: return "The system cannot find the file specified.";
    case ERROR_PATH_NOT_FOUND: return "The system cannot find the path specified.";
    case ERROR_ACCESS_DENIED: return "Access is denied.";
    case ERROR_INVALID_HANDLE: return "The handle is invalid.";
    case ERROR_NOT_ENOUGH_MEMORY: return "Not enough memory resources are available to process this command.";
    case ERROR_NO_MORE_FILES: return "There are no more files.";
    case ERROR_FILE_EXISTS: return "The file exists.";
    case ERROR_INVALID_PARAMETER: return "The parameter is incorrect.";
    case ERROR_INSUFFICIENT_BUFFER: return "The data area passed to a system call is too small.";
    case ERROR_MOD_NOT_FOUND: return "The specified module could not be found.";
    case ERROR_PROC_NOT_FOUND: return "The specified procedure could not be found.";
    case ERROR_ALREADY_EXISTS: return "Cannot create a file when that file already exists.";
    case ERROR_ENVVAR_NOT_FOUND: return "The system could not find the environment option that was entered.";
    default: return NULL;
    }
}

static char *format(DWORD flags, LPCVOID source, DWORD id)
{
    const char *text = NULL;
    char buf[64];
    if (flags & FORMAT_MESSAGE_FROM_STRING)
        text = source;
    else if (flags & FORMAT_MESSAGE_FROM_SYSTEM) {
        text = message_for(id);
        if (!text) {
            snprintf(buf, sizeof buf, "Error %u.", id);
            text = buf;
        }
    }
    if (!text)
        return NULL;
    char *out = malloc(strlen(text) + 3);
    sprintf(out, "%s%s", text, flags & FORMAT_MESSAGE_FROM_SYSTEM ? "\r\n" : "");
    return out;
}

static DWORD WINAPI FormatMessageA(DWORD flags, LPCVOID source, DWORD id, DWORD lang, LPSTR out,
                                   DWORD size, void *args)
{
    char *msg = format(flags, source, id);
    if (!msg)
        return fail(ERROR_INVALID_PARAMETER);
    DWORD len = strlen(msg);
    if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
        *(char **)out = msg;
        return len;
    }
    bool fits = len < size;
    if (fits)
        memcpy(out, msg, len + 1);
    free(msg);
    if (!fits)
        return fail(ERROR_INSUFFICIENT_BUFFER);
    return len;
}

static DWORD WINAPI FormatMessageW(DWORD flags, LPCVOID source, DWORD id, DWORD lang, LPWSTR out,
                                   DWORD size, void *args)
{
    char *src = flags & FORMAT_MESSAGE_FROM_STRING ? wide_to_host(source) : NULL;
    char *msg = format(flags, src, id);
    free(src);
    if (!msg)
        return fail(ERROR_INVALID_PARAMETER);
    WCHAR *w = host_to_wide(msg);
    free(msg);
    DWORD len = wstr_len(w);
    if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
        *(WCHAR **)out = w;
        return len;
    }
    bool fits = len < size;
    if (fits)
        memcpy(out, w, (len + 1) * sizeof *w);
    free(w);
    if (!fits)
        return fail(ERROR_INSUFFICIENT_BUFFER);
    return len;
}

static BOOL WINAPI GetLogicalProcessorInformation(PROCESSOR_INFO *out, LPDWORD len)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    DWORD need = n * sizeof *out;
    if (!out || *len < need) {
        *len = need;
        return fail(ERROR_INSUFFICIENT_BUFFER);
    }
    memset(out, 0, need);
    for (long i = 0; i < n; i++) {
        out[i].mask = (ULONG_PTR)1 << i;
        out[i].relationship = 0;
        out[i].flags = 0;
    }
    *len = need;
    return TRUE;
}

static BOOL WINAPI IsWow64Process(HANDLE proc, BOOL *wow)
{
    *wow = FALSE;
    return TRUE;
}

static BOOL WINAPI GetHandleInformation(HANDLE h, LPDWORD flags)
{
    if (flags)
        *flags = 0;
    return TRUE;
}

static BOOL WINAPI SetHandleInformation(HANDLE h, DWORD mask, DWORD flags) { return TRUE; }

static BOOL WINAPI DeviceIoControl(HANDLE dev, DWORD code, void *in, DWORD in_len, void *out, DWORD out_len,
                                   LPDWORD returned, void *overlapped)
{
    if (returned)
        *returned = 0;
    trace("DeviceIoControl code 0x%x is not implemented", code);
    return fail(ERROR_NOT_SUPPORTED);
}

BIT_TABLE(k32_system,
          BIT_FN(GetSystemInfo), BIT_FN(GetNativeSystemInfo),
          BIT_FN(GetVersion), BIT_FN(GetVersionExA), BIT_FN(GetVersionExW),
          BIT_FN(VerifyVersionInfoA), BIT_FN(VerifyVersionInfoW), BIT_FN(VerSetConditionMask),
          BIT_FN(GetProductInfo), BIT_FN(IsProcessorFeaturePresent),
          BIT_FN(GetLastError), BIT_FN(SetLastError), BIT_FN(RestoreLastError),
          BIT_FN(GetComputerNameA), BIT_FN(GetComputerNameW),
          BIT_FN(GetComputerNameExA), BIT_FN(GetComputerNameExW),
          BIT_FN(GetSystemDirectoryA), BIT_FN(GetSystemDirectoryW),
          BIT_FN(GetWindowsDirectoryA), BIT_FN(GetWindowsDirectoryW),
          BIT_FN(GetSystemWindowsDirectoryA), BIT_FN(GetSystemWindowsDirectoryW),
          BIT_FN(IsDebuggerPresent), BIT_FN(CheckRemoteDebuggerPresent),
          BIT_FN(OutputDebugStringA), BIT_FN(OutputDebugStringW), BIT_FN(DebugBreak),
          BIT_FN(SetUnhandledExceptionFilter), BIT_FN(UnhandledExceptionFilter),
          BIT_FN(SetErrorMode), BIT_FN(GetErrorMode), BIT_FN(SetThreadErrorMode),
          BIT_FN(RaiseException), BIT_FN(FormatMessageA), BIT_FN(FormatMessageW),
          BIT_FN(GetLogicalProcessorInformation), BIT_FN(IsWow64Process), BIT_FN(DeviceIoControl),
          BIT_FN(GetHandleInformation), BIT_FN(SetHandleInformation));
