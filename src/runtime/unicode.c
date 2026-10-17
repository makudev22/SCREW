#include <screw/unicode.h>

#include <stdlib.h>
#include <string.h>

size_t wstr_len(const WCHAR *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int wstr_cmp(const WCHAR *a, const WCHAR *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (int)*a - (int)*b;
}

WCHAR *wstr_dup(const WCHAR *s)
{
    size_t n = (wstr_len(s) + 1) * sizeof(WCHAR);
    return memcpy(malloc(n), s, n);
}

static int put(char *dst, int dst_len, int pos, uint32_t cp)
{
    char buf[4];
    int n;
    if (cp < 0x80) {
        buf[0] = cp;
        n = 1;
    } else if (cp < 0x800) {
        buf[0] = 0xC0 | (cp >> 6);
        buf[1] = 0x80 | (cp & 0x3F);
        n = 2;
    } else if (cp < 0x10000) {
        buf[0] = 0xE0 | (cp >> 12);
        buf[1] = 0x80 | ((cp >> 6) & 0x3F);
        buf[2] = 0x80 | (cp & 0x3F);
        n = 3;
    } else {
        buf[0] = 0xF0 | (cp >> 18);
        buf[1] = 0x80 | ((cp >> 12) & 0x3F);
        buf[2] = 0x80 | ((cp >> 6) & 0x3F);
        buf[3] = 0x80 | (cp & 0x3F);
        n = 4;
    }
    if (dst) {
        if (pos + n > dst_len)
            return -1;
        memcpy(dst + pos, buf, n);
    }
    return n;
}

int utf16_to_utf8(const WCHAR *src, int src_len, char *dst, int dst_len)
{
    if (src_len < 0)
        src_len = wstr_len(src) + 1;
    int out = 0;
    for (int i = 0; i < src_len; i++) {
        uint32_t cp = src[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < src_len && src[i + 1] >= 0xDC00 && src[i + 1] < 0xE000)
            cp = 0x10000 + ((cp - 0xD800) << 10) + (src[++i] - 0xDC00);
        else if (cp >= 0xD800 && cp < 0xE000)
            cp = 0xFFFD;
        int n = put(dst, dst_len, out, cp);
        if (n < 0)
            return -1;
        out += n;
    }
    return out;
}

static uint32_t take(const unsigned char *s, int len, int *i)
{
    unsigned c = s[*i];
    int extra = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
    if (c >= 0x80 && c < 0xC0)
        return (*i)++, 0xFFFD;
    uint32_t cp = extra ? c & (0x3F >> extra) : c;
    (*i)++;
    for (int k = 0; k < extra; k++, (*i)++) {
        if (*i >= len || (s[*i] & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (s[*i] & 0x3F);
    }
    return cp;
}

int utf8_to_utf16(const char *src, int src_len, WCHAR *dst, int dst_len)
{
    if (src_len < 0)
        src_len = strlen(src) + 1;
    const unsigned char *s = (const unsigned char *)src;
    int out = 0;
    for (int i = 0; i < src_len;) {
        uint32_t cp = take(s, src_len, &i);
        int need = cp >= 0x10000 ? 2 : 1;
        if (dst) {
            if (out + need > dst_len)
                return -1;
            if (need == 2) {
                cp -= 0x10000;
                dst[out] = 0xD800 | (cp >> 10);
                dst[out + 1] = 0xDC00 | (cp & 0x3FF);
            } else {
                dst[out] = cp;
            }
        }
        out += need;
    }
    return out;
}

char *wide_to_host(const WCHAR *s)
{
    if (!s)
        return NULL;
    int n = utf16_to_utf8(s, -1, NULL, 0);
    char *out = malloc(n);
    utf16_to_utf8(s, -1, out, n);
    return out;
}

WCHAR *host_to_wide(const char *s)
{
    if (!s)
        return NULL;
    int n = utf8_to_utf16(s, -1, NULL, 0);
    WCHAR *out = malloc(n * sizeof *out);
    utf8_to_utf16(s, -1, out, n);
    return out;
}
