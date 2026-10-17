#include "k32.h"

DWORD copy_out_a(const char *src, LPSTR dst, DWORD size)
{
    DWORD len = strlen(src);
    if (!dst || size <= len) {
        last_error_set(ERROR_INSUFFICIENT_BUFFER);
        return len + 1;
    }
    memcpy(dst, src, len + 1);
    return len;
}

DWORD copy_out_w(const char *src, LPWSTR dst, DWORD size)
{
    int need = utf8_to_utf16(src, -1, NULL, 0);
    if (!dst || size < (DWORD)need) {
        last_error_set(ERROR_INSUFFICIENT_BUFFER);
        return need;
    }
    utf8_to_utf16(src, -1, dst, size);
    return need - 1;
}

BIT_DEFINE(bit_kernel32, "kernel32.dll",
           &k32_module, &k32_file, &k32_find, &k32_console, &k32_memory, &k32_thread,
           &k32_sync, &k32_time, &k32_system, &k32_env, &k32_string, &k32_proc);
