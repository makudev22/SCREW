#include <screw/bit.h>
#include <screw/teb.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <errno.h>
#include <malloc.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_NO_MEMORY ((NTSTATUS)0xC0000017)
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023)
#define STATUS_TIMEOUT ((NTSTATUS)0x00000102)

#define UNIX_EPOCH_TICKS 116444736000000000ull

typedef struct {
    WORD length;
    WORD max_length;
    WCHAR *buffer;
} UNICODE_STRING;

typedef struct {
    WORD length;
    WORD max_length;
    char *buffer;
} ANSI_STRING;

typedef struct {
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platform;
    WCHAR service_pack[128];
    WORD sp_major;
    WORD sp_minor;
    WORD suite;
    BYTE product;
    BYTE reserved;
} OS_VERSION_INFO_EX;

static void WINAPI RtlInitUnicodeString(UNICODE_STRING *s, const WCHAR *src)
{
    size_t n = src ? wstr_len(src) * 2 : 0;
    if (n > 0xFFFC)
        n = 0xFFFC;
    s->length = n;
    s->max_length = src ? n + 2 : 0;
    s->buffer = (WCHAR *)src;
}

static void WINAPI RtlInitAnsiString(ANSI_STRING *s, const char *src)
{
    size_t n = src ? strlen(src) : 0;
    if (n > 0xFFFE)
        n = 0xFFFE;
    s->length = n;
    s->max_length = src ? n + 1 : 0;
    s->buffer = (char *)src;
}

static void WINAPI RtlFreeUnicodeString(UNICODE_STRING *s)
{
    free(s->buffer);
    s->buffer = NULL;
    s->length = s->max_length = 0;
}

static void WINAPI RtlFreeAnsiString(ANSI_STRING *s)
{
    free(s->buffer);
    s->buffer = NULL;
    s->length = s->max_length = 0;
}

static NTSTATUS WINAPI RtlAnsiStringToUnicodeString(UNICODE_STRING *dst, const ANSI_STRING *src, BOOL alloc)
{
    int n = utf8_to_utf16(src->buffer, src->length, NULL, 0);
    if (alloc) {
        dst->buffer = malloc((n + 1) * 2);
        dst->max_length = (n + 1) * 2;
    } else if ((n + 1) * 2 > dst->max_length) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    utf8_to_utf16(src->buffer, src->length, dst->buffer, n);
    dst->buffer[n] = 0;
    dst->length = n * 2;
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI RtlUnicodeStringToAnsiString(ANSI_STRING *dst, const UNICODE_STRING *src, BOOL alloc)
{
    int n = utf16_to_utf8(src->buffer, src->length / 2, NULL, 0);
    if (alloc) {
        dst->buffer = malloc(n + 1);
        dst->max_length = n + 1;
    } else if (n + 1 > dst->max_length) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    utf16_to_utf8(src->buffer, src->length / 2, dst->buffer, n);
    dst->buffer[n] = 0;
    dst->length = n;
    return STATUS_SUCCESS;
}

static BOOL WINAPI RtlEqualUnicodeString(const UNICODE_STRING *a, const UNICODE_STRING *b, BOOL ci)
{
    if (a->length != b->length)
        return FALSE;
    for (size_t i = 0; i < a->length / 2u; i++) {
        WCHAR x = a->buffer[i], y = b->buffer[i];
        if (ci && x < 0x80 && y < 0x80) {
            x = x >= 'a' && x <= 'z' ? x - 32 : x;
            y = y >= 'a' && y <= 'z' ? y - 32 : y;
        }
        if (x != y)
            return FALSE;
    }
    return TRUE;
}

static void *WINAPI RtlAllocateHeap(HANDLE heap, ULONG flags, SIZE_T size)
{
    return flags & 0x08 ? calloc(1, size ? size : 1) : malloc(size ? size : 1);
}

static BOOL WINAPI RtlFreeHeap(HANDLE heap, ULONG flags, void *p)
{
    free(p);
    return TRUE;
}

static void *WINAPI RtlReAllocateHeap(HANDLE heap, ULONG flags, void *p, SIZE_T size)
{
    size_t old = p ? malloc_usable_size(p) : 0;
    void *r = realloc(p, size ? size : 1);
    if (r && (flags & 0x08) && size > old)
        memset((char *)r + old, 0, size - old);
    return r;
}

static SIZE_T WINAPI RtlSizeHeap(HANDLE heap, ULONG flags, const void *p)
{
    return malloc_usable_size((void *)p);
}

static void WINAPI RtlMoveMemory(void *d, const void *s, SIZE_T n) { memmove(d, s, n); }
static void WINAPI RtlFillMemory(void *d, SIZE_T n, BYTE v) { memset(d, v, n); }
static void WINAPI RtlZeroMemory(void *d, SIZE_T n) { memset(d, 0, n); }

static SIZE_T WINAPI RtlCompareMemory(const void *a, const void *b, SIZE_T n)
{
    const BYTE *x = a, *y = b;
    SIZE_T i = 0;
    while (i < n && x[i] == y[i])
        i++;
    return i;
}

static NTSTATUS WINAPI RtlGetVersion(OS_VERSION_INFO_EX *v)
{
    v->major = 10;
    v->minor = 0;
    v->build = 19045;
    v->platform = 2;
    memset(v->service_pack, 0, sizeof v->service_pack);
    if (v->size >= sizeof *v) {
        v->sp_major = v->sp_minor = 0;
        v->suite = 0x100;
        v->product = 1;
    }
    return STATUS_SUCCESS;
}

static void WINAPI RtlGetNtVersionNumbers(ULONG *major, ULONG *minor, ULONG *build)
{
    if (major)
        *major = 10;
    if (minor)
        *minor = 0;
    if (build)
        *build = 0xF0000000u | 19045;
}

static struct teb *WINAPI NtCurrentTeb(void) { return teb_current(); }

static ULONG WINAPI RtlGetLastWin32Error(void) { return last_error_get(); }
static void WINAPI RtlSetLastWin32Error(ULONG e) { last_error_set(e); }

static ULONG WINAPI RtlNtStatusToDosError(NTSTATUS s)
{
    switch ((uint32_t)s) {
    case 0: return ERROR_SUCCESS;
    case 0xC000000D: return ERROR_INVALID_PARAMETER;
    case 0xC0000017: return ERROR_NOT_ENOUGH_MEMORY;
    case 0xC0000023: return ERROR_INSUFFICIENT_BUFFER;
    case 0xC0000008: return ERROR_INVALID_HANDLE;
    case 0xC0000022: return ERROR_ACCESS_DENIED;
    case 0xC0000034: return ERROR_FILE_NOT_FOUND;
    case 0xC000003A: return ERROR_PATH_NOT_FOUND;
    case 0xC0000035: return ERROR_ALREADY_EXISTS;
    case 0xC0000002: return ERROR_NOT_SUPPORTED;
    default: return ERROR_INVALID_FUNCTION;
    }
}

static NTSTATUS WINAPI NtQuerySystemTime(LARGE_INTEGER *t)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    t->quad = UNIX_EPOCH_TICKS + ts.tv_sec * 10000000ull + ts.tv_nsec / 100;
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI NtQueryPerformanceCounter(LARGE_INTEGER *counter, LARGE_INTEGER *freq)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    counter->quad = ts.tv_sec * 10000000ll + ts.tv_nsec / 100;
    if (freq)
        freq->quad = 10000000;
    return STATUS_SUCCESS;
}

static BOOL WINAPI RtlQueryPerformanceCounter(LARGE_INTEGER *c) { return NtQueryPerformanceCounter(c, NULL) == 0; }

static BOOL WINAPI RtlQueryPerformanceFrequency(LARGE_INTEGER *f)
{
    f->quad = 10000000;
    return TRUE;
}

static NTSTATUS WINAPI NtDelayExecution(BOOL alertable, const LARGE_INTEGER *timeout)
{
    int64_t t = timeout->quad;
    if (t > 0) {
        LARGE_INTEGER now;
        NtQuerySystemTime(&now);
        t = now.quad - t;
    }
    int64_t ticks = -t;
    if (ticks <= 0) {
        sched_yield();
        return STATUS_SUCCESS;
    }
    struct timespec ts = { ticks / 10000000, (ticks % 10000000) * 100 };
    while (nanosleep(&ts, &ts) && errno == EINTR)
        ;
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI NtYieldExecution(void)
{
    sched_yield();
    return STATUS_SUCCESS;
}

static ULONG WINAPI RtlRandomEx(ULONG *seed)
{
    *seed = *seed * 0x7FFFFFED + 0x7FFFFFC3;
    return *seed % 0x7FFFFFFF;
}

static BOOL WINAPI RtlGenRandom(void *buf, ULONG n)
{
    return getrandom(buf, n, 0) == (ssize_t)n;
}

static BYTE WINAPI RtlDllShutdownInProgress(void) { return 0; }

static WORD WINAPI RtlCaptureStackBackTrace(ULONG skip, ULONG count, void **frames, ULONG *hash)
{
    if (hash)
        *hash = 0;
    return 0;
}

BIT_TABLE(nt_rtl,
          BIT_FN(RtlInitUnicodeString), BIT_FN(RtlInitAnsiString),
          BIT_FN(RtlFreeUnicodeString), BIT_FN(RtlFreeAnsiString),
          BIT_FN(RtlAnsiStringToUnicodeString), BIT_FN(RtlUnicodeStringToAnsiString),
          BIT_FN(RtlEqualUnicodeString),
          BIT_FN(RtlAllocateHeap), BIT_FN(RtlFreeHeap), BIT_FN(RtlReAllocateHeap), BIT_FN(RtlSizeHeap),
          BIT_FN(RtlMoveMemory), BIT_AS("RtlCopyMemory", RtlMoveMemory), BIT_FN(RtlFillMemory),
          BIT_FN(RtlZeroMemory), BIT_FN(RtlCompareMemory),
          BIT_FN(RtlGetVersion), BIT_FN(RtlGetNtVersionNumbers), BIT_FN(NtCurrentTeb),
          BIT_FN(RtlGetLastWin32Error), BIT_FN(RtlSetLastWin32Error), BIT_FN(RtlNtStatusToDosError),
          BIT_FN(NtQuerySystemTime), BIT_FN(NtQueryPerformanceCounter),
          BIT_FN(RtlQueryPerformanceCounter), BIT_FN(RtlQueryPerformanceFrequency),
          BIT_FN(NtDelayExecution), BIT_FN(NtYieldExecution),
          BIT_FN(RtlRandomEx), BIT_AS("RtlRandom", RtlRandomEx), BIT_AS("SystemFunction036", RtlGenRandom),
          BIT_FN(RtlCaptureStackBackTrace), BIT_FN(RtlDllShutdownInProgress));

BIT_DEFINE(bit_ntdll, "ntdll.dll", &nt_rtl);
