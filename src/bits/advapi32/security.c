#include "adv.h"

#include <pwd.h>
#include <sys/random.h>
#include <unistd.h>

#define TOKEN_ELEVATION_CLASS 20
#define PROV_RSA_FULL 1

struct token {
    struct object base;
};

static void token_destroy(struct object *o) { free(o); }

static const char *user_name(void)
{
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_name : "user";
}

static BOOL WINAPI GetUserNameA(LPSTR out, LPDWORD size)
{
    const char *u = user_name();
    DWORD n = strlen(u) + 1;
    if (!out || *size < n) {
        *size = n;
        last_error_set(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(out, u, n);
    *size = n;
    return TRUE;
}

static BOOL WINAPI GetUserNameW(LPWSTR out, LPDWORD size)
{
    const char *u = user_name();
    DWORD n = utf8_to_utf16(u, -1, NULL, 0);
    if (!out || *size < n) {
        *size = n;
        last_error_set(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    utf8_to_utf16(u, -1, out, n);
    *size = n;
    return TRUE;
}

static BOOL WINAPI SystemFunction036(void *buf, ULONG n)
{
    return getrandom(buf, n, 0) == (ssize_t)n;
}

static BOOL WINAPI CryptAcquireContextA(ULONG_PTR *prov, LPCSTR container, LPCSTR provider, DWORD type, DWORD flags)
{
    *prov = PROV_RSA_FULL;
    return TRUE;
}

static BOOL WINAPI CryptAcquireContextW(ULONG_PTR *prov, LPCWSTR container, LPCWSTR provider, DWORD type, DWORD flags)
{
    *prov = PROV_RSA_FULL;
    return TRUE;
}

static BOOL WINAPI CryptReleaseContext(ULONG_PTR prov, DWORD flags) { return TRUE; }

static BOOL WINAPI CryptGenRandom(ULONG_PTR prov, DWORD n, BYTE *buf) { return SystemFunction036(buf, n); }

static BOOL WINAPI OpenProcessToken(HANDLE proc, DWORD access, HANDLE *out)
{
    struct token *t = calloc(1, sizeof *t);
    object_init(&t->base, OBJ_ANY, token_destroy);
    *out = handle_new(&t->base);
    return TRUE;
}

static BOOL WINAPI OpenThreadToken(HANDLE thread, DWORD access, BOOL self, HANDLE *out)
{
    return OpenProcessToken(NULL, access, out);
}

static BOOL WINAPI GetTokenInformation(HANDLE token, int cls, void *info, DWORD len, DWORD *ret)
{
    if (cls != TOKEN_ELEVATION_CLASS) {
        last_error_set(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (ret)
        *ret = sizeof(DWORD);
    if (len < sizeof(DWORD)) {
        last_error_set(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    *(DWORD *)info = geteuid() == 0;
    return TRUE;
}

static ULONG WINAPI EventRegister(const void *guid, void *cb, void *ctx, uint64_t *handle)
{
    *handle = 0;
    return ERROR_SUCCESS;
}

static ULONG WINAPI EventUnregister(uint64_t handle) { return ERROR_SUCCESS; }
static ULONG WINAPI EventWriteTransfer(uint64_t h, const void *desc, const void *a, const void *b, ULONG n, void *data) { return ERROR_SUCCESS; }
static BOOL WINAPI EventEnabled(uint64_t h, const void *desc) { return FALSE; }

BIT_TABLE(adv_security,
          BIT_FN(GetUserNameA), BIT_FN(GetUserNameW), BIT_FN(SystemFunction036),
          BIT_FN(CryptAcquireContextA), BIT_FN(CryptAcquireContextW), BIT_FN(CryptReleaseContext),
          BIT_FN(CryptGenRandom),
          BIT_FN(OpenProcessToken), BIT_FN(OpenThreadToken), BIT_FN(GetTokenInformation),
          BIT_FN(EventRegister), BIT_FN(EventUnregister), BIT_FN(EventWriteTransfer), BIT_FN(EventEnabled));

BIT_DEFINE(bit_advapi32, "advapi32.dll", &adv_registry, &adv_security, &adv_notify);
