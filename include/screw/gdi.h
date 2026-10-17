#ifndef SCREW_GDI_H
#define SCREW_GDI_H

#include <screw/win.h>

void *gdi_window_dc(HWND hwnd);
int gdi_release_dc(HWND hwnd, void *hdc);

#endif
