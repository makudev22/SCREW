#include "u32.h"

#include <X11/Xatom.h>

#include <pthread.h>
#include <stdio.h>

#define MAX_WINDOWS 4096
#define HWND_BASE 0x10000u
#define HWND_STEP 2u
#define DESKTOP ((HWND)(uintptr_t)0x10010u)

#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080

#define HWND_TOPMOST ((HWND)(intptr_t)-1)
#define HWND_NOTOPMOST ((HWND)(intptr_t)-2)

#define ERROR_INVALID_WINDOW_HANDLE 1400
#define ERROR_CANNOT_FIND_WND_CLASS 1407

#define SC_CLOSE 0xF060
#define HTCLIENT 1

static BOOL WINAPI ShowWindow(HWND h, int cmd);

static struct window *windows[MAX_WINDOWS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static HWND focus;

struct window *window_get(HWND h)
{
    uintptr_t v = (uintptr_t)h;
    if (v < HWND_BASE + HWND_STEP * 16 || (v - HWND_BASE) % HWND_STEP)
        return NULL;
    size_t i = (v - HWND_BASE) / HWND_STEP;
    if (i >= MAX_WINDOWS)
        return NULL;
    struct window *w = windows[i];
    return w && !w->destroyed ? w : NULL;
}

struct window *window_by_xid(Window xid)
{
    pthread_mutex_lock(&lock);
    struct window *found = NULL;
    for (size_t i = 0; i < MAX_WINDOWS && !found; i++)
        if (windows[i] && windows[i]->xid == xid && !windows[i]->destroyed)
            found = windows[i];
    pthread_mutex_unlock(&lock);
    return found;
}

static HWND add(struct window *w)
{
    pthread_mutex_lock(&lock);
    size_t i = 16;
    while (i < MAX_WINDOWS && windows[i])
        i++;
    if (i == MAX_WINDOWS) {
        pthread_mutex_unlock(&lock);
        return NULL;
    }
    windows[i] = w;
    pthread_mutex_unlock(&lock);
    w->hwnd = (HWND)(uintptr_t)(HWND_BASE + i * HWND_STEP);
    return w->hwnd;
}

static void drop(struct window *w)
{
    pthread_mutex_lock(&lock);
    windows[((uintptr_t)w->hwnd - HWND_BASE) / HWND_STEP] = NULL;
    pthread_mutex_unlock(&lock);
    free(w->title);
    free(w->extra);
    free(w);
}

LRESULT window_send(struct window *w, UINT msg, WPARAM wp, LPARAM lp)
{
    return w && w->proc ? w->proc(w->hwnd, msg, wp, lp) : 0;
}

HWND window_focus(void) { return focus; }
void window_set_focus(HWND h) { focus = h; }

void window_moved(struct window *w, int x, int y, int width, int height)
{
    bool resized = width != w->w || height != w->h;
    bool moved = x != w->x || y != w->y;
    w->x = x;
    w->y = y;
    w->w = width;
    w->h = height;
    if (moved)
        window_send(w, WM_MOVE, 0, make_lparam(x, y));
    if (resized) {
        window_send(w, WM_SIZE, SIZE_RESTORED, make_lparam(width, height));
        w->needs_paint = true;
    }
}

static void set_title(struct window *w, const char *title)
{
    free(w->title);
    w->title = strdup(title ? title : "");
    Display *d = x11();
    if (!d || !w->xid)
        return;
    XStoreName(d, w->xid, w->title);
    XChangeProperty(d, w->xid, x11_atom("_NET_WM_NAME"), x11_atom("UTF8_STRING"), 8, PropModeReplace,
                    (const unsigned char *)w->title, strlen(w->title));
    XFlush(d);
}

static void decorations(struct window *w)
{
    struct {
        unsigned long flags, functions, decorations;
        long input_mode;
        unsigned long status;
    } hints = { 2, 0, (w->style & WS_POPUP) ? 0 : 1, 0, 0 };
    XChangeProperty(x11(), w->xid, x11_atom("_MOTIF_WM_HINTS"), x11_atom("_MOTIF_WM_HINTS"), 32,
                    PropModeReplace, (unsigned char *)&hints, 5);
}

static void fullscreen(struct window *w, bool on)
{
    Display *d = x11();
    Atom state = x11_atom("_NET_WM_STATE"), full = x11_atom("_NET_WM_STATE_FULLSCREEN");
    if (!w->visible) {
        if (on)
            XChangeProperty(d, w->xid, state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&full, 1);
        else
            XDeleteProperty(d, w->xid, state);
        return;
    }
    XEvent e = { 0 };
    e.xclient.type = ClientMessage;
    e.xclient.window = w->xid;
    e.xclient.message_type = state;
    e.xclient.format = 32;
    e.xclient.data.l[0] = on ? 1 : 0;
    e.xclient.data.l[1] = full;
    e.xclient.data.l[3] = 1;
    XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
}

static bool covers_screen(struct window *w)
{
    int sw, sh;
    display_size(&sw, &sh);
    return (w->style & WS_POPUP) && w->x <= 0 && w->y <= 0 && w->w >= sw && w->h >= sh;
}

static HWND create(DWORD ex_style, const void *cls_name, const char *title, DWORD style, int x, int y, int cw,
                   int ch, HWND parent, HMENU menu, HINSTANCE inst, void *param, bool wide)
{
    char key[128];
    class_key(cls_name, wide, key);
    struct wclass *c = class_find(key, (uintptr_t)cls_name < 0x10000 ? (ATOM)(uintptr_t)cls_name : 0);
    if (!c) {
        last_error_set(ERROR_CANNOT_FIND_WND_CLASS);
        return NULL;
    }
    Display *d = x11();
    if (!d) {
        last_error_set(ERROR_NOT_SUPPORTED);
        return NULL;
    }

    struct window *w = calloc(1, sizeof *w);
    w->cls = c;
    w->proc = c->proc;
    w->wide = wide;
    w->parent = parent == DESKTOP ? NULL : parent;
    w->style = style;
    w->ex_style = ex_style;
    w->instance = inst;
    w->id = (LONG_PTR)menu;
    w->thread = teb_current()->thread_id;
    w->extra = calloc(1, c->wnd_extra > 0 ? c->wnd_extra : 1);
    w->x = x == CW_USEDEFAULT ? 64 : x;
    w->y = x == CW_USEDEFAULT ? 64 : y;
    w->w = cw == CW_USEDEFAULT || cw <= 0 ? 800 : cw;
    w->h = cw == CW_USEDEFAULT || ch <= 0 ? 600 : ch;
    if (!add(w)) {
        free(w->extra);
        free(w);
        last_error_set(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    struct window *pw = window_get(w->parent);
    Window xparent = pw && (style & WS_CHILD) ? pw->xid : DefaultRootWindow(d);
    XSetWindowAttributes attrs = { 0 };
    attrs.background_pixel = BlackPixel(d, DefaultScreen(d));
    attrs.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                       StructureNotifyMask | ExposureMask | FocusChangeMask | EnterWindowMask | LeaveWindowMask;
    w->xid = XCreateWindow(d, xparent, w->x, w->y, w->w, w->h, 0, CopyFromParent, InputOutput, CopyFromParent,
                           CWBackPixel | CWEventMask, &attrs);
    Atom del = x11_atom("WM_DELETE_WINDOW");
    XSetWMProtocols(d, w->xid, &del, 1);
    XClassHint hint = { (char *)"screw", (char *)"Screw" };
    XSetClassHint(d, w->xid, &hint);
    decorations(w);
    set_title(w, title);
    if (covers_screen(w))
        fullscreen(w, true);

    CREATESTRUCT cs = { param, inst, menu, parent, w->h, w->w, w->y, w->x, (LONG)style, NULL, cls_name, ex_style };
    WCHAR *wtitle = wide ? host_to_wide(w->title) : NULL;
    cs.name = wide ? (const void *)wtitle : (const void *)w->title;
    HWND h = w->hwnd;
    if (!window_send(w, WM_NCCREATE, 0, (LPARAM)&cs) || window_send(w, WM_CREATE, 0, (LPARAM)&cs) == -1) {
        free(wtitle);
        XDestroyWindow(d, w->xid);
        drop(w);
        return NULL;
    }
    free(wtitle);
    window_send(w, WM_SIZE, SIZE_RESTORED, make_lparam(w->w, w->h));
    window_send(w, WM_MOVE, 0, make_lparam(w->x, w->y));
    if (style & WS_VISIBLE) {
        w->style &= ~WS_VISIBLE;
        ShowWindow(h, 5);
    }
    XFlush(d);
    return h;
}

static HWND WINAPI CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h,
                                   HWND parent, HMENU menu, HINSTANCE inst, void *param)
{
    return create(ex, cls, title, style, x, y, w, h, parent, menu, inst, param, false);
}

static HWND WINAPI CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h,
                                   HWND parent, HMENU menu, HINSTANCE inst, void *param)
{
    char *t = title ? wide_to_host(title) : NULL;
    HWND r = create(ex, cls, t, style, x, y, w, h, parent, menu, inst, param, true);
    free(t);
    return r;
}

static BOOL WINAPI DestroyWindow(HWND h)
{
    struct window *w = window_get(h);
    if (!w)
        return FALSE;
    window_send(w, WM_DESTROY, 0, 0);
    window_send(w, WM_NCDESTROY, 0, 0);
    w->destroyed = true;
    if (focus == h)
        focus = NULL;
    Display *d = x11();
    XDestroyWindow(d, w->xid);
    XFlush(d);
    drop(w);
    return TRUE;
}

static BOOL WINAPI ShowWindow(HWND h, int cmd)
{
    struct window *w = window_get(h);
    if (!w)
        return FALSE;
    Display *d = x11();
    bool was = w->visible;
    bool show = cmd != SW_HIDE;
    if (show == was && cmd != SW_MAXIMIZE && cmd != SW_MINIMIZE && cmd != SW_SHOWMINIMIZED)
        return was;
    window_send(w, WM_SHOWWINDOW, show, 0);
    if (!show) {
        XUnmapWindow(d, w->xid);
        w->visible = false;
        w->style &= ~WS_VISIBLE;
    } else if (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED) {
        XIconifyWindow(d, w->xid, DefaultScreen(d));
    } else {
        XMapRaised(d, w->xid);
        w->visible = true;
        w->style |= WS_VISIBLE;
        w->needs_paint = true;
        if (!was) {
            focus = h;
            window_send(w, WM_ACTIVATE, WA_ACTIVE, 0);
            window_send(w, WM_SETFOCUS, 0, 0);
        }
    }
    XFlush(d);
    return was;
}

static BOOL WINAPI UpdateWindow(HWND h)
{
    struct window *w = window_get(h);
    if (!w)
        return FALSE;
    if (w->needs_paint && w->visible)
        window_send(w, WM_PAINT, 0, 0);
    return TRUE;
}

static BOOL WINAPI IsWindow(HWND h) { return window_get(h) != NULL; }

static BOOL WINAPI IsWindowVisible(HWND h)
{
    struct window *w = window_get(h);
    return w && w->visible;
}

static BOOL WINAPI IsIconic(HWND h) { return FALSE; }
static BOOL WINAPI IsZoomed(HWND h) { return FALSE; }
static BOOL WINAPI IsWindowEnabled(HWND h) { struct window *w = window_get(h); return w && !(w->style & WS_DISABLED); }

static BOOL WINAPI EnableWindow(HWND h, BOOL on)
{
    struct window *w = window_get(h);
    if (!w)
        return FALSE;
    BOOL was_disabled = (w->style & WS_DISABLED) != 0;
    w->style = on ? w->style & ~WS_DISABLED : w->style | WS_DISABLED;
    return was_disabled;
}

static void root_origin(struct window *w, int *x, int *y)
{
    Display *d = x11();
    Window child;
    if (!XTranslateCoordinates(d, w->xid, DefaultRootWindow(d), 0, 0, x, y, &child)) {
        *x = w->x;
        *y = w->y;
    }
}

static BOOL WINAPI GetClientRect(HWND h, RECT *r)
{
    struct window *w = window_get(h);
    if (!w) {
        if (h == DESKTOP || !h) {
            int sw, sh;
            display_size(&sw, &sh);
            *r = (RECT){ 0, 0, sw, sh };
            return TRUE;
        }
        last_error_set(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    *r = (RECT){ 0, 0, w->w, w->h };
    return TRUE;
}

static BOOL WINAPI GetWindowRect(HWND h, RECT *r)
{
    struct window *w = window_get(h);
    if (!w)
        return GetClientRect(h, r);
    int x, y;
    root_origin(w, &x, &y);
    *r = (RECT){ x, y, x + w->w, y + w->h };
    return TRUE;
}

static BOOL WINAPI ClientToScreen(HWND h, POINT *p)
{
    struct window *w = window_get(h);
    if (!w)
        return FALSE;
    int x, y;
    root_origin(w, &x, &y);
    p->x += x;
    p->y += y;
    return TRUE;
}

static BOOL WINAPI ScreenToClient(HWND h, POINT *p)
{
    struct window *w = window_get(h);
    if (!w)
        return FALSE;
    int x, y;
    root_origin(w, &x, &y);
    p->x -= x;
    p->y -= y;
    return TRUE;
}

static int WINAPI MapWindowPoints(HWND from, HWND to, POINT *pts, UINT n)
{
    for (UINT i = 0; i < n; i++) {
        if (from && from != DESKTOP)
            ClientToScreen(from, &pts[i]);
        if (to && to != DESKTOP)
            ScreenToClient(to, &pts[i]);
    }
    return 0;
}

static BOOL WINAPI AdjustWindowRect(RECT *r, DWORD style, BOOL menu) { return TRUE; }
static BOOL WINAPI AdjustWindowRectEx(RECT *r, DWORD style, BOOL menu, DWORD ex) { return TRUE; }
static BOOL WINAPI AdjustWindowRectExForDpi(RECT *r, DWORD style, BOOL menu, DWORD ex, UINT dpi) { return TRUE; }

static BOOL WINAPI SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    struct window *w = window_get(h);
    if (!w)
        return FALSE;
    Display *d = x11();
    int nx = flags & SWP_NOMOVE ? w->x : x, ny = flags & SWP_NOMOVE ? w->y : y;
    int nw = flags & SWP_NOSIZE ? w->w : (cx > 0 ? cx : 1), nh = flags & SWP_NOSIZE ? w->h : (cy > 0 ? cy : 1);
    if (!(flags & SWP_NOMOVE) || !(flags & SWP_NOSIZE))
        XMoveResizeWindow(d, w->xid, nx, ny, nw, nh);
    if (after == HWND_TOPMOST)
        XRaiseWindow(d, w->xid);
    window_moved(w, nx, ny, nw, nh);
    fullscreen(w, covers_screen(w));
    if (flags & SWP_SHOWWINDOW)
        ShowWindow(h, 5);
    if (flags & SWP_HIDEWINDOW)
        ShowWindow(h, SW_HIDE);
    XFlush(d);
    return TRUE;
}

static BOOL WINAPI MoveWindow(HWND h, int x, int y, int cx, int cy, BOOL repaint)
{
    return SetWindowPos(h, NULL, x, y, cx, cy, 0);
}

static LONG_PTR get_long(HWND h, int index)
{
    struct window *w = window_get(h);
    if (!w) {
        last_error_set(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    switch (index) {
    case GWL_WNDPROC: return (LONG_PTR)w->proc;
    case GWL_HINSTANCE: return (LONG_PTR)w->instance;
    case GWL_HWNDPARENT: return (LONG_PTR)w->parent;
    case GWL_STYLE: return w->style;
    case GWL_EXSTYLE: return w->ex_style;
    case GWL_USERDATA: return w->userdata;
    case GWL_ID: return w->id;
    default:
        if (index >= 0 && index + (int)sizeof(LONG_PTR) <= w->cls->wnd_extra) {
            LONG_PTR v;
            memcpy(&v, w->extra + index, sizeof v);
            return v;
        }
        last_error_set(ERROR_INVALID_PARAMETER);
        return 0;
    }
}

static LONG_PTR set_long(HWND h, int index, LONG_PTR value, size_t width)
{
    struct window *w = window_get(h);
    if (!w) {
        last_error_set(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    LONG_PTR old = get_long(h, index);
    switch (index) {
    case GWL_WNDPROC: w->proc = (WNDPROC)value; break;
    case GWL_HINSTANCE: w->instance = (HINSTANCE)value; break;
    case GWL_HWNDPARENT: w->parent = (HWND)value; break;
    case GWL_STYLE:
        w->style = (DWORD)value;
        decorations(w);
        fullscreen(w, covers_screen(w));
        XFlush(x11());
        break;
    case GWL_EXSTYLE: w->ex_style = (DWORD)value; break;
    case GWL_USERDATA: w->userdata = value; break;
    case GWL_ID: w->id = value; break;
    default:
        if (index >= 0 && index + (int)width <= w->cls->wnd_extra)
            memcpy(w->extra + index, &value, width);
        else
            last_error_set(ERROR_INVALID_PARAMETER);
        break;
    }
    return old;
}

static LONG_PTR WINAPI GetWindowLongPtrA(HWND h, int i) { return get_long(h, i); }
static LONG_PTR WINAPI GetWindowLongPtrW(HWND h, int i) { return get_long(h, i); }
static LONG WINAPI GetWindowLongA(HWND h, int i) { return (LONG)get_long(h, i); }
static LONG WINAPI GetWindowLongW(HWND h, int i) { return (LONG)get_long(h, i); }
static LONG_PTR WINAPI SetWindowLongPtrA(HWND h, int i, LONG_PTR v) { return set_long(h, i, v, 8); }
static LONG_PTR WINAPI SetWindowLongPtrW(HWND h, int i, LONG_PTR v) { return set_long(h, i, v, 8); }
static LONG WINAPI SetWindowLongA(HWND h, int i, LONG v) { return (LONG)set_long(h, i, v, 4); }
static LONG WINAPI SetWindowLongW(HWND h, int i, LONG v) { return (LONG)set_long(h, i, v, 4); }

static BOOL WINAPI SetWindowTextA(HWND h, LPCSTR t)
{
    struct window *w = window_get(h);
    if (!w)
        return FALSE;
    set_title(w, t);
    return TRUE;
}

static BOOL WINAPI SetWindowTextW(HWND h, LPCWSTR t)
{
    struct window *w = window_get(h);
    if (!w)
        return FALSE;
    char *u = wide_to_host(t);
    set_title(w, u);
    free(u);
    return TRUE;
}

static int WINAPI GetWindowTextA(HWND h, LPSTR out, int n)
{
    struct window *w = window_get(h);
    if (!w || n <= 0)
        return 0;
    snprintf(out, n, "%s", w->title ? w->title : "");
    return strlen(out);
}

static int WINAPI GetWindowTextW(HWND h, LPWSTR out, int n)
{
    struct window *w = window_get(h);
    if (!w || n <= 0)
        return 0;
    int r = utf8_to_utf16(w->title ? w->title : "", -1, out, n);
    if (r < 0) {
        out[n - 1] = 0;
        return n - 1;
    }
    return r - 1;
}

static int WINAPI GetWindowTextLengthA(HWND h)
{
    struct window *w = window_get(h);
    return w && w->title ? (int)strlen(w->title) : 0;
}

static int WINAPI GetWindowTextLengthW(HWND h)
{
    struct window *w = window_get(h);
    return w && w->title ? utf8_to_utf16(w->title, -1, NULL, 0) - 1 : 0;
}

static LRESULT def_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    struct window *w = window_get(h);
    switch (msg) {
    case WM_NCCREATE: return TRUE;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
        if (w)
            w->needs_paint = false;
        return 0;
    case WM_NCHITTEST: return HTCLIENT;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_CLOSE)
            return window_send(w, WM_CLOSE, 0, 0);
        return 0;
    case WM_GETTEXTLENGTH: return w && w->title ? (LRESULT)strlen(w->title) : 0;
    default: return 0;
    }
}

static LRESULT WINAPI DefWindowProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SETTEXT)
        return SetWindowTextA(h, (LPCSTR)lp);
    if (msg == WM_GETTEXT)
        return GetWindowTextA(h, (LPSTR)lp, (int)wp);
    return def_proc(h, msg, wp, lp);
}

static LRESULT WINAPI DefWindowProcW(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SETTEXT)
        return SetWindowTextW(h, (LPCWSTR)lp);
    if (msg == WM_GETTEXT)
        return GetWindowTextW(h, (LPWSTR)lp, (int)wp);
    if (msg == WM_GETTEXTLENGTH)
        return GetWindowTextLengthW(h);
    return def_proc(h, msg, wp, lp);
}

static LRESULT WINAPI CallWindowProcA(WNDPROC proc, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return proc ? proc(h, msg, wp, lp) : 0;
}

static LRESULT WINAPI CallWindowProcW(WNDPROC proc, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return proc ? proc(h, msg, wp, lp) : 0;
}

static HWND WINAPI GetDesktopWindow(void) { return DESKTOP; }
static HWND WINAPI GetForegroundWindow(void) { return focus; }
static HWND WINAPI GetActiveWindow(void) { return focus; }
static HWND WINAPI GetFocus(void) { return focus; }

static HWND WINAPI SetFocus(HWND h)
{
    HWND old = focus;
    struct window *w = window_get(h);
    if (w && w->visible) {
        XSetInputFocus(x11(), w->xid, RevertToParent, CurrentTime);
        XFlush(x11());
    }
    focus = h;
    return old;
}

static HWND WINAPI SetActiveWindow(HWND h) { return SetFocus(h); }

static BOOL WINAPI SetForegroundWindow(HWND h)
{
    struct window *w = window_get(h);
    if (!w)
        return FALSE;
    XRaiseWindow(x11(), w->xid);
    SetFocus(h);
    return TRUE;
}

static BOOL WINAPI BringWindowToTop(HWND h) { return SetForegroundWindow(h); }

static HWND WINAPI GetParent(HWND h)
{
    struct window *w = window_get(h);
    return w ? w->parent : NULL;
}

static HWND WINAPI GetAncestor(HWND h, UINT flags)
{
    struct window *w = window_get(h);
    while (w && w->parent && window_get(w->parent))
        w = window_get(w->parent);
    return w ? w->hwnd : NULL;
}

static HWND find(const char *cls, const char *title)
{
    char key[128] = "";
    if (cls)
        class_key(cls, false, key);
    pthread_mutex_lock(&lock);
    HWND r = NULL;
    for (size_t i = 0; i < MAX_WINDOWS && !r; i++) {
        struct window *w = windows[i];
        if (!w || w->destroyed)
            continue;
        if (cls && strcmp(w->cls->name, key))
            continue;
        if (title && (!w->title || strcmp(w->title, title)))
            continue;
        r = w->hwnd;
    }
    pthread_mutex_unlock(&lock);
    return r;
}

static HWND WINAPI FindWindowA(LPCSTR cls, LPCSTR title) { return find(cls, title); }

static HWND WINAPI FindWindowW(LPCWSTR cls, LPCWSTR title)
{
    char *c = cls ? wide_to_host(cls) : NULL, *t = title ? wide_to_host(title) : NULL;
    HWND r = find(c, t);
    free(c);
    free(t);
    return r;
}

static DWORD WINAPI GetWindowThreadProcessId(HWND h, DWORD *pid)
{
    struct window *w = window_get(h);
    if (pid)
        *pid = teb_current()->process_id;
    return w ? (DWORD)w->thread : 0;
}

static BOOL WINAPI InvalidateRect(HWND h, const RECT *r, BOOL erase)
{
    struct window *w = window_get(h);
    if (w) {
        w->needs_paint = true;
        queue_wake();
    }
    return TRUE;
}

static BOOL WINAPI ValidateRect(HWND h, const RECT *r)
{
    struct window *w = window_get(h);
    if (w)
        w->needs_paint = false;
    return TRUE;
}

static BOOL WINAPI RedrawWindow(HWND h, const RECT *r, void *rgn, UINT flags) { return InvalidateRect(h, r, TRUE); }

BIT_TABLE(u32_window,
          BIT_FN(CreateWindowExA), BIT_FN(CreateWindowExW), BIT_FN(DestroyWindow), BIT_FN(ShowWindow),
          BIT_FN(UpdateWindow), BIT_FN(IsWindow), BIT_FN(IsWindowVisible), BIT_FN(IsIconic), BIT_FN(IsZoomed),
          BIT_FN(IsWindowEnabled), BIT_FN(EnableWindow),
          BIT_FN(GetClientRect), BIT_FN(GetWindowRect), BIT_FN(ClientToScreen), BIT_FN(ScreenToClient),
          BIT_FN(MapWindowPoints), BIT_FN(AdjustWindowRect), BIT_FN(AdjustWindowRectEx),
          BIT_FN(AdjustWindowRectExForDpi), BIT_FN(SetWindowPos), BIT_FN(MoveWindow),
          BIT_FN(GetWindowLongPtrA), BIT_FN(GetWindowLongPtrW), BIT_FN(GetWindowLongA), BIT_FN(GetWindowLongW),
          BIT_FN(SetWindowLongPtrA), BIT_FN(SetWindowLongPtrW), BIT_FN(SetWindowLongA), BIT_FN(SetWindowLongW),
          BIT_FN(SetWindowTextA), BIT_FN(SetWindowTextW), BIT_FN(GetWindowTextA), BIT_FN(GetWindowTextW),
          BIT_FN(GetWindowTextLengthA), BIT_FN(GetWindowTextLengthW),
          BIT_FN(DefWindowProcA), BIT_FN(DefWindowProcW), BIT_FN(CallWindowProcA), BIT_FN(CallWindowProcW),
          BIT_FN(GetDesktopWindow), BIT_FN(GetForegroundWindow), BIT_FN(GetActiveWindow), BIT_FN(GetFocus),
          BIT_FN(SetFocus), BIT_FN(SetActiveWindow), BIT_FN(SetForegroundWindow), BIT_FN(BringWindowToTop),
          BIT_FN(GetParent), BIT_FN(GetAncestor), BIT_FN(FindWindowA), BIT_FN(FindWindowW),
          BIT_FN(GetWindowThreadProcessId), BIT_FN(InvalidateRect), BIT_FN(ValidateRect), BIT_FN(RedrawWindow));
