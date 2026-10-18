#include "u32.h"

#include <stdio.h>

#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXVSCROLL 2
#define SM_CYHSCROLL 3
#define SM_CYCAPTION 4
#define SM_CXBORDER 5
#define SM_CYBORDER 6
#define SM_CXDLGFRAME 7
#define SM_CYDLGFRAME 8
#define SM_CXICON 11
#define SM_CYICON 12
#define SM_CXCURSOR 13
#define SM_CYCURSOR 14
#define SM_CYMENU 15
#define SM_CXFULLSCREEN 16
#define SM_CYFULLSCREEN 17
#define SM_MOUSEPRESENT 19
#define SM_CXMIN 28
#define SM_CYMIN 29
#define SM_CXFRAME 32
#define SM_CYFRAME 33
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
#define SM_CXSMICON 49
#define SM_CYSMICON 50
#define SM_CMOUSEBUTTONS 43
#define SM_XVIRTUALSCREEN 76
#define SM_YVIRTUALSCREEN 77
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#define SM_CMONITORS 80
#define SM_SAMEDISPLAYFORMAT 81
#define SM_CXMAXIMIZED 61
#define SM_CYMAXIMIZED 62
#define SM_MOUSEWHEELPRESENT 75
#define SM_REMOTESESSION 0x1000

#define SPI_GETWORKAREA 0x0030
#define SPI_GETSCREENSAVEACTIVE 0x0010
#define SPI_SETSCREENSAVEACTIVE 0x0011
#define SPI_GETMOUSESPEED 0x0070
#define SPI_GETWHEELSCROLLLINES 0x0068

#define MONITORINFOF_PRIMARY 1
#define PRIMARY_MONITOR ((HMONITOR)(uintptr_t)0x12340001)

#define DISP_CHANGE_SUCCESSFUL 0
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#define ENUM_REGISTRY_SETTINGS ((DWORD)-2)

#define DM_BITSPERPEL 0x00040000
#define DM_PELSWIDTH 0x00080000
#define DM_PELSHEIGHT 0x00100000
#define DM_DISPLAYFREQUENCY 0x00400000

#define DISPLAY_DEVICE_ATTACHED_TO_DESKTOP 0x1
#define DISPLAY_DEVICE_PRIMARY_DEVICE 0x4

#define USER_DEFAULT_DPI 96

typedef struct {
    DWORD size;
    RECT monitor;
    RECT work;
    DWORD flags;
} MONITORINFO;

typedef struct {
    DWORD size;
    RECT monitor;
    RECT work;
    DWORD flags;
    char device[32];
} MONITORINFOEXA;

typedef struct {
    DWORD size;
    RECT monitor;
    RECT work;
    DWORD flags;
    WCHAR device[32];
} MONITORINFOEXW;

typedef struct {
    char device_name[32];
    WORD spec_version;
    WORD driver_version;
    WORD size;
    WORD driver_extra;
    DWORD fields;
    LONG position_x;
    LONG position_y;
    DWORD orientation;
    DWORD fixed_output;
    short color;
    short duplex;
    short y_resolution;
    short tt_option;
    short collate;
    char form_name[32];
    WORD log_pixels;
    DWORD bits_per_pel;
    DWORD pels_width;
    DWORD pels_height;
    DWORD display_flags;
    DWORD display_frequency;
    DWORD icm[8];
} DEVMODEA;

typedef struct {
    WCHAR device_name[32];
    WORD spec_version;
    WORD driver_version;
    WORD size;
    WORD driver_extra;
    DWORD fields;
    LONG position_x;
    LONG position_y;
    DWORD orientation;
    DWORD fixed_output;
    short color;
    short duplex;
    short y_resolution;
    short tt_option;
    short collate;
    WCHAR form_name[32];
    WORD log_pixels;
    DWORD bits_per_pel;
    DWORD pels_width;
    DWORD pels_height;
    DWORD display_flags;
    DWORD display_frequency;
    DWORD icm[8];
} DEVMODEW;

typedef struct {
    DWORD size;
    char name[32];
    char string[128];
    DWORD flags;
    char id[128];
    char key[128];
} DISPLAY_DEVICEA;

typedef struct {
    DWORD size;
    WCHAR name[32];
    WCHAR string[128];
    DWORD flags;
    WCHAR id[128];
    WCHAR key[128];
} DISPLAY_DEVICEW;

typedef BOOL WINAPI (*MONITORENUMPROC)(HMONITOR, HDC, RECT *, LPARAM);

static const char device_name[] = "\\\\.\\DISPLAY1";

static void screen(int *w, int *h) { display_size(w, h); }

static int WINAPI GetSystemMetrics(int index)
{
    int w, h;
    screen(&w, &h);
    switch (index) {
    case SM_CXSCREEN:
    case SM_CXFULLSCREEN:
    case SM_CXVIRTUALSCREEN:
    case SM_CXMAXIMIZED: return w;
    case SM_CYSCREEN:
    case SM_CYFULLSCREEN:
    case SM_CYVIRTUALSCREEN:
    case SM_CYMAXIMIZED: return h;
    case SM_CXVSCROLL:
    case SM_CYHSCROLL: return 17;
    case SM_CYCAPTION: return 23;
    case SM_CXBORDER:
    case SM_CYBORDER: return 1;
    case SM_CXDLGFRAME:
    case SM_CYDLGFRAME: return 3;
    case SM_CXFRAME:
    case SM_CYFRAME: return 4;
    case SM_CXICON:
    case SM_CYICON:
    case SM_CXCURSOR:
    case SM_CYCURSOR: return 32;
    case SM_CXSMICON:
    case SM_CYSMICON: return 16;
    case SM_CYMENU: return 20;
    case SM_CXMIN: return 136;
    case SM_CYMIN: return 39;
    case SM_CXDOUBLECLK:
    case SM_CYDOUBLECLK: return 4;
    case SM_MOUSEPRESENT:
    case SM_MOUSEWHEELPRESENT:
    case SM_CMONITORS:
    case SM_SAMEDISPLAYFORMAT: return 1;
    case SM_CMOUSEBUTTONS: return 3;
    case SM_XVIRTUALSCREEN:
    case SM_YVIRTUALSCREEN:
    case SM_REMOTESESSION: return 0;
    default: return 0;
    }
}

static int WINAPI GetSystemMetricsForDpi(int index, UINT dpi) { return GetSystemMetrics(index); }

static BOOL WINAPI SystemParametersInfoA(UINT action, UINT param, void *out, UINT ini)
{
    int w, h;
    screen(&w, &h);
    switch (action) {
    case SPI_GETWORKAREA:
        *(RECT *)out = (RECT){ 0, 0, w, h };
        return TRUE;
    case SPI_GETSCREENSAVEACTIVE:
        *(BOOL *)out = FALSE;
        return TRUE;
    case SPI_SETSCREENSAVEACTIVE:
        return TRUE;
    case SPI_GETMOUSESPEED:
        *(int *)out = 10;
        return TRUE;
    case SPI_GETWHEELSCROLLLINES:
        *(UINT *)out = 3;
        return TRUE;
    default:
        return fail_win(ERROR_INVALID_PARAMETER);
    }
}

static BOOL WINAPI SystemParametersInfoW(UINT action, UINT param, void *out, UINT ini)
{
    return SystemParametersInfoA(action, param, out, ini);
}

static HMONITOR WINAPI MonitorFromWindow(HWND h, DWORD flags) { return PRIMARY_MONITOR; }
static HMONITOR WINAPI MonitorFromPoint(POINT p, DWORD flags) { return PRIMARY_MONITOR; }
static HMONITOR WINAPI MonitorFromRect(const RECT *r, DWORD flags) { return PRIMARY_MONITOR; }

static BOOL monitor_info(HMONITOR m, MONITORINFO *info)
{
    if (m != PRIMARY_MONITOR)
        return fail_win(ERROR_INVALID_PARAMETER);
    int w, h;
    screen(&w, &h);
    info->monitor = (RECT){ 0, 0, w, h };
    info->work = info->monitor;
    info->flags = MONITORINFOF_PRIMARY;
    return TRUE;
}

static BOOL WINAPI GetMonitorInfoA(HMONITOR m, MONITORINFO *info)
{
    if (!monitor_info(m, info))
        return FALSE;
    if (info->size >= sizeof(MONITORINFOEXA))
        snprintf(((MONITORINFOEXA *)info)->device, 32, "%s", device_name);
    return TRUE;
}

static BOOL WINAPI GetMonitorInfoW(HMONITOR m, MONITORINFO *info)
{
    if (!monitor_info(m, info))
        return FALSE;
    if (info->size >= sizeof(MONITORINFOEXW))
        utf8_to_utf16(device_name, -1, ((MONITORINFOEXW *)info)->device, 32);
    return TRUE;
}

static BOOL WINAPI EnumDisplayMonitors(HDC dc, const RECT *clip, MONITORENUMPROC proc, LPARAM data)
{
    int w, h;
    screen(&w, &h);
    RECT r = { 0, 0, w, h };
    return proc(PRIMARY_MONITOR, dc, &r, data);
}

static void fill_mode(DWORD *fields, DWORD *bpp, DWORD *width, DWORD *height, DWORD *freq, WORD *size, WORD dsize)
{
    int w, h;
    screen(&w, &h);
    *fields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    *bpp = 32;
    *width = w;
    *height = h;
    *freq = 60;
    *size = dsize;
}

static BOOL WINAPI EnumDisplaySettingsA(LPCSTR dev, DWORD mode, DEVMODEA *dm)
{
    if (mode != ENUM_CURRENT_SETTINGS && mode != ENUM_REGISTRY_SETTINGS && mode > 0)
        return FALSE;
    fill_mode(&dm->fields, &dm->bits_per_pel, &dm->pels_width, &dm->pels_height, &dm->display_frequency,
              &dm->size, sizeof *dm);
    snprintf(dm->device_name, sizeof dm->device_name, "%s", device_name);
    return TRUE;
}

static BOOL WINAPI EnumDisplaySettingsW(LPCWSTR dev, DWORD mode, DEVMODEW *dm)
{
    if (mode != ENUM_CURRENT_SETTINGS && mode != ENUM_REGISTRY_SETTINGS && mode > 0)
        return FALSE;
    fill_mode(&dm->fields, &dm->bits_per_pel, &dm->pels_width, &dm->pels_height, &dm->display_frequency,
              &dm->size, sizeof *dm);
    utf8_to_utf16(device_name, -1, dm->device_name, 32);
    return TRUE;
}

static BOOL WINAPI EnumDisplaySettingsExA(LPCSTR dev, DWORD mode, DEVMODEA *dm, DWORD flags)
{
    return EnumDisplaySettingsA(dev, mode, dm);
}

static BOOL WINAPI EnumDisplaySettingsExW(LPCWSTR dev, DWORD mode, DEVMODEW *dm, DWORD flags)
{
    return EnumDisplaySettingsW(dev, mode, dm);
}

static LONG WINAPI ChangeDisplaySettingsA(DEVMODEA *dm, DWORD flags) { return DISP_CHANGE_SUCCESSFUL; }
static LONG WINAPI ChangeDisplaySettingsW(DEVMODEW *dm, DWORD flags) { return DISP_CHANGE_SUCCESSFUL; }

static LONG WINAPI ChangeDisplaySettingsExA(LPCSTR dev, DEVMODEA *dm, HWND h, DWORD flags, void *p)
{
    return DISP_CHANGE_SUCCESSFUL;
}

static LONG WINAPI ChangeDisplaySettingsExW(LPCWSTR dev, DEVMODEW *dm, HWND h, DWORD flags, void *p)
{
    return DISP_CHANGE_SUCCESSFUL;
}

static BOOL WINAPI EnumDisplayDevicesA(LPCSTR dev, DWORD index, DISPLAY_DEVICEA *out, DWORD flags)
{
    if (index)
        return FALSE;
    snprintf(out->name, sizeof out->name, "%s", device_name);
    snprintf(out->string, sizeof out->string, "Screw Display");
    out->flags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE;
    out->id[0] = out->key[0] = 0;
    return TRUE;
}

static BOOL WINAPI EnumDisplayDevicesW(LPCWSTR dev, DWORD index, DISPLAY_DEVICEW *out, DWORD flags)
{
    if (index)
        return FALSE;
    utf8_to_utf16(device_name, -1, out->name, 32);
    utf8_to_utf16("Screw Display", -1, out->string, 128);
    out->flags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE;
    out->id[0] = out->key[0] = 0;
    return TRUE;
}

static UINT WINAPI GetDpiForWindow(HWND h) { return USER_DEFAULT_DPI; }
static UINT WINAPI GetDpiForSystem(void) { return USER_DEFAULT_DPI; }
static BOOL WINAPI SetProcessDPIAware(void) { return TRUE; }
static BOOL WINAPI IsProcessDPIAware(void) { return TRUE; }
static BOOL WINAPI SetProcessDpiAwarenessContext(void *ctx) { return TRUE; }
static void *WINAPI SetThreadDpiAwarenessContext(void *ctx) { return (void *)(intptr_t)-4; }
static void *WINAPI GetThreadDpiAwarenessContext(void) { return (void *)(intptr_t)-4; }

static DWORD WINAPI GetSysColor(int index)
{
    static const DWORD colors[] = { 0xC8C8C8, 0x000000, 0xD1B499, 0xDBCDBF, 0xF0F0F0, 0xFFFFFF, 0x646464,
                                    0x000000, 0x000000, 0x000000, 0xB4B4B4, 0xFCF7F4, 0xABABAB, 0xD77800,
                                    0xFFFFFF, 0xF0F0F0, 0xA0A0A0, 0x6D6D6D, 0x000000, 0x000000, 0xFFFFFF };
    return index >= 0 && index < (int)(sizeof colors / sizeof colors[0]) ? colors[index] : 0;
}

static UINT WINAPI GetDoubleClickTime(void) { return 500; }
static UINT WINAPI GetCaretBlinkTime(void) { return 530; }
static BOOL WINAPI MessageBeep(UINT type) { return TRUE; }

static void *resource_handle(uintptr_t id) { return (void *)(0x20000 | (id & 0xFFFF)); }

static HCURSOR WINAPI LoadCursorA(HINSTANCE inst, LPCSTR name) { return resource_handle((uintptr_t)name); }
static HCURSOR WINAPI LoadCursorW(HINSTANCE inst, LPCWSTR name) { return resource_handle((uintptr_t)name); }
static HICON WINAPI LoadIconA(HINSTANCE inst, LPCSTR name) { return resource_handle((uintptr_t)name); }
static HICON WINAPI LoadIconW(HINSTANCE inst, LPCWSTR name) { return resource_handle((uintptr_t)name); }

static HANDLE WINAPI LoadImageA(HINSTANCE inst, LPCSTR name, UINT type, int cx, int cy, UINT flags)
{
    return resource_handle((uintptr_t)name);
}

static HANDLE WINAPI LoadImageW(HINSTANCE inst, LPCWSTR name, UINT type, int cx, int cy, UINT flags)
{
    return resource_handle((uintptr_t)name);
}

static BOOL WINAPI DestroyIcon(HICON h) { return TRUE; }
static BOOL WINAPI DestroyCursor(HCURSOR h) { return TRUE; }

static DWORD WINAPI GetClipboardSequenceNumber(void) { return 0; }
static BOOL WINAPI OpenClipboard(HWND owner) { return TRUE; }
static BOOL WINAPI CloseClipboard(void) { return TRUE; }
static BOOL WINAPI EmptyClipboard(void) { return TRUE; }
static BOOL WINAPI IsClipboardFormatAvailable(UINT fmt) { return FALSE; }
static HANDLE WINAPI GetClipboardData(UINT fmt) { return NULL; }
static HANDLE WINAPI SetClipboardData(UINT fmt, HANDLE mem) { return mem; }
static UINT WINAPI RegisterClipboardFormatA(LPCSTR name) { return 0xC000; }
static UINT WINAPI RegisterClipboardFormatW(LPCWSTR name) { return 0xC000; }

BIT_TABLE(u32_system,
          BIT_FN(GetSystemMetrics), BIT_FN(GetSystemMetricsForDpi),
          BIT_FN(SystemParametersInfoA), BIT_FN(SystemParametersInfoW),
          BIT_FN(MonitorFromWindow), BIT_FN(MonitorFromPoint), BIT_FN(MonitorFromRect),
          BIT_FN(GetMonitorInfoA), BIT_FN(GetMonitorInfoW), BIT_FN(EnumDisplayMonitors),
          BIT_FN(EnumDisplaySettingsA), BIT_FN(EnumDisplaySettingsW),
          BIT_FN(EnumDisplaySettingsExA), BIT_FN(EnumDisplaySettingsExW),
          BIT_FN(ChangeDisplaySettingsA), BIT_FN(ChangeDisplaySettingsW),
          BIT_FN(ChangeDisplaySettingsExA), BIT_FN(ChangeDisplaySettingsExW),
          BIT_FN(EnumDisplayDevicesA), BIT_FN(EnumDisplayDevicesW),
          BIT_FN(GetDpiForWindow), BIT_FN(GetDpiForSystem), BIT_FN(SetProcessDPIAware), BIT_FN(IsProcessDPIAware),
          BIT_FN(SetProcessDpiAwarenessContext), BIT_FN(SetThreadDpiAwarenessContext),
          BIT_FN(GetThreadDpiAwarenessContext),
          BIT_FN(GetSysColor), BIT_FN(GetDoubleClickTime), BIT_FN(GetCaretBlinkTime), BIT_FN(MessageBeep),
          BIT_FN(LoadCursorA), BIT_FN(LoadCursorW), BIT_FN(LoadIconA), BIT_FN(LoadIconW),
          BIT_FN(LoadImageA), BIT_FN(LoadImageW), BIT_FN(DestroyIcon), BIT_FN(DestroyCursor),
          BIT_FN(GetClipboardSequenceNumber), BIT_FN(OpenClipboard), BIT_FN(CloseClipboard),
          BIT_FN(EmptyClipboard), BIT_FN(IsClipboardFormatAvailable), BIT_FN(GetClipboardData),
          BIT_FN(SetClipboardData), BIT_FN(RegisterClipboardFormatA), BIT_FN(RegisterClipboardFormatW));
