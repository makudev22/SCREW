#ifndef SCREW_DISPLAY_H
#define SCREW_DISPLAY_H

#include <screw/win.h>

void *display_x11(void);
unsigned long display_window(HWND hwnd);
HWND display_hwnd(unsigned long xid);
void display_size(int *w, int *h);

#endif
