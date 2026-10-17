#include "gdi.h"

#define SRCCOPY 0x00CC0020u

typedef struct {
    LONG x, y;
} POINT;

static bool pixel_span(struct bitmap *b, int x, int y, int w, uint8_t **row, int *bytes)
{
    if (b->bpp != 32 || y < 0 || y >= b->height || x < 0 || x + w > b->width)
        return false;
    *row = b->bits + (size_t)y * b->stride + (size_t)x * 4;
    *bytes = w * 4;
    return true;
}

static struct bitmap *dc_bitmap(HDC h)
{
    struct gdi_object *o = gdi_get(h, GDI_DC);
    return o && !o->dc.window ? o->dc.selected : NULL;
}

static BOOL WINAPI BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop)
{
    struct bitmap *d = dc_bitmap(dst), *s = dc_bitmap(src);
    if (!d || !s || rop != SRCCOPY)
        return FALSE;
    for (int row = 0; row < h; row++) {
        uint8_t *dr, *sr;
        int db, sb;
        if (!pixel_span(d, x, y + row, w, &dr, &db) || !pixel_span(s, sx, sy + row, w, &sr, &sb))
            return FALSE;
        memcpy(dr, sr, db);
    }
    return TRUE;
}

static BOOL WINAPI StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop)
{
    struct bitmap *d = dc_bitmap(dst), *s = dc_bitmap(src);
    if (!d || !s || rop != SRCCOPY || !sw || !sh)
        return FALSE;
    for (int row = 0; row < h; row++) {
        int srow = sy + row * sh / h;
        uint8_t *dr;
        int db;
        if (!pixel_span(d, x, y + row, w, &dr, &db))
            return FALSE;
        for (int col = 0; col < w; col++) {
            int scol = sx + col * sw / w;
            if (scol < 0 || scol >= s->width || srow < 0 || srow >= s->height)
                return FALSE;
            memcpy(dr + col * 4, s->bits + (size_t)srow * s->stride + (size_t)scol * 4, 4);
        }
    }
    return TRUE;
}

static BOOL WINAPI PatBlt(HDC dst, int x, int y, int w, int h, DWORD rop)
{
    struct bitmap *d = dc_bitmap(dst);
    if (!d)
        return FALSE;
    uint32_t fill = rop == 0x00FF0062u ? 0xFFFFFFFFu : 0;
    for (int row = 0; row < h; row++) {
        uint8_t *dr;
        int db;
        if (!pixel_span(d, x, y + row, w, &dr, &db))
            return FALSE;
        for (int col = 0; col < w; col++)
            memcpy(dr + col * 4, &fill, 4);
    }
    return TRUE;
}

static DWORD WINAPI SetPixel(HDC dc, int x, int y, DWORD color)
{
    struct bitmap *d = dc_bitmap(dc);
    uint8_t *row;
    int bytes;
    if (!d || !pixel_span(d, x, y, 1, &row, &bytes))
        return 0xFFFFFFFFu;
    uint32_t bgr = ((color & 0xFF) << 16) | (color & 0xFF00) | ((color >> 16) & 0xFF);
    memcpy(row, &bgr, 4);
    return color;
}

static DWORD WINAPI GetPixel(HDC dc, int x, int y)
{
    struct bitmap *d = dc_bitmap(dc);
    uint8_t *row;
    int bytes;
    if (!d || !pixel_span(d, x, y, 1, &row, &bytes))
        return 0xFFFFFFFFu;
    uint32_t bgr;
    memcpy(&bgr, row, 4);
    return ((bgr & 0xFF) << 16) | (bgr & 0xFF00) | ((bgr >> 16) & 0xFF);
}

static BOOL WINAPI MoveToEx(HDC dc, int x, int y, POINT *prev) { return gdi_get(dc, GDI_DC) != NULL; }
static BOOL WINAPI LineTo(HDC dc, int x, int y) { return gdi_get(dc, GDI_DC) != NULL; }

BIT_TABLE(gdi_blit,
          BIT_FN(BitBlt), BIT_FN(StretchBlt), BIT_FN(PatBlt), BIT_FN(SetPixel), BIT_FN(GetPixel),
          BIT_FN(MoveToEx), BIT_FN(LineTo));
