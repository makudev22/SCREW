#ifndef SCREW_K32_H
#define SCREW_K32_H

#include <screw/bit.h>
#include <screw/handle.h>
#include <screw/path.h>
#include <screw/teb.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const struct bit_table k32_module;
extern const struct bit_table k32_file;
extern const struct bit_table k32_find;
extern const struct bit_table k32_console;
extern const struct bit_table k32_memory;
extern const struct bit_table k32_thread;
extern const struct bit_table k32_sync;
extern const struct bit_table k32_time;
extern const struct bit_table k32_system;
extern const struct bit_table k32_env;
extern const struct bit_table k32_string;
extern const struct bit_table k32_proc;

static inline BOOL fail(DWORD error)
{
    last_error_set(error);
    return FALSE;
}

static inline BOOL fail_errno(void)
{
    return fail(path_errno_to_error(errno));
}

#define UNIX_EPOCH_TICKS 116444736000000000ull

static inline uint64_t ticks_from_timespec(const struct timespec *ts)
{
    return UNIX_EPOCH_TICKS + ts->tv_sec * 10000000ull + ts->tv_nsec / 100;
}

static inline FILETIME filetime_from_ticks(uint64_t t)
{
    return (FILETIME){ (DWORD)t, (DWORD)(t >> 32) };
}

static inline uint64_t ticks_from_filetime(const FILETIME *ft)
{
    return ((uint64_t)ft->high << 32) | ft->low;
}

struct stat;
DWORD attributes_of(const char *host, const struct stat *st);

DWORD copy_out_a(const char *src, LPSTR dst, DWORD size);
DWORD copy_out_w(const char *src, LPWSTR dst, DWORD size);

#endif
