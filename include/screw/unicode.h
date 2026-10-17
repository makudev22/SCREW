#ifndef SCREW_UNICODE_H
#define SCREW_UNICODE_H

#include <screw/win.h>

size_t wstr_len(const WCHAR *s);
int wstr_cmp(const WCHAR *a, const WCHAR *b);
WCHAR *wstr_dup(const WCHAR *s);

int utf16_to_utf8(const WCHAR *src, int src_len, char *dst, int dst_len);
int utf8_to_utf16(const char *src, int src_len, WCHAR *dst, int dst_len);

char *wide_to_host(const WCHAR *s);
WCHAR *host_to_wide(const char *s);

#endif
