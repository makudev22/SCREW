#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

int main(int argc, char **argv)
{
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, sizeof self);
    DWORD handle = 0, size = GetFileVersionInfoSizeA(self, &handle);
    printf("size known: %s\n", size > 0 ? "yes" : "no");
    if (!size)
        return 1;
    void *buf = malloc(size);
    if (!GetFileVersionInfoA(self, 0, size, buf)) {
        puts("read failed");
        return 1;
    }

    VS_FIXEDFILEINFO *fixed;
    UINT len;
    if (VerQueryValueA(buf, "\\", (void **)&fixed, &len))
        printf("file version: %lu.%lu.%lu.%lu\n", HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
               HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));

    DWORD *lang;
    if (VerQueryValueA(buf, "\\VarFileInfo\\Translation", (void **)&lang, &len))
        printf("translation: %04x %04x\n", LOWORD(*lang), HIWORD(*lang));

    char *name;
    if (VerQueryValueA(buf, "\\StringFileInfo\\040904b0\\ProductName", (void **)&name, &len))
        printf("product (ansi): %s\n", name);

    WCHAR *wdesc;
    if (VerQueryValueW(buf, L"\\StringFileInfo\\040904B0\\FileDescription", (void **)&wdesc, &len))
        printf("description (wide, %u chars): %ls\n", len, wdesc);

    printf("missing key: %s\n", VerQueryValueA(buf, "\\StringFileInfo\\040904b0\\Nope", (void **)&name, &len) ? "found" : "absent");
    printf("no resource in kernel32: %s\n", GetFileVersionInfoSizeA("C:\\windows\\system32\\kernel32.dll", &handle) ? "has" : "none");
    free(buf);
    return 0;
}
