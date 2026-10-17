#include <screw/bit.h>
#include <screw/teb.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DIGCF_PRESENT 0x2
#define DIGCF_DEVICEINTERFACE 0x10
#define ERROR_NO_MORE_ITEMS_ 259
#define ERROR_INSUFFICIENT_BUFFER_ 122

#define DEVICE_PATH u"\\\\?\\screw#display#0#{generic}"

typedef void *HDEVINFO;

typedef struct {
    DWORD a;
    WORD b, c;
    BYTE d[8];
} GUID;

typedef struct {
    DWORD size;
    GUID class_guid;
    DWORD flags;
    ULONG_PTR reserved;
} SP_DEVICE_INTERFACE_DATA;

typedef struct {
    DWORD size;
    GUID class_guid;
    DWORD dev_inst;
    ULONG_PTR reserved;
} SP_DEVINFO_DATA;

typedef struct {
    DWORD size;
    WCHAR device_path[1];
} SP_DEVICE_INTERFACE_DETAIL_DATA_W;

static const char info_set[] = "screw-devinfo";

static HDEVINFO WINAPI SetupDiGetClassDevsW(const GUID *cls, const WCHAR *enumerator, HWND parent, DWORD flags)
{
    return (HDEVINFO)info_set;
}

static HDEVINFO WINAPI SetupDiGetClassDevsA(const GUID *cls, LPCSTR enumerator, HWND parent, DWORD flags)
{
    return (HDEVINFO)info_set;
}

static BOOL WINAPI SetupDiEnumDeviceInterfaces(HDEVINFO set, SP_DEVINFO_DATA *dev, const GUID *cls, DWORD index,
                                               SP_DEVICE_INTERFACE_DATA *out)
{
    if (set != (HDEVINFO)info_set || index > 0) {
        last_error_set(ERROR_NO_MORE_ITEMS_);
        return FALSE;
    }
    if (out) {
        if (cls)
            out->class_guid = *cls;
        out->flags = DIGCF_PRESENT;
        out->reserved = 0;
    }
    return TRUE;
}

static BOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HDEVINFO set, SP_DEVICE_INTERFACE_DATA *iface,
                                                    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail, DWORD size,
                                                    DWORD *required, SP_DEVINFO_DATA *dev)
{
    static const WCHAR path[] = DEVICE_PATH;
    DWORD path_bytes = (DWORD)(sizeof path);
    DWORD need = (DWORD)offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, device_path) + path_bytes;
    if (required)
        *required = need;
    if (!detail)
        return size ? TRUE : FALSE;
    if (size < need) {
        last_error_set(ERROR_INSUFFICIENT_BUFFER_);
        return FALSE;
    }
    memcpy(detail->device_path, path, path_bytes);
    if (dev && dev->size >= sizeof *dev)
        dev->dev_inst = 1;
    return TRUE;
}

static HANDLE WINAPI SetupDiOpenDevRegKey(HDEVINFO set, SP_DEVINFO_DATA *dev, DWORD scope, DWORD profile,
                                          DWORD type, DWORD access)
{
    return INVALID_HANDLE_VALUE;
}

static BOOL WINAPI SetupDiEnumDeviceInfo(HDEVINFO set, DWORD index, SP_DEVINFO_DATA *out)
{
    if (set != (HDEVINFO)info_set || index > 0) {
        last_error_set(ERROR_NO_MORE_ITEMS_);
        return FALSE;
    }
    if (out)
        out->dev_inst = 1;
    return TRUE;
}

static BOOL WINAPI SetupDiDestroyDeviceInfoList(HDEVINFO set) { return set == (HDEVINFO)info_set; }

BIT_TABLE(setupapi_exports,
          BIT_FN(SetupDiGetClassDevsW), BIT_FN(SetupDiGetClassDevsA),
          BIT_FN(SetupDiEnumDeviceInterfaces), BIT_FN(SetupDiGetDeviceInterfaceDetailW),
          BIT_FN(SetupDiOpenDevRegKey), BIT_FN(SetupDiEnumDeviceInfo),
          BIT_FN(SetupDiDestroyDeviceInfoList));

BIT_DEFINE(bit_setupapi, "setupapi.dll", &setupapi_exports);
