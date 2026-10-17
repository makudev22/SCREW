#include <stdio.h>
#include <windows.h>

int main(void)
{
    HDC screen = GetDC(NULL);
    printf("screen dc: %s, %dx%d, %d bpp\n", screen ? "ok" : "none", GetDeviceCaps(screen, HORZRES),
           GetDeviceCaps(screen, VERTRES), GetDeviceCaps(screen, BITSPIXEL));

    HDC src = CreateCompatibleDC(screen);
    HDC dst = CreateCompatibleDC(screen);
    HBITMAP sb = CreateBitmap(4, 4, 1, 32, NULL);
    HBITMAP db = CreateBitmap(4, 4, 1, 32, NULL);
    SelectObject(src, sb);
    SelectObject(dst, db);

    SetPixel(src, 1, 2, RGB(10, 20, 30));
    SetPixel(src, 3, 3, RGB(200, 100, 50));
    COLORREF c = GetPixel(src, 1, 2);
    printf("pixel roundtrip: %s (r=%d g=%d b=%d)\n", c == RGB(10, 20, 30) ? "ok" : "BAD", GetRValue(c),
           GetGValue(c), GetBValue(c));

    BOOL blit = BitBlt(dst, 0, 0, 4, 4, src, 0, 0, SRCCOPY);
    printf("bitblt: %s, copied pixel %s\n", blit ? "ok" : "BAD",
           GetPixel(dst, 3, 3) == RGB(200, 100, 50) ? "ok" : "BAD");

    HDC big = CreateCompatibleDC(screen);
    HBITMAP bb = CreateBitmap(8, 8, 1, 32, NULL);
    SelectObject(big, bb);
    BOOL stretch = StretchBlt(big, 0, 0, 8, 8, src, 0, 0, 4, 4, SRCCOPY);
    printf("stretchblt: %s, scaled pixel %s\n", stretch ? "ok" : "BAD",
           GetPixel(big, 7, 7) == RGB(200, 100, 50) ? "ok" : "BAD");

    BITMAP info;
    GetObjectA(bb, sizeof info, &info);
    printf("getobject: %ldx%ld, type memdc=%s\n", info.bmWidth, info.bmHeight,
           GetObjectType(big) == OBJ_MEMDC ? "yes" : "no");

    DeleteObject(sb);
    DeleteObject(db);
    DeleteObject(bb);
    DeleteDC(src);
    DeleteDC(dst);
    DeleteDC(big);
    ReleaseDC(NULL, screen);
    puts("cleanup done");
    return 0;
}
