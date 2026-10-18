#include "u32.h"

#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include <pthread.h>

#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_NUMPAD0 0x60
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE

#define KEY_DOWN 0x80
#define KEY_TOGGLE 0x01
#define WHEEL_DELTA 120

#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2

static uint8_t key_state[256];
static int cursor_x, cursor_y;
static int cursor_shown = 1;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned vk_from_keysym(KeySym ks)
{
    if (ks >= XK_a && ks <= XK_z)
        return 'A' + (ks - XK_a);
    if (ks >= XK_A && ks <= XK_Z)
        return ks;
    if (ks >= XK_0 && ks <= XK_9)
        return ks;
    if (ks >= XK_F1 && ks <= XK_F24)
        return VK_F1 + (ks - XK_F1);
    if (ks >= XK_KP_0 && ks <= XK_KP_9)
        return VK_NUMPAD0 + (ks - XK_KP_0);
    switch (ks) {
    case XK_BackSpace: return VK_BACK;
    case XK_Tab: return VK_TAB;
    case XK_Return:
    case XK_KP_Enter: return VK_RETURN;
    case XK_Escape: return VK_ESCAPE;
    case XK_space: return VK_SPACE;
    case XK_Shift_L: return VK_LSHIFT;
    case XK_Shift_R: return VK_RSHIFT;
    case XK_Control_L: return VK_LCONTROL;
    case XK_Control_R: return VK_RCONTROL;
    case XK_Alt_L: return VK_LMENU;
    case XK_Alt_R: return VK_RMENU;
    case XK_Caps_Lock: return VK_CAPITAL;
    case XK_Pause: return VK_PAUSE;
    case XK_Super_L: return VK_LWIN;
    case XK_Super_R: return VK_RWIN;
    case XK_Prior: return VK_PRIOR;
    case XK_Next: return VK_NEXT;
    case XK_End: return VK_END;
    case XK_Home: return VK_HOME;
    case XK_Left: return VK_LEFT;
    case XK_Up: return VK_UP;
    case XK_Right: return VK_RIGHT;
    case XK_Down: return VK_DOWN;
    case XK_Insert: return VK_INSERT;
    case XK_Delete: return VK_DELETE;
    case XK_Print: return VK_SNAPSHOT;
    case XK_KP_Add: return VK_ADD;
    case XK_KP_Subtract: return VK_SUBTRACT;
    case XK_KP_Multiply: return VK_MULTIPLY;
    case XK_KP_Divide: return VK_DIVIDE;
    case XK_KP_Decimal: return VK_DECIMAL;
    case XK_semicolon: return VK_OEM_1;
    case XK_slash: return VK_OEM_2;
    case XK_grave: return VK_OEM_3;
    case XK_bracketleft: return VK_OEM_4;
    case XK_backslash: return VK_OEM_5;
    case XK_bracketright: return VK_OEM_6;
    case XK_apostrophe: return VK_OEM_7;
    case XK_equal:
    case XK_plus: return VK_OEM_PLUS;
    case XK_comma: return VK_OEM_COMMA;
    case XK_minus: return VK_OEM_MINUS;
    case XK_period: return VK_OEM_PERIOD;
    default: return 0;
    }
}

static void sync_modifier(unsigned vk, unsigned left, unsigned right)
{
    key_state[vk] = (key_state[left] | key_state[right]) & KEY_DOWN;
}

static void set_key(unsigned vk, bool down)
{
    if (down) {
        key_state[vk] ^= KEY_TOGGLE;
        key_state[vk] |= KEY_DOWN;
    } else {
        key_state[vk] &= ~KEY_DOWN;
    }
    if (vk == VK_LSHIFT || vk == VK_RSHIFT)
        sync_modifier(VK_SHIFT, VK_LSHIFT, VK_RSHIFT);
    else if (vk == VK_LCONTROL || vk == VK_RCONTROL)
        sync_modifier(VK_CONTROL, VK_LCONTROL, VK_RCONTROL);
    else if (vk == VK_LMENU || vk == VK_RMENU)
        sync_modifier(VK_MENU, VK_LMENU, VK_RMENU);
}

static WPARAM mouse_keys(unsigned xstate)
{
    WPARAM w = 0;
    if (xstate & Button1Mask)
        w |= MK_LBUTTON;
    if (xstate & Button2Mask)
        w |= MK_MBUTTON;
    if (xstate & Button3Mask)
        w |= MK_RBUTTON;
    if (xstate & ShiftMask)
        w |= MK_SHIFT;
    if (xstate & ControlMask)
        w |= MK_CONTROL;
    return w;
}

void input_event(XEvent *ev, struct window *w)
{
    pthread_mutex_lock(&lock);
    switch (ev->type) {
    case KeyPress:
    case KeyRelease: {
        char buf[16];
        KeySym ks = 0;
        int n = XLookupString(&ev->xkey, buf, sizeof buf, &ks, NULL);
        unsigned vk = vk_from_keysym(ks);
        bool down = ev->type == KeyPress;
        if (vk)
            set_key(vk, down);
        bool alt = key_state[VK_MENU] & KEY_DOWN;
        UINT msg = down ? (alt ? WM_SYSKEYDOWN : WM_KEYDOWN) : (alt ? WM_SYSKEYUP : WM_KEYUP);
        LPARAM lp = 1 | ((LPARAM)(ev->xkey.keycode & 0xFF) << 16);
        if (!down)
            lp |= (LPARAM)3 << 30;
        DWORD ch = down && n > 0 ? (unsigned char)buf[0] : 0;
        cursor_x = ev->xkey.x_root;
        cursor_y = ev->xkey.y_root;
        pthread_mutex_unlock(&lock);
        queue_post_key(w->hwnd, msg, vk, lp, ch);
        return;
    }
    case ButtonPress:
    case ButtonRelease: {
        bool down = ev->type == ButtonPress;
        int bx = ev->xbutton.x, by = ev->xbutton.y;
        cursor_x = ev->xbutton.x_root;
        cursor_y = ev->xbutton.y_root;
        unsigned state = ev->xbutton.state;
        pthread_mutex_unlock(&lock);
        LPARAM lp = make_lparam(bx, by);
        switch (ev->xbutton.button) {
        case 1:
            queue_post(w->hwnd, down ? WM_LBUTTONDOWN : WM_LBUTTONUP, mouse_keys(state) | (down ? MK_LBUTTON : 0), lp);
            break;
        case 2:
            queue_post(w->hwnd, down ? WM_MBUTTONDOWN : WM_MBUTTONUP, mouse_keys(state) | (down ? MK_MBUTTON : 0), lp);
            break;
        case 3:
            queue_post(w->hwnd, down ? WM_RBUTTONDOWN : WM_RBUTTONUP, mouse_keys(state) | (down ? MK_RBUTTON : 0), lp);
            break;
        case 4:
        case 5:
            if (down) {
                int delta = ev->xbutton.button == 4 ? WHEEL_DELTA : -WHEEL_DELTA;
                WPARAM wp = ((WPARAM)(WORD)(SHORT)delta << 16) | mouse_keys(state);
                queue_post(w->hwnd, WM_MOUSEWHEEL, wp, make_lparam(ev->xbutton.x_root, ev->xbutton.y_root));
            }
            break;
        }
        return;
    }
    case MotionNotify: {
        int mx = ev->xmotion.x, my = ev->xmotion.y;
        cursor_x = ev->xmotion.x_root;
        cursor_y = ev->xmotion.y_root;
        unsigned state = ev->xmotion.state;
        pthread_mutex_unlock(&lock);
        queue_post(w->hwnd, WM_MOUSEMOVE, mouse_keys(state), make_lparam(mx, my));
        return;
    }
    default:
        break;
    }
    pthread_mutex_unlock(&lock);
}

static SHORT WINAPI GetAsyncKeyState(int vk)
{
    if (vk < 0 || vk > 255)
        return 0;
    pthread_mutex_lock(&lock);
    SHORT r = (key_state[vk] & KEY_DOWN) ? (SHORT)0x8000 : 0;
    pthread_mutex_unlock(&lock);
    return r;
}

static SHORT WINAPI GetKeyState(int vk)
{
    if (vk < 0 || vk > 255)
        return 0;
    pthread_mutex_lock(&lock);
    SHORT r = ((key_state[vk] & KEY_DOWN) ? (SHORT)0x8000 : 0) | (key_state[vk] & KEY_TOGGLE);
    pthread_mutex_unlock(&lock);
    return r;
}

static BOOL WINAPI GetKeyboardState(uint8_t *out)
{
    if (!out)
        return FALSE;
    pthread_mutex_lock(&lock);
    memcpy(out, key_state, 256);
    pthread_mutex_unlock(&lock);
    return TRUE;
}

static BOOL WINAPI SetKeyboardState(const uint8_t *in)
{
    if (!in)
        return FALSE;
    pthread_mutex_lock(&lock);
    memcpy(key_state, in, 256);
    pthread_mutex_unlock(&lock);
    return TRUE;
}

static BOOL WINAPI GetCursorPos(POINT *p)
{
    if (!p)
        return FALSE;
    pthread_mutex_lock(&lock);
    p->x = cursor_x;
    p->y = cursor_y;
    pthread_mutex_unlock(&lock);
    return TRUE;
}

static BOOL WINAPI SetCursorPos(int x, int y)
{
    Display *d = x11();
    pthread_mutex_lock(&lock);
    cursor_x = x;
    cursor_y = y;
    pthread_mutex_unlock(&lock);
    if (d) {
        XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, x, y);
        XFlush(d);
    }
    return TRUE;
}

static HCURSOR WINAPI SetCursor(HCURSOR c) { return c; }
static HCURSOR WINAPI GetCursor(void) { return NULL; }

static int WINAPI ShowCursor(BOOL show)
{
    pthread_mutex_lock(&lock);
    cursor_shown += show ? 1 : -1;
    int r = cursor_shown;
    pthread_mutex_unlock(&lock);
    return r;
}

static BOOL WINAPI ClipCursor(const RECT *r) { return TRUE; }
static BOOL WINAPI GetClipCursor(RECT *r)
{
    if (r) {
        int w, h;
        display_size(&w, &h);
        *r = (RECT){ 0, 0, w, h };
    }
    return TRUE;
}

static UINT WINAPI MapVirtualKeyW(UINT code, UINT type)
{
    switch (type) {
    case MAPVK_VK_TO_CHAR:
        if (code >= 'A' && code <= 'Z')
            return code;
        if (code >= '0' && code <= '9')
            return code;
        if (code == VK_SPACE)
            return ' ';
        return 0;
    case MAPVK_VK_TO_VSC:
        return code & 0x7F;
    case MAPVK_VSC_TO_VK:
        return code;
    default:
        return 0;
    }
}

static UINT WINAPI MapVirtualKeyA(UINT code, UINT type) { return MapVirtualKeyW(code, type); }
static UINT WINAPI MapVirtualKeyExW(UINT code, UINT type, HANDLE layout) { return MapVirtualKeyW(code, type); }
static UINT WINAPI MapVirtualKeyExA(UINT code, UINT type, HANDLE layout) { return MapVirtualKeyW(code, type); }

static int WINAPI ToUnicode(UINT vk, UINT scan, const uint8_t *state, WCHAR *buf, int size, UINT flags)
{
    if (size < 1)
        return 0;
    unsigned ch = 0;
    bool shift = state && (state[VK_SHIFT] & KEY_DOWN);
    if (vk >= 'A' && vk <= 'Z')
        ch = shift ? vk : vk + 32;
    else if (vk >= '0' && vk <= '9')
        ch = vk;
    else if (vk == VK_SPACE)
        ch = ' ';
    else if (vk == VK_RETURN)
        ch = '\r';
    if (!ch)
        return 0;
    buf[0] = ch;
    return 1;
}

static int WINAPI ToAscii(UINT vk, UINT scan, const uint8_t *state, WORD *out, UINT flags)
{
    WCHAR w;
    int r = ToUnicode(vk, scan, state, &w, 1, flags);
    if (r == 1 && out)
        *out = w;
    return r;
}

static SHORT WINAPI VkKeyScanW(WCHAR ch)
{
    if (ch >= 'a' && ch <= 'z')
        return ch - 32;
    if (ch >= 'A' && ch <= 'Z')
        return ch | 0x100;
    if (ch >= '0' && ch <= '9')
        return ch;
    if (ch == ' ')
        return VK_SPACE;
    return -1;
}

static SHORT WINAPI VkKeyScanA(char ch) { return VkKeyScanW((unsigned char)ch); }

static HANDLE WINAPI CreateIconIndirect(void *info) { return (HANDLE)(uintptr_t)0x20001; }

BIT_TABLE(u32_input,
          BIT_FN(GetAsyncKeyState), BIT_FN(GetKeyState), BIT_FN(GetKeyboardState), BIT_FN(SetKeyboardState),
          BIT_FN(GetCursorPos), BIT_FN(SetCursorPos), BIT_FN(SetCursor), BIT_FN(GetCursor),
          BIT_FN(ShowCursor), BIT_FN(ClipCursor), BIT_FN(GetClipCursor),
          BIT_FN(MapVirtualKeyA), BIT_FN(MapVirtualKeyW), BIT_FN(MapVirtualKeyExA), BIT_FN(MapVirtualKeyExW),
          BIT_FN(ToUnicode), BIT_FN(ToAscii), BIT_FN(VkKeyScanA), BIT_FN(VkKeyScanW),
          BIT_FN(CreateIconIndirect));
