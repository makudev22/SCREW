#include <screw/bit.h>
#include <screw/win.h>

#define NO_ERROR 0
#define ERROR_NO_DATA 232
#define ERROR_NOT_SUPPORTED 50

static DWORD WINAPI GetAdaptersInfo(void *info, DWORD *size)
{
    if (size)
        *size = 0;
    return ERROR_NO_DATA;
}

static DWORD WINAPI GetAdaptersAddresses(DWORD family, DWORD flags, void *reserved, void *addrs, DWORD *size)
{
    if (size)
        *size = 0;
    return ERROR_NO_DATA;
}

static DWORD WINAPI GetNetworkParams(void *info, DWORD *size)
{
    if (size)
        *size = 0;
    return ERROR_NO_DATA;
}

static DWORD WINAPI GetBestInterface(DWORD addr, DWORD *index)
{
    if (index)
        *index = 0;
    return NO_ERROR;
}

static DWORD WINAPI GetIpAddrTable(void *table, DWORD *size, BOOL order)
{
    if (size)
        *size = 0;
    return ERROR_NO_DATA;
}

BIT_TABLE(iphlpapi_exports,
          BIT_FN(GetAdaptersInfo), BIT_FN(GetAdaptersAddresses), BIT_FN(GetNetworkParams),
          BIT_FN(GetBestInterface), BIT_FN(GetIpAddrTable));

BIT_DEFINE(bit_iphlpapi, "iphlpapi.dll", &iphlpapi_exports);
