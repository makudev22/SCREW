#include "gdi.h"

#include <screw/display.h>

#include <pthread.h>

#define MAX_OBJECTS 8192
#define HANDLE_BASE 0x00410000u

#define TECHNOLOGY 2
#define HORZSIZE 4
#define VERTSIZE 6
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define PLANES 14
#define NUMCOLORS 24
#define RASTERCAPS 38
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define VREFRESH 116
#define DESKTOPVERTRES 117
#define DESKTOPHORZRES 118
#define DT_RASDISPLAY 1
#define RC_BITBLT 0x0001
#define RC_STRETCHBLT 0x0800
#define RC_DI_BITMAP 0x0080

#define OBJ_DC 3
#define OBJ_BITMAP 7
#define OBJ_MEMDC 10

typedef struct {
    LONG type;
    LONG width;
    LONG height;
    LONG width_bytes;
    WORD planes;
    WORD bits_pixel;
    void *bits;
} BITMAP;

static struct gdi_object *objects[MAX_OBJECTS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static size_t index_of(HGDIOBJ h)
{
    uintptr_t v = (uintptr_t)h;
    if (v < HANDLE_BASE || v >= HANDLE_BASE + MAX_OBJECTS)
        return 0;
    return v - HANDLE_BASE;
}

HGDIOBJ gdi_new(enum gdi_kind kind, struct gdi_object **out)
{
    struct gdi_object *o = calloc(1, sizeof *o);
    o->kind = kind;
    pthread_mutex_lock(&lock);
    size_t i = 1;
    while (i < MAX_OBJECTS && objects[i])
        i++;
    if (i < MAX_OBJECTS)
        objects[i] = o;
    pthread_mutex_unlock(&lock);
    if (i == MAX_OBJECTS) {
        free(o);
        last_error_set(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (out)
        *out = o;
    return (HGDIOBJ)(uintptr_t)(HANDLE_BASE + i);
}

struct gdi_object *gdi_get(HGDIOBJ h, enum gdi_kind kind)
{
    size_t i = index_of(h);
    pthread_mutex_lock(&lock);
    struct gdi_object *o = i ? objects[i] : NULL;
    pthread_mutex_unlock(&lock);
    return o && o->kind == kind ? o : NULL;
}

void gdi_free(HGDIOBJ h)
{
    size_t i = index_of(h);
    if (!i)
        return;
    pthread_mutex_lock(&lock);
    struct gdi_object *o = objects[i];
    objects[i] = NULL;
    pthread_mutex_unlock(&lock);
    if (o && o->kind == GDI_BITMAP && o->bitmap.owns_bits)
        free(o->bitmap.bits);
    free(o);
}

HBITMAP bitmap_create(int width, int height, int bpp, const void *init)
{
    if (width <= 0 || height <= 0 || (bpp != 1 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)) {
        last_error_set(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    struct gdi_object *o;
    HBITMAP h = gdi_new(GDI_BITMAP, &o);
    if (!h)
        return NULL;
    struct bitmap *b = &o->bitmap;
    b->width = width;
    b->height = height;
    b->bpp = bpp;
    b->stride = ((width * bpp + 15) / 16) * 2;
    b->bits = calloc(height, b->stride);
    b->owns_bits = true;
    if (init)
        memcpy(b->bits, init, (size_t)height * b->stride);
    return h;
}

static HDC new_dc(HWND window)
{
    struct gdi_object *o;
    HDC h = gdi_new(GDI_DC, &o);
    if (!h)
        return NULL;
    o->dc.window = window;
    if (!window) {
        o->dc.default_bitmap = bitmap_create(1, 1, 1, NULL);
        o->dc.selected_handle = o->dc.default_bitmap;
        o->dc.selected = &gdi_get(o->dc.default_bitmap, GDI_BITMAP)->bitmap;
    }
    return h;
}

void *gdi_window_dc(HWND hwnd) { return new_dc(hwnd ? hwnd : (HWND)(uintptr_t)0x10010u); }

int gdi_release_dc(HWND hwnd, void *hdc)
{
    struct gdi_object *o = gdi_get(hdc, GDI_DC);
    if (!o || !o->dc.window)
        return 0;
    gdi_free(hdc);
    return 1;
}

static HDC WINAPI CreateCompatibleDC(HDC dc) { return new_dc(NULL); }

static HDC WINAPI CreateDCW(const WCHAR *driver, const WCHAR *device, const WCHAR *port, const void *mode)
{
    return gdi_window_dc(NULL);
}

static HDC WINAPI CreateDCA(LPCSTR driver, LPCSTR device, LPCSTR port, const void *mode)
{
    return gdi_window_dc(NULL);
}

static BOOL WINAPI DeleteDC(HDC h)
{
    struct gdi_object *o = gdi_get(h, GDI_DC);
    if (!o)
        return FALSE;
    if (o->dc.default_bitmap)
        gdi_free(o->dc.default_bitmap);
    gdi_free(h);
    return TRUE;
}

static HGDIOBJ WINAPI SelectObject(HDC h, HGDIOBJ obj)
{
    struct gdi_object *dc = gdi_get(h, GDI_DC);
    struct gdi_object *bm = gdi_get(obj, GDI_BITMAP);
    if (!dc)
        return NULL;
    if (!bm || dc->dc.window)
        return NULL;
    HGDIOBJ prev = dc->dc.selected_handle;
    dc->dc.selected = &bm->bitmap;
    dc->dc.selected_handle = obj;
    return prev;
}

static HGDIOBJ WINAPI GetCurrentObject(HDC h, UINT type)
{
    struct gdi_object *dc = gdi_get(h, GDI_DC);
    return dc && type == OBJ_BITMAP ? dc->dc.selected_handle : NULL;
}

static BOOL WINAPI DeleteObject(HGDIOBJ h)
{
    if (!gdi_get(h, GDI_BITMAP))
        return FALSE;
    gdi_free(h);
    return TRUE;
}

static int get_object(HGDIOBJ h, int size, void *out)
{
    struct gdi_object *o = gdi_get(h, GDI_BITMAP);
    if (!o)
        return 0;
    if (!out)
        return sizeof(BITMAP);
    if (size < (int)sizeof(BITMAP))
        return 0;
    struct bitmap *b = &o->bitmap;
    *(BITMAP *)out = (BITMAP){ 0, b->width, b->height, b->stride, 1, (WORD)b->bpp, b->owns_bits ? NULL : b->bits };
    return sizeof(BITMAP);
}

static int WINAPI GetObjectA(HGDIOBJ h, int size, void *out) { return get_object(h, size, out); }
static int WINAPI GetObjectW(HGDIOBJ h, int size, void *out) { return get_object(h, size, out); }

static DWORD WINAPI GetObjectType(HGDIOBJ h)
{
    struct gdi_object *dc = gdi_get(h, GDI_DC);
    if (dc)
        return dc->dc.window ? OBJ_DC : OBJ_MEMDC;
    return gdi_get(h, GDI_BITMAP) ? OBJ_BITMAP : 0;
}

static HBITMAP WINAPI CreateBitmap(int w, int h, UINT planes, UINT bpp, const void *bits)
{
    return bitmap_create(w, h, planes * bpp, bits);
}

static HBITMAP WINAPI CreateCompatibleBitmap(HDC dc, int w, int h) { return bitmap_create(w, h, 32, NULL); }

static int WINAPI GetDeviceCaps(HDC dc, int index)
{
    int w, h;
    display_size(&w, &h);
    switch (index) {
    case TECHNOLOGY: return DT_RASDISPLAY;
    case HORZRES:
    case DESKTOPHORZRES: return w;
    case VERTRES:
    case DESKTOPVERTRES: return h;
    case HORZSIZE: return w * 254 / 960;
    case VERTSIZE: return h * 254 / 960;
    case BITSPIXEL: return 32;
    case PLANES: return 1;
    case NUMCOLORS: return -1;
    case RASTERCAPS: return RC_BITBLT | RC_STRETCHBLT | RC_DI_BITMAP;
    case LOGPIXELSX:
    case LOGPIXELSY: return 96;
    case VREFRESH: return 60;
    default: return 0;
    }
}

BIT_TABLE(gdi_objects,
          BIT_FN(CreateCompatibleDC), BIT_FN(CreateDCA), BIT_FN(CreateDCW), BIT_FN(DeleteDC),
          BIT_FN(SelectObject), BIT_FN(GetCurrentObject), BIT_FN(DeleteObject),
          BIT_FN(GetObjectA), BIT_FN(GetObjectW), BIT_FN(GetObjectType),
          BIT_FN(CreateBitmap), BIT_FN(CreateCompatibleBitmap), BIT_FN(GetDeviceCaps));
