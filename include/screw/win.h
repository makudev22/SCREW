#ifndef SCREW_WIN_H
#define SCREW_WIN_H

#include <stddef.h>
#include <stdint.h>

#define WINAPI __attribute__((ms_abi))

typedef int32_t BOOL;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint32_t UINT;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uint64_t ULONG_PTR;
typedef int64_t LONG_PTR;
typedef uint64_t SIZE_T;
typedef uint64_t DWORD64;
typedef uint16_t WCHAR;
typedef int32_t NTSTATUS;

typedef void *HANDLE;
typedef void *HMODULE;
typedef void *HWND;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef DWORD *LPDWORD;
typedef LONG *PLONG;
typedef void *FARPROC;

#define TRUE 1
#define FALSE 0

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define CURRENT_PROCESS ((HANDLE)(intptr_t)-1)
#define CURRENT_THREAD ((HANDLE)(intptr_t)-2)
#define INFINITE 0xFFFFFFFFu

#define ERROR_SUCCESS 0
#define ERROR_INVALID_FUNCTION 1
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_NO_MORE_FILES 18
#define ERROR_HANDLE_EOF 38
#define ERROR_NOT_SUPPORTED 50
#define ERROR_FILE_EXISTS 80
#define ERROR_INVALID_PARAMETER 87
#define ERROR_BROKEN_PIPE 109
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_MOD_NOT_FOUND 126
#define ERROR_PROC_NOT_FOUND 127
#define ERROR_DIR_NOT_EMPTY 145
#define ERROR_ALREADY_EXISTS 183
#define ERROR_ENVVAR_NOT_FOUND 203
#define ERROR_NO_UNICODE_TRANSLATION 1113

#define WAIT_OBJECT_0 0x00000000u
#define WAIT_TIMEOUT 0x00000102u
#define WAIT_FAILED 0xFFFFFFFFu

typedef struct {
    DWORD low;
    DWORD high;
} FILETIME;

typedef union {
    struct {
        DWORD low;
        LONG high;
    } parts;
    LONGLONG quad;
} LARGE_INTEGER;

#endif
