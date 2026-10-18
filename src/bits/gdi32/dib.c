#include "gdi.h"

#include <string.h>

#define BI_RGB 0
#define DIB_RGB_COLORS 0

typedef struct {
    DWORD size;
    LONG width;
    LONG height;
    WORD planes;
    WORD bit_count;
    DWORD compression;
    DWORD size_image;
    LONG x_ppm;
    LONG y_ppm;
    DWORD clr_used;
    DWORD clr_important;
} BITMAPINFOHEADER;

typedef struct {
    BITMAPINFOHEADER header;
    DWORD colors[256];
} BITMAPINFO;

static int stride_of(int width, int bpp) { return ((width * bpp + 31) / 32) * 4; }

static HBITMAP WINAPI CreateDIBSection(HDC dc, const BITMAPINFO *bmi, UINT usage, void **bits, HANDLE section,
                                       DWORD offset)
{
    if (!bmi)
        return NULL;
    int w = bmi->header.width;
    int h = bmi->header.height < 0 ? -bmi->header.height : bmi->header.height;
    int bpp = bmi->header.bit_count ? bmi->header.bit_count : 32;
    HBITMAP hb = bitmap_create(w, h, bpp, NULL);
    struct gdi_object *o = gdi_get(hb, GDI_BITMAP);
    if (!o)
        return NULL;
    if (bits)
        *bits = o->bitmap.bits;
    return hb;
}

static int WINAPI GetDIBits(HDC dc, HBITMAP hb, UINT start, UINT lines, void *bits, BITMAPINFO *bmi, UINT usage)
{
    struct gdi_object *o = gdi_get(hb, GDI_BITMAP);
    if (!o || !bmi)
        return 0;
    struct bitmap *b = &o->bitmap;
    if (!bits) {
        bmi->header.width = b->width;
        bmi->header.height = b->height;
        bmi->header.planes = 1;
        bmi->header.bit_count = b->bpp;
        bmi->header.compression = BI_RGB;
        bmi->header.size_image = b->stride * b->height;
        return b->height;
    }
    int dst_stride = stride_of(b->width, bmi->header.bit_count ? bmi->header.bit_count : b->bpp);
    UINT n = start + lines > (UINT)b->height ? b->height - start : lines;
    for (UINT i = 0; i < n; i++)
        memcpy((uint8_t *)bits + i * dst_stride, b->bits + (start + i) * b->stride,
               dst_stride < b->stride ? dst_stride : b->stride);
    return n;
}

static int WINAPI SetDIBits(HDC dc, HBITMAP hb, UINT start, UINT lines, const void *bits, const BITMAPINFO *bmi,
                            UINT usage)
{
    struct gdi_object *o = gdi_get(hb, GDI_BITMAP);
    if (!o || !bits || !bmi)
        return 0;
    struct bitmap *b = &o->bitmap;
    int src_stride = stride_of(b->width, bmi->header.bit_count ? bmi->header.bit_count : b->bpp);
    UINT n = start + lines > (UINT)b->height ? b->height - start : lines;
    for (UINT i = 0; i < n; i++)
        memcpy(b->bits + (start + i) * b->stride, (const uint8_t *)bits + i * src_stride,
               src_stride < b->stride ? src_stride : b->stride);
    return n;
}

static int WINAPI StretchDIBits(HDC dc, int xd, int yd, int wd, int hd, int xs, int ys, int ws, int hs,
                                const void *bits, const BITMAPINFO *bmi, UINT usage, DWORD rop)
{
    return hs;
}

static int WINAPI SetDIBitsToDevice(HDC dc, int xd, int yd, DWORD w, DWORD h, int xs, int ys, UINT start,
                                    UINT lines, const void *bits, const BITMAPINFO *bmi, UINT usage)
{
    return lines;
}

static UINT WINAPI GetDIBColorTable(HDC dc, UINT start, UINT count, void *colors) { return 0; }
static UINT WINAPI SetDIBColorTable(HDC dc, UINT start, UINT count, const void *colors) { return count; }

BIT_TABLE(gdi_dib,
          BIT_FN(CreateDIBSection), BIT_FN(GetDIBits), BIT_FN(SetDIBits), BIT_FN(StretchDIBits),
          BIT_FN(SetDIBitsToDevice), BIT_FN(GetDIBColorTable), BIT_FN(SetDIBColorTable));
