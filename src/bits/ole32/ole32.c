#include <screw/bit.h>
#include <screw/win.h>

#include <stdlib.h>
#include <sys/random.h>

typedef long HRESULT;

#define S_OK 0
#define S_FALSE 1
#define E_NOTIMPL 0x80004001u
#define E_FAIL 0x80004005u
#define E_INVALIDARG 0x80070057u
#define REGDB_E_CLASSNOTREG 0x80040154u

typedef struct {
    DWORD a;
    WORD b, c;
    BYTE d[8];
} GUID;

static HRESULT WINAPI CoInitialize(void *reserved) { return S_OK; }
static HRESULT WINAPI CoInitializeEx(void *reserved, DWORD model) { return S_OK; }
static void WINAPI CoUninitialize(void) {}
static HRESULT WINAPI CoInitializeSecurity(void *a, LONG b, void *c, void *d, DWORD e, DWORD f, void *g, DWORD h,
                                           void *i)
{
    return S_OK;
}

static HRESULT WINAPI CoCreateInstance(const GUID *clsid, void *outer, DWORD ctx, const GUID *iid, void **out)
{
    if (out)
        *out = NULL;
    return (HRESULT)REGDB_E_CLASSNOTREG;
}

static HRESULT WINAPI CoCreateInstanceEx(const GUID *clsid, void *outer, DWORD ctx, void *info, DWORD count,
                                         void *results)
{
    return (HRESULT)REGDB_E_CLASSNOTREG;
}

static HRESULT WINAPI CoGetClassObject(const GUID *clsid, DWORD ctx, void *info, const GUID *iid, void **out)
{
    if (out)
        *out = NULL;
    return (HRESULT)REGDB_E_CLASSNOTREG;
}

static void *WINAPI CoTaskMemAlloc(SIZE_T size) { return malloc(size ? size : 1); }
static void *WINAPI CoTaskMemRealloc(void *p, SIZE_T size) { return realloc(p, size ? size : 1); }
static void WINAPI CoTaskMemFree(void *p) { free(p); }

static HRESULT WINAPI CoGetMalloc(DWORD context, void **out)
{
    if (out)
        *out = NULL;
    return (HRESULT)E_NOTIMPL;
}

static HRESULT WINAPI CoCreateGuid(GUID *out)
{
    if (!out)
        return (HRESULT)E_INVALIDARG;
    if (getrandom(out, sizeof *out, 0) != (ssize_t)sizeof *out)
        return (HRESULT)E_FAIL;
    out->c = (out->c & 0x0FFF) | 0x4000;
    out->d[0] = (out->d[0] & 0x3F) | 0x80;
    return S_OK;
}

static void WINAPI CoFreeUnusedLibraries(void) {}
static HRESULT WINAPI OleInitialize(void *reserved) { return S_OK; }
static void WINAPI OleUninitialize(void) {}
static HRESULT WINAPI CoRegisterMessageFilter(void *filter, void **old)
{
    if (old)
        *old = NULL;
    return S_OK;
}

static HRESULT WINAPI PropVariantClear(void *pv) { return S_OK; }
static HRESULT WINAPI CoInitializeWOW(DWORD a, DWORD b) { return S_OK; }

BIT_TABLE(ole32_exports,
          BIT_FN(CoInitialize), BIT_FN(CoInitializeEx), BIT_FN(CoUninitialize), BIT_FN(CoInitializeSecurity),
          BIT_FN(CoCreateInstance), BIT_FN(CoCreateInstanceEx), BIT_FN(CoGetClassObject),
          BIT_FN(CoTaskMemAlloc), BIT_FN(CoTaskMemRealloc), BIT_FN(CoTaskMemFree), BIT_FN(CoGetMalloc),
          BIT_FN(CoCreateGuid), BIT_FN(CoFreeUnusedLibraries), BIT_FN(CoRegisterMessageFilter),
          BIT_FN(OleInitialize), BIT_FN(OleUninitialize), BIT_FN(PropVariantClear), BIT_FN(CoInitializeWOW));

BIT_DEFINE(bit_ole32, "ole32.dll", &ole32_exports);
