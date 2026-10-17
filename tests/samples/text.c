#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include <windows.h>

int main(void)
{
    const wchar_t *w = L"Привет, Screw! ✓";
    char utf8[64];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, utf8, sizeof utf8, NULL, NULL);
    printf("utf8 (%d): %s\n", n, utf8);
    wchar_t back[64];
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, back, 64);
    printf("roundtrip: %s, wcslen=%u\n", wcscmp(w, back) == 0 ? "ok" : "bad", (unsigned)wcslen(back));
    printf("float: %.3f %e %g\n", 3.14159265, 12345.678, sqrt(2.0));
    printf("ints: %5d|%-5d|%05x|%lld|%I64u|%ld\n", 42, 42, 255, -1234567890123LL, 18446744073709551615ULL, (long)-7);
    printf("str: [%10s] [%-6.3s] %c\n", "right", "trunc", 'Z');
    char buf[32];
    int k = snprintf(buf, sizeof buf, "%s-%d", "abc", 99);
    printf("snprintf: %s (%d)\n", buf, k);
    int a; char word[16]; double d;
    sscanf("17 apples 2.5", "%d %15s %lf", &a, word, &d);
    printf("sscanf: %d %s %.1f\n", a, word, d);
    printf("strtol: %ld\n", strtol("-0x7f", NULL, 16));
    return 0;
}
