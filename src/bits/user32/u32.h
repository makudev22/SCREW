#ifndef SCREW_U32_H
#define SCREW_U32_H

#include <screw/bit.h>
#include <screw/display.h>
#include <screw/teb.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t UINT_PTR;
typedef uint64_t WPARAM;
typedef int64_t LPARAM;
typedef int64_t LRESULT;
typedef uint16_t ATOM;
typedef void *HINSTANCE;
typedef void *HICON;
typedef void *HCURSOR;
typedef void *HBRUSH;
typedef void *HMENU;
typedef void *HDC;
typedef void *HMONITOR;

typedef LRESULT WINAPI (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct {
    LONG left, top, right, bottom;
} RECT;

typedef struct {
    LONG x, y;
} POINT;

typedef struct {
    HWND hwnd;
    UINT message;
    WPARAM wparam;
    LPARAM lparam;
    DWORD time;
    POINT pt;
    DWORD private_;
} MSG;

typedef struct {
    void *create_params;
    HINSTANCE instance;
    HMENU menu;
    HWND parent;
    int cy, cx, y, x;
    LONG style;
    const void *name;
    const void *class_name;
    DWORD ex_style;
} CREATESTRUCT;

#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCACTIVATE 0x0086
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_MOUSEHWHEEL 0x020E
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_USER 0x0400

#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2

#define WA_INACTIVE 0
#define WA_ACTIVE 1

#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010

#define WS_POPUP 0x80000000u
#define WS_CHILD 0x40000000u
#define WS_VISIBLE 0x10000000u
#define WS_DISABLED 0x08000000u

#define CW_USEDEFAULT ((int)0x80000000)

#define GWL_WNDPROC (-4)
#define GWL_HINSTANCE (-6)
#define GWL_HWNDPARENT (-8)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWL_USERDATA (-21)
#define GWL_ID (-12)

#define SW_HIDE 0
#define SW_SHOWMINIMIZED 2
#define SW_MAXIMIZE 3
#define SW_MINIMIZE 6
#define SW_RESTORE 9

#define PM_REMOVE 0x0001

struct wclass {
    char name[128];
    bool wide;
    WNDPROC proc;
    UINT style;
    int cls_extra;
    int wnd_extra;
    HINSTANCE instance;
    HICON icon;
    HCURSOR cursor;
    HBRUSH background;
    ATOM atom;
    struct wclass *next;
};

struct window {
    HWND hwnd;
    Window xid;
    struct wclass *cls;
    WNDPROC proc;
    bool wide;
    HWND parent;
    DWORD style;
    DWORD ex_style;
    LONG_PTR userdata;
    LONG_PTR id;
    HINSTANCE instance;
    int x, y, w, h;
    bool visible;
    bool needs_paint;
    bool destroyed;
    uint64_t thread;
    char *title;
    uint8_t *extra;
};

Display *x11(void);
Atom x11_atom(const char *name);

struct wclass *class_find(const char *name, ATOM atom);
const char *class_key(const void *name, bool wide, char buf[128]);

struct window *window_get(HWND h);
struct window *window_by_xid(Window xid);
struct window *window_next(struct window *prev);
LRESULT window_send(struct window *w, UINT msg, WPARAM wp, LPARAM lp);
HWND window_focus(void);
void window_set_focus(HWND h);
void window_moved(struct window *w, int x, int y, int width, int height);

void queue_post(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void queue_post_key(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, DWORD ch);
void queue_post_thread(uint64_t thread, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void queue_wake(void);

extern const struct bit_table u32_class;
extern const struct bit_table u32_window;
extern const struct bit_table u32_message;
extern const struct bit_table u32_system;
extern const struct bit_table u32_dialog;
extern const struct bit_table u32_rect;

static inline BOOL fail_win(DWORD error)
{
    last_error_set(error);
    return FALSE;
}

static inline LPARAM make_lparam(int lo, int hi) { return (LPARAM)(DWORD)((WORD)lo | ((DWORD)(WORD)hi << 16)); }

#endif
