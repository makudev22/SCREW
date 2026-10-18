#include <screw/bit.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <stdlib.h>
#include <string.h>

typedef long HRESULT;
typedef WCHAR *BSTR;
#define S_OK 0
#define E_NOTIMPL 0x80004001u

static BSTR alloc_bstr(const WCHAR *src, uint32_t chars)
{
    uint32_t bytes = chars * sizeof(WCHAR);
    uint8_t *raw = malloc(sizeof(uint32_t) + bytes + sizeof(WCHAR));
    if (!raw)
        return NULL;
    *(uint32_t *)raw = bytes;
    BSTR s = (BSTR)(raw + sizeof(uint32_t));
    if (src)
        memcpy(s, src, bytes);
    s[chars] = 0;
    return s;
}

static BSTR WINAPI SysAllocString(const WCHAR *src) { return alloc_bstr(src, src ? wstr_len(src) : 0); }
static BSTR WINAPI SysAllocStringLen(const WCHAR *src, UINT len) { return alloc_bstr(src, len); }

static BSTR WINAPI SysAllocStringByteLen(const char *src, UINT bytes)
{
    uint8_t *raw = malloc(sizeof(uint32_t) + bytes + sizeof(WCHAR));
    if (!raw)
        return NULL;
    *(uint32_t *)raw = bytes;
    BSTR s = (BSTR)(raw + sizeof(uint32_t));
    if (src)
        memcpy(s, src, bytes);
    s[bytes / sizeof(WCHAR)] = 0;
    return s;
}

static void WINAPI SysFreeString(BSTR s)
{
    if (s)
        free((uint8_t *)s - sizeof(uint32_t));
}

static UINT WINAPI SysStringLen(BSTR s) { return s ? *(uint32_t *)((uint8_t *)s - sizeof(uint32_t)) / sizeof(WCHAR) : 0; }
static UINT WINAPI SysStringByteLen(BSTR s) { return s ? *(uint32_t *)((uint8_t *)s - sizeof(uint32_t)) : 0; }

static int WINAPI SysReAllocString(BSTR *dst, const WCHAR *src)
{
    if (!dst)
        return 0;
    SysFreeString(*dst);
    *dst = SysAllocString(src);
    return 1;
}

static void WINAPI VariantInit(void *v)
{
    if (v)
        memset(v, 0, 24);
}

static HRESULT WINAPI VariantClear(void *v)
{
    if (v)
        memset(v, 0, 24);
    return S_OK;
}

static HRESULT WINAPI VariantCopy(void *dst, const void *src)
{
    if (dst && src)
        memcpy(dst, src, 24);
    return S_OK;
}

static HRESULT WINAPI LoadTypeLib(const WCHAR *file, void **out)
{
    if (out)
        *out = NULL;
    return (HRESULT)E_NOTIMPL;
}

static HRESULT WINAPI VariantChangeType(void *dst, const void *src, WORD flags, WORD vt)
{
    return (HRESULT)E_NOTIMPL;
}

BIT_TABLE(oleaut32_exports,
          BIT_FN(SysAllocString), BIT_FN(SysAllocStringLen), BIT_FN(SysAllocStringByteLen),
          BIT_FN(SysFreeString), BIT_FN(SysStringLen), BIT_FN(SysStringByteLen), BIT_FN(SysReAllocString),
          BIT_FN(VariantInit), BIT_FN(VariantClear), BIT_FN(VariantCopy), BIT_FN(VariantChangeType),
          BIT_FN(LoadTypeLib));

BIT_DEFINE(bit_oleaut32, "oleaut32.dll", &oleaut32_exports);
