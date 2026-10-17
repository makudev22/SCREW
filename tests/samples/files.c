#include <stdio.h>
#include <string.h>
#include <windows.h>

int main(void)
{
    char dir[MAX_PATH];
    GetTempPathA(sizeof dir, dir);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sscrew_test.txt", dir);

    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("create failed %lu\n", GetLastError());
        return 1;
    }
    DWORD n;
    WriteFile(h, "Screw rocks\n", 12, &n, NULL);
    CloseHandle(h);

    FILE *f = fopen(path, "r");
    char line[64] = "";
    fgets(line, sizeof line, f);
    fclose(f);
    printf("read back: %s", line);

    WIN32_FIND_DATAA fd;
    char spec[MAX_PATH];
    snprintf(spec, sizeof spec, "%sSCREW_*.TXT", dir);
    HANDLE fh = FindFirstFileA(spec, &fd);
    printf("found: %s size=%lu\n", fh != INVALID_HANDLE_VALUE ? fd.cFileName : "(none)", fd.nFileSizeLow);
    FindClose(fh);

    printf("attrs ok: %d\n", GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES);
    printf("delete: %d\n", DeleteFileA(path));
    printf("gone: %d\n", GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES);
    return 0;
}
