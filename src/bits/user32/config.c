#include "u32.h"

#define ERROR_NOT_SUPPORTED_ 50

static LONG WINAPI GetDisplayConfigBufferSizes(DWORD flags, DWORD *paths, DWORD *modes)
{
    if (paths)
        *paths = 0;
    if (modes)
        *modes = 0;
    return ERROR_NOT_SUPPORTED_;
}

static LONG WINAPI QueryDisplayConfig(DWORD flags, DWORD *path_count, void *paths, DWORD *mode_count, void *modes,
                                      void *topology)
{
    return ERROR_NOT_SUPPORTED_;
}

static LONG WINAPI DisplayConfigGetDeviceInfo(void *packet) { return ERROR_NOT_SUPPORTED_; }

BIT_TABLE(u32_config,
          BIT_FN(GetDisplayConfigBufferSizes), BIT_FN(QueryDisplayConfig), BIT_FN(DisplayConfigGetDeviceInfo));
