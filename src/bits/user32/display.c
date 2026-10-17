#include "u32.h"

#include <screw/log.h>

#include <pthread.h>

static Display *dpy;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void connect_display(void)
{
    XInitThreads();
    dpy = XOpenDisplay(NULL);
    if (!dpy)
        warn("no X11 display available; windows cannot be shown");
    else
        trace("connected to X11 display %s", DisplayString(dpy));
}

Display *x11(void)
{
    pthread_once(&once, connect_display);
    return dpy;
}

Atom x11_atom(const char *name)
{
    Display *d = x11();
    return d ? XInternAtom(d, name, False) : None;
}

void *display_x11(void) { return x11(); }

unsigned long display_window(HWND hwnd)
{
    struct window *w = window_get(hwnd);
    return w ? w->xid : 0;
}

HWND display_hwnd(unsigned long xid)
{
    struct window *w = window_by_xid(xid);
    return w ? w->hwnd : NULL;
}

void display_size(int *w, int *h)
{
    Display *d = x11();
    *w = d ? DisplayWidth(d, DefaultScreen(d)) : 1920;
    *h = d ? DisplayHeight(d, DefaultScreen(d)) : 1080;
}
