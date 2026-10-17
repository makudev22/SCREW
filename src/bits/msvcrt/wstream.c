#include "crt.h"

#include <errno.h>

#define WEOF 0xFFFFu
#define PENDING 0x10000

#define W_UPPER 0x001
#define W_LOWER 0x002
#define W_DIGIT 0x004
#define W_SPACE 0x008
#define W_PUNCT 0x010
#define W_CONTROL 0x020
#define W_BLANK 0x040
#define W_HEX 0x080
#define W_ALPHA (0x100 | W_UPPER | W_LOWER)

static WCHAR take_pending(struct wfile *f, bool *have)
{
    *have = f->charbuf & PENDING;
    WCHAR c = (WCHAR)f->charbuf;
    f->charbuf = 0;
    return c;
}

static unsigned read_wide(struct wfile *f)
{
    bool have;
    WCHAR pending = take_pending(f, &have);
    if (have)
        return pending;

    int c = crt_getc(f);
    if (c == EOF)
        return WEOF;
    unsigned char lead = c;
    int extra = lead < 0x80 ? 0 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
    char buf[4] = { (char)lead };
    for (int i = 1; i <= extra; i++) {
        c = crt_getc(f);
        if (c == EOF)
            return 0xFFFD;
        buf[i] = c;
    }
    WCHAR out[2];
    int n = utf8_to_utf16(buf, extra + 1, out, 2);
    if (n == 2)
        f->charbuf = PENDING | out[1];
    return n > 0 ? out[0] : 0xFFFD;
}

static bool emit(const WCHAR *w, int len, struct wfile *f)
{
    char buf[8];
    int n = utf16_to_utf8(w, len, buf, sizeof buf);
    return n > 0 && fwrite(buf, 1, n, crt_host(f)) == (size_t)n;
}

static unsigned write_wide(WCHAR c, struct wfile *f)
{
    if (c >= 0xD800 && c < 0xDC00) {
        f->bufsiz = PENDING | c;
        return c;
    }
    if (c >= 0xDC00 && c < 0xE000 && (f->bufsiz & PENDING)) {
        WCHAR pair[2] = { (WCHAR)f->bufsiz, c };
        f->bufsiz = 0;
        return emit(pair, 2, f) ? c : WEOF;
    }
    f->bufsiz = 0;
    return emit(&c, 1, f) ? c : WEOF;
}

static unsigned WINAPI fgetwc_(struct wfile *f) { return read_wide(f); }
static unsigned WINAPI getwchar_(void) { return read_wide(crt_stream(0)); }
static unsigned WINAPI fputwc_(WCHAR c, struct wfile *f) { return write_wide(c, f); }
static unsigned WINAPI putwchar_(WCHAR c) { return write_wide(c, crt_stream(1)); }

static unsigned WINAPI ungetwc_(unsigned c, struct wfile *f)
{
    if (c == WEOF || (f->charbuf & PENDING))
        return WEOF;
    f->charbuf = PENDING | (WCHAR)c;
    return c;
}

static int WINAPI fputws_(const WCHAR *s, struct wfile *f)
{
    int n = utf16_to_utf8(s, wstr_len(s), NULL, 0);
    char *u = malloc(n + 1);
    utf16_to_utf8(s, wstr_len(s), u, n);
    size_t done = fwrite(u, 1, n, crt_host(f));
    free(u);
    return done == (size_t)n ? 0 : -1;
}

static WCHAR *WINAPI fgetws_(WCHAR *buf, int n, struct wfile *f)
{
    if (n <= 0)
        return NULL;
    int i = 0;
    while (i < n - 1) {
        unsigned c = read_wide(f);
        if (c == WEOF)
            break;
        buf[i++] = c;
        if (c == '\n')
            break;
    }
    if (!i)
        return NULL;
    buf[i] = 0;
    return buf;
}

static size_t WINAPI mbsrtowcs_(WCHAR *dst, const char **src, size_t n, void *state)
{
    const char *s = *src;
    int need = utf8_to_utf16(s, -1, NULL, 0) - 1;
    if (need < 0) {
        crt_errno_set(EILSEQ);
        return (size_t)-1;
    }
    if (!dst)
        return need;
    WCHAR *tmp = malloc((need + 1) * sizeof *tmp);
    utf8_to_utf16(s, -1, tmp, need + 1);
    size_t copy = (size_t)need < n ? (size_t)need : n;
    memcpy(dst, tmp, copy * sizeof *tmp);
    free(tmp);
    if (copy == (size_t)need) {
        if (copy < n)
            dst[copy] = 0;
        *src = NULL;
    } else {
        size_t bytes = 0;
        for (size_t units = 0; units < copy && s[bytes];) {
            unsigned char c = s[bytes];
            int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            if (units + (len == 4 ? 2 : 1) > copy)
                break;
            units += len == 4 ? 2 : 1;
            bytes += len;
        }
        *src = s + bytes;
    }
    return copy;
}

static unsigned short WINAPI wctype_(const char *name)
{
    static const struct {
        const char *name;
        unsigned short mask;
    } classes[] = {
        { "alnum", W_ALPHA | W_DIGIT },
        { "alpha", W_ALPHA },
        { "blank", W_BLANK },
        { "cntrl", W_CONTROL },
        { "digit", W_DIGIT },
        { "graph", W_PUNCT | W_ALPHA | W_DIGIT },
        { "lower", W_LOWER },
        { "print", W_BLANK | W_PUNCT | W_ALPHA | W_DIGIT },
        { "punct", W_PUNCT },
        { "space", W_SPACE },
        { "upper", W_UPPER },
        { "xdigit", W_HEX },
    };
    for (size_t i = 0; i < sizeof classes / sizeof classes[0]; i++)
        if (!strcmp(name, classes[i].name))
            return classes[i].mask;
    return 0;
}

BIT_TABLE(crt_wstream,
          BIT_AS("fgetwc", fgetwc_), BIT_AS("getwc", fgetwc_), BIT_AS("_fgetwc_nolock", fgetwc_),
          BIT_AS("getwchar", getwchar_),
          BIT_AS("fputwc", fputwc_), BIT_AS("putwc", fputwc_), BIT_AS("_fputwc_nolock", fputwc_),
          BIT_AS("putwchar", putwchar_), BIT_AS("ungetwc", ungetwc_), BIT_AS("_ungetwc_nolock", ungetwc_),
          BIT_AS("fputws", fputws_), BIT_AS("fgetws", fgetws_),
          BIT_AS("mbsrtowcs", mbsrtowcs_), BIT_AS("wctype", wctype_));
