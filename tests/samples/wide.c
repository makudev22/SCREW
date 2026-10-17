#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <windows.h>

int main(void)
{
    const wchar_t text[] = L"\x041F\x0440\x0438\x0432\x0435\x0442 \xD83D\xDE00!\n";
    FILE *f = fopen("wide_test.txt", "wb");
    fputws(text, f);
    fputwc(L'z', f);
    fclose(f);

    f = fopen("wide_test.txt", "rb");
    wint_t first = fgetwc(f);
    ungetwc(first, f);
    wchar_t back[64] = { 0 };
    fgetws(back, 64, f);
    wint_t last = fgetwc(f);
    wint_t end = fgetwc(f);
    fclose(f);
    remove("wide_test.txt");

    printf("roundtrip: %s\n", wcscmp(back, text) == 0 ? "ok" : "BAD");
    printf("first: %04x, last: %c, eof: %s\n", (unsigned)first, (int)last, end == WEOF ? "yes" : "no");

    const char *src = "\xD0\x90\xD0\x91\xD0\x92";
    const char *p = src;
    wchar_t out[8] = { 0 };
    typedef size_t (*mbsrtowcs_fn)(wchar_t *, const char **, size_t, void *);
    mbsrtowcs_fn crt_mbsrtowcs = (mbsrtowcs_fn)GetProcAddress(GetModuleHandleA("msvcrt.dll"), "mbsrtowcs");
    size_t n = crt_mbsrtowcs(out, &p, 8, NULL);
    printf("mbsrtowcs: %u units, %04x %04x %04x, done %s\n", (unsigned)n, (unsigned)out[0], (unsigned)out[1],
           (unsigned)out[2], p == NULL ? "yes" : "no");

    printf("wctype: alpha(A)=%d digit(7)=%d space(x)=%d\n", iswctype(L'A', wctype("alpha")) != 0,
           iswctype(L'7', wctype("digit")) != 0, iswctype(L'x', wctype("space")) != 0);
    return 0;
}
