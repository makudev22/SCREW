#include <stdio.h>
#include <windows.h>

int main(void)
{
    HKEY key;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\ScrewWatchTest", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL)) {
        puts("create failed");
        return 1;
    }
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    LONG r = RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, ev, TRUE);
    printf("notify armed: %s\n", r == ERROR_SUCCESS ? "yes" : "no");
    printf("before change: %s\n", WaitForSingleObject(ev, 100) == WAIT_TIMEOUT ? "quiet" : "signaled");

    DWORD v = 7;
    RegSetValueExA(key, "Level", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
    printf("after change: %s\n", WaitForSingleObject(ev, 3000) == WAIT_OBJECT_0 ? "signaled" : "timeout");

    LUID a, b;
    AllocateLocallyUniqueId(&a);
    AllocateLocallyUniqueId(&b);
    printf("luids unique: %s\n", a.LowPart != b.LowPart || a.HighPart != b.HighPart ? "yes" : "no");

    RegCloseKey(key);
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\ScrewWatchTest");
    CloseHandle(ev);
    return 0;
}
