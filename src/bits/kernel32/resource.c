#include "k32.h"

#include <screw/module.h>

#define ERROR_RESOURCE_DATA_NOT_FOUND 1812

static HANDLE WINAPI FindResourceA(HMODULE mod, LPCSTR name, LPCSTR type)
{
    return fail(ERROR_RESOURCE_DATA_NOT_FOUND), (HANDLE)NULL;
}

static HANDLE WINAPI FindResourceW(HMODULE mod, LPCWSTR name, LPCWSTR type)
{
    return fail(ERROR_RESOURCE_DATA_NOT_FOUND), (HANDLE)NULL;
}

static HANDLE WINAPI FindResourceExA(HMODULE mod, LPCSTR type, LPCSTR name, WORD lang)
{
    return fail(ERROR_RESOURCE_DATA_NOT_FOUND), (HANDLE)NULL;
}

static HANDLE WINAPI FindResourceExW(HMODULE mod, LPCWSTR type, LPCWSTR name, WORD lang)
{
    return fail(ERROR_RESOURCE_DATA_NOT_FOUND), (HANDLE)NULL;
}

static HANDLE WINAPI LoadResource(HMODULE mod, HANDLE res) { return NULL; }
static LPVOID WINAPI LockResource(HANDLE data) { return NULL; }
static DWORD WINAPI SizeofResource(HMODULE mod, HANDLE res) { return 0; }

static BOOL WINAPI EnumResourceNamesA(HMODULE mod, LPCSTR type, void *cb, LONG_PTR param)
{
    return fail(ERROR_RESOURCE_DATA_NOT_FOUND);
}

static BOOL WINAPI EnumResourceNamesW(HMODULE mod, LPCWSTR type, void *cb, LONG_PTR param)
{
    return fail(ERROR_RESOURCE_DATA_NOT_FOUND);
}

static BOOL WINAPI EnumResourceTypesA(HMODULE mod, void *cb, LONG_PTR param)
{
    return fail(ERROR_RESOURCE_DATA_NOT_FOUND);
}

static BOOL WINAPI EnumResourceTypesW(HMODULE mod, void *cb, LONG_PTR param)
{
    return fail(ERROR_RESOURCE_DATA_NOT_FOUND);
}

BIT_TABLE(k32_resource,
          BIT_FN(FindResourceA), BIT_FN(FindResourceW), BIT_FN(FindResourceExA), BIT_FN(FindResourceExW),
          BIT_FN(LoadResource), BIT_FN(LockResource), BIT_FN(SizeofResource),
          BIT_FN(EnumResourceNamesA), BIT_FN(EnumResourceNamesW),
          BIT_FN(EnumResourceTypesA), BIT_FN(EnumResourceTypesW));
