#ifndef SCREW_PATH_H
#define SCREW_PATH_H

#include <screw/win.h>

void path_init(void);
const char *path_c_root(void);

char *path_to_host(const char *win);
char *path_to_host_w(const WCHAR *win);
char *path_to_windows(const char *host);

DWORD path_errno_to_error(int err);

#endif
