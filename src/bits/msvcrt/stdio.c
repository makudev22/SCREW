#include "crt.h"

#include <screw/path.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h>

#define IOB_COUNT 20
#define FLAG_TEXT 0x10000

static struct wfile iob[IOB_COUNT];
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void setup(void)
{
    iob[0].tmpfname = (char *)stdin;
    iob[1].tmpfname = (char *)stdout;
    iob[2].tmpfname = (char *)stderr;
    for (int i = 0; i < 3; i++) {
        iob[i].file = i;
        iob[i].flag = FLAG_TEXT;
    }
}

struct wfile *crt_stream(int index)
{
    pthread_once(&once, setup);
    return &iob[index];
}

FILE *crt_host(struct wfile *f)
{
    pthread_once(&once, setup);
    return f ? (FILE *)f->tmpfname : NULL;
}

void crt_flush_all(void) { fflush(NULL); }

static bool text(struct wfile *f) { return f->flag & FLAG_TEXT; }

static struct wfile *wrap(FILE *host, bool text_mode)
{
    if (!host)
        return NULL;
    struct wfile *f = calloc(1, sizeof *f);
    f->tmpfname = (char *)host;
    f->file = fileno(host);
    f->flag = text_mode ? FLAG_TEXT : 0;
    return f;
}

static bool host_mode(const char *mode, char out[8])
{
    size_t n = 0;
    bool binary = false;
    for (const char *p = mode; *p && *p != ',' && n < 6; p++) {
        if (*p == 'b')
            binary = true;
        if (strchr("rwa+", *p))
            out[n++] = *p;
    }
    out[n] = 0;
    return !binary;
}

static struct wfile *open_file(const char *name, const char *mode)
{
    char hm[8];
    bool text_mode = host_mode(mode, hm);
    char *host = path_to_host(name);
    if (!host) {
        crt_errno_set(ENOENT);
        return NULL;
    }
    FILE *fp = fopen(host, hm);
    if (!fp)
        crt_errno_set(errno);
    free(host);
    return wrap(fp, text_mode);
}

static struct wfile *WINAPI fopen_(const char *name, const char *mode) { return open_file(name, mode); }

static struct wfile *WINAPI _wfopen(const WCHAR *name, const WCHAR *mode)
{
    char *n = wide_to_host(name), *m = wide_to_host(mode);
    struct wfile *f = open_file(n, m);
    free(n);
    free(m);
    return f;
}

static int WINAPI fopen_s(struct wfile **out, const char *name, const char *mode)
{
    *out = open_file(name, mode);
    return *out ? 0 : *crt_errno();
}

static int WINAPI _wfopen_s(struct wfile **out, const WCHAR *name, const WCHAR *mode)
{
    *out = _wfopen(name, mode);
    return *out ? 0 : *crt_errno();
}

static struct wfile *WINAPI freopen_(const char *name, const char *mode, struct wfile *f)
{
    char hm[8];
    bool text_mode = host_mode(mode, hm);
    char *host = name ? path_to_host(name) : NULL;
    FILE *fp = freopen(host, hm, crt_host(f));
    free(host);
    if (!fp)
        return NULL;
    f->tmpfname = (char *)fp;
    f->flag = text_mode ? FLAG_TEXT : 0;
    return f;
}

static struct wfile *WINAPI _fdopen(int fd, const char *mode)
{
    char hm[8];
    bool text_mode = host_mode(mode, hm);
    return wrap(fdopen(fd, hm), text_mode);
}

static int WINAPI fclose_(struct wfile *f)
{
    if (!f)
        return EOF;
    int r = fclose(crt_host(f));
    if (f < iob || f >= iob + IOB_COUNT)
        free(f);
    return r;
}

static int WINAPI _fcloseall(void) { return 0; }

static int getc_text(struct wfile *f)
{
    FILE *h = crt_host(f);
    int c = getc(h);
    if (c == '\r' && text(f)) {
        int d = getc(h);
        if (d == '\n')
            return '\n';
        if (d != EOF)
            ungetc(d, h);
    }
    return c;
}

int crt_getc(struct wfile *f) { return getc_text(f); }

static int WINAPI fgetc_(struct wfile *f) { return getc_text(f); }
static int WINAPI getchar_(void) { return getc_text(crt_stream(0)); }

static char *WINAPI fgets_(char *buf, int n, struct wfile *f)
{
    if (n <= 0)
        return NULL;
    int i = 0;
    while (i < n - 1) {
        int c = getc_text(f);
        if (c == EOF)
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

static size_t WINAPI fread_(void *buf, size_t size, size_t count, struct wfile *f)
{
    if (!text(f) || !size)
        return fread(buf, size, count, crt_host(f));
    size_t total = size * count, got = 0;
    char *p = buf;
    while (got < total) {
        int c = getc_text(f);
        if (c == EOF)
            break;
        p[got++] = c;
    }
    return got / size;
}

static size_t WINAPI fwrite_(const void *buf, size_t size, size_t count, struct wfile *f)
{
    return fwrite(buf, size, count, crt_host(f));
}

static int WINAPI fputc_(int c, struct wfile *f) { return fputc(c, crt_host(f)); }
static int WINAPI putchar_(int c) { return fputc(c, stdout); }
static int WINAPI fputs_(const char *s, struct wfile *f) { return fputs(s, crt_host(f)) < 0 ? EOF : 0; }
static int WINAPI puts_(const char *s) { return puts(s) < 0 ? EOF : 0; }
static int WINAPI ungetc_(int c, struct wfile *f) { return ungetc(c, crt_host(f)); }

static int WINAPI fflush_(struct wfile *f) { return fflush(f ? crt_host(f) : NULL); }

static int WINAPI fseek_(struct wfile *f, LONG off, int whence) { return fseeko(crt_host(f), off, whence); }
static int WINAPI _fseeki64(struct wfile *f, int64_t off, int whence) { return fseeko(crt_host(f), off, whence); }
static LONG WINAPI ftell_(struct wfile *f) { return (LONG)ftello(crt_host(f)); }
static int64_t WINAPI _ftelli64(struct wfile *f) { return ftello(crt_host(f)); }

static int WINAPI fgetpos_(struct wfile *f, int64_t *pos)
{
    *pos = ftello(crt_host(f));
    return *pos < 0 ? -1 : 0;
}

static int WINAPI fsetpos_(struct wfile *f, const int64_t *pos) { return fseeko(crt_host(f), *pos, SEEK_SET); }
static void WINAPI rewind_(struct wfile *f) { rewind(crt_host(f)); }
static int WINAPI feof_(struct wfile *f) { return feof(crt_host(f)); }
static int WINAPI ferror_(struct wfile *f) { return ferror(crt_host(f)); }
static void WINAPI clearerr_(struct wfile *f) { clearerr(crt_host(f)); }

static int WINAPI setvbuf_(struct wfile *f, char *buf, int mode, size_t size)
{
    int m = mode == 4 ? _IONBF : mode == 0x40 ? _IOLBF : _IOFBF;
    return setvbuf(crt_host(f), buf, m, size);
}

static void WINAPI setbuf_(struct wfile *f, char *buf) { setbuf(crt_host(f), buf); }
static int WINAPI _fileno(struct wfile *f) { return fileno(crt_host(f)); }
static struct wfile *WINAPI tmpfile_(void) { return wrap(tmpfile(), false); }

static int on_path(const char *name, int (*op)(const char *))
{
    char *host = path_to_host(name);
    int r = host ? op(host) : -1;
    if (r)
        crt_errno_set(host ? errno : ENOENT);
    free(host);
    return r;
}

static int WINAPI remove_(const char *name) { return on_path(name, remove); }

static int WINAPI _wremove(const WCHAR *name)
{
    char *n = wide_to_host(name);
    int r = on_path(n, remove);
    free(n);
    return r;
}

static int WINAPI rename_(const char *from, const char *to)
{
    char *a = path_to_host(from), *b = path_to_host(to);
    int r = a && b ? rename(a, b) : -1;
    if (r)
        crt_errno_set(errno);
    free(a);
    free(b);
    return r;
}

static int WINAPI _wrename(const WCHAR *from, const WCHAR *to)
{
    char *a = wide_to_host(from), *b = wide_to_host(to);
    int r = rename_(a, b);
    free(a);
    free(b);
    return r;
}

static void WINAPI perror_(const char *s)
{
    if (s && *s)
        fprintf(stderr, "%s: %s\n", s, strerror(*crt_errno()));
    else
        fprintf(stderr, "%s\n", strerror(*crt_errno()));
}

static struct wfile *WINAPI __iob_func(void) { return crt_stream(0); }
static struct wfile *WINAPI __acrt_iob_func(unsigned i) { return crt_stream(i < IOB_COUNT ? i : 0); }

static void WINAPI _lock_file(struct wfile *f) { flockfile(crt_host(f)); }
static void WINAPI _unlock_file(struct wfile *f) { funlockfile(crt_host(f)); }

static int raw_key(int timeout_ms)
{
    fflush(stdout);
    struct termios old, raw;
    bool tty = !tcgetattr(0, &old);
    if (tty) {
        raw = old;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &raw);
    }
    int c = -1;
    struct pollfd p = { 0, POLLIN, 0 };
    if (poll(&p, 1, timeout_ms) > 0) {
        unsigned char ch;
        if (timeout_ms)
            c = read(0, &ch, 1) == 1 ? (ch == '\n' ? '\r' : ch) : -1;
        else
            c = 1;
    }
    if (tty)
        tcsetattr(0, TCSANOW, &old);
    return c;
}

static int WINAPI _getch(void) { return raw_key(-1); }

static int WINAPI _getche(void)
{
    int c = raw_key(-1);
    if (c >= 0)
        putchar(c);
    return c;
}

static int WINAPI _kbhit(void) { return raw_key(0) > 0; }
static int WINAPI _putch(int c) { return putchar(c); }

BIT_TABLE(crt_stdio,
          BIT_AS("fopen", fopen_), BIT_FN(_wfopen), BIT_FN(fopen_s), BIT_FN(_wfopen_s),
          BIT_AS("freopen", freopen_), BIT_FN(_fdopen), BIT_AS("fclose", fclose_), BIT_FN(_fcloseall),
          BIT_AS("fgetc", fgetc_), BIT_AS("getc", fgetc_), BIT_AS("_fgetc_nolock", fgetc_),
          BIT_AS("getchar", getchar_), BIT_AS("fgets", fgets_),
          BIT_AS("fread", fread_), BIT_AS("fwrite", fwrite_), BIT_AS("_fwrite_nolock", fwrite_),
          BIT_AS("fputc", fputc_), BIT_AS("putc", fputc_), BIT_AS("_fputc_nolock", fputc_),
          BIT_AS("putchar", putchar_), BIT_AS("fputs", fputs_), BIT_AS("puts", puts_),
          BIT_AS("ungetc", ungetc_), BIT_AS("fflush", fflush_),
          BIT_AS("fseek", fseek_), BIT_FN(_fseeki64), BIT_AS("ftell", ftell_), BIT_FN(_ftelli64),
          BIT_AS("fgetpos", fgetpos_), BIT_AS("fsetpos", fsetpos_), BIT_AS("rewind", rewind_),
          BIT_AS("feof", feof_), BIT_AS("ferror", ferror_), BIT_AS("clearerr", clearerr_),
          BIT_AS("setvbuf", setvbuf_), BIT_AS("setbuf", setbuf_),
          BIT_FN(_fileno), BIT_AS("fileno", _fileno), BIT_AS("tmpfile", tmpfile_),
          BIT_AS("remove", remove_), BIT_FN(_wremove), BIT_AS("_unlink", remove_),
          BIT_AS("rename", rename_), BIT_FN(_wrename), BIT_AS("perror", perror_),
          BIT_FN(__iob_func), BIT_FN(__acrt_iob_func), BIT_DATA("_iob", iob),
          BIT_FN(_lock_file), BIT_FN(_unlock_file),
          BIT_FN(_getch), BIT_FN(_getche), BIT_FN(_kbhit), BIT_FN(_putch));
