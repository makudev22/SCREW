#include <stdio.h>
#include <windows.h>
#include <setupapi.h>

int main(void)
{
    GUID display = { 0x4d36e968, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
    HDEVINFO set = SetupDiGetClassDevsW(&display, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    printf("devinfo set: %s\n", set != INVALID_HANDLE_VALUE ? "ok" : "fail");

    SP_DEVICE_INTERFACE_DATA iface = { .cbSize = sizeof iface };
    BOOL first = SetupDiEnumDeviceInterfaces(set, NULL, &display, 0, &iface);
    BOOL second = SetupDiEnumDeviceInterfaces(set, NULL, &display, 1, &iface);
    printf("enumerate: first %s, second %s\n", first ? "yes" : "no", second ? "yes" : "no");

    DWORD need = 0;
    SetupDiGetDeviceInterfaceDetailW(set, &iface, NULL, 0, &need, NULL);
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = malloc(need);
    detail->cbSize = sizeof *detail;
    BOOL got = SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, need, NULL, NULL);
    printf("detail: %s, path starts %lc%lc\n", got ? "ok" : "fail", detail->DevicePath[0], detail->DevicePath[1]);

    SetupDiDestroyDeviceInfoList(set);
    free(detail);
    return 0;
}
