#include "u32.h"

#include <screw/gdi.h>

typedef struct {
    HDC hdc;
    BOOL erase;
    RECT paint;
    BOOL restore;
    BOOL inc_update;
    BYTE reserved[32];
} PAINTSTRUCT;

typedef void *HDC;

static HDC WINAPI GetDC(HWND h)
{
    if (h && !window_get(h))
        return NULL;
    return gdi_window_dc(h);
}

static HDC WINAPI GetDCEx(HWND h, void *region, DWORD flags) { return GetDC(h); }
static HDC WINAPI GetWindowDC(HWND h) { return GetDC(h); }

static int WINAPI ReleaseDC(HWND h, HDC dc) { return gdi_release_dc(h, dc); }

static HDC WINAPI BeginPaint(HWND h, PAINTSTRUCT *ps)
{
    struct window *w = window_get(h);
    HDC dc = gdi_window_dc(h);
    memset(ps, 0, sizeof *ps);
    ps->hdc = dc;
    ps->erase = TRUE;
    if (w)
        ps->paint = (RECT){ 0, 0, w->w, w->h };
    return dc;
}

static BOOL WINAPI EndPaint(HWND h, const PAINTSTRUCT *ps)
{
    if (ps)
        gdi_release_dc(h, ps->hdc);
    return TRUE;
}

static BOOL WINAPI GetUpdateRect(HWND h, RECT *r, BOOL erase)
{
    struct window *w = window_get(h);
    if (r)
        *r = w ? (RECT){ 0, 0, w->needs_paint ? w->w : 0, w->needs_paint ? w->h : 0 } : (RECT){ 0, 0, 0, 0 };
    return w && w->needs_paint;
}

static BOOL WINAPI IsWindowUnicode(HWND h)
{
    struct window *w = window_get(h);
    return w && w->wide;
}

BIT_TABLE(u32_paint,
          BIT_FN(GetDC), BIT_FN(GetDCEx), BIT_FN(GetWindowDC), BIT_FN(ReleaseDC),
          BIT_FN(BeginPaint), BIT_FN(EndPaint), BIT_FN(GetUpdateRect), BIT_FN(IsWindowUnicode));
