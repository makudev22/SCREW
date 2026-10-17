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

DWORD copy_out_a(const char *src, LPSTR dst, DWORD size);
DWORD copy_out_w(const char *src, LPWSTR dst, DWORD size);

#endif
