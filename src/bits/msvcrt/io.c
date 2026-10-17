#include "crt.h"

#include <screw/handle.h>
#include <screw/path.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#define W_O_WRONLY 0x0001
#define W_O_RDWR 0x0002
#define W_O_APPEND 0x0008
#define W_O_CREAT 0x0100
#define W_O_TRUNC 0x0200
#define W_O_EXCL 0x0400
#define W_O_TEXT 0x4000
#define W_O_BINARY 0x8000

#define W_S_IFDIR 0x4000
#define W_S_IFCHR 0x2000
#define W_S_IFIFO 0x1000
#define W_S_IFREG 0x8000

#define W_A_RDONLY 0x01
#define W_A_HIDDEN 0x02
#define W_A_SUBDIR 0x10
#define W_A_ARCH 0x20

struct wstat64 {
    uint32_t dev;
    uint16_t ino;
    uint16_t mode;
    int16_t nlink;
    int16_t uid;
    int16_t gid;
    uint32_t rdev;
    int64_t size;
    int64_t atime;
    int64_t mtime;
    int64_t ctime;
};

struct wstat64i32 {
    uint32_t dev;
    uint16_t ino;
    uint16_t mode;
    int16_t nlink;
    int16_t uid;
    int16_t gid;
    uint32_t rdev;
    int32_t size;
    int64_t atime;
    int64_t mtime;
    int64_t ctime;
};

struct wfind64 {
    unsigned attrib;
    int64_t create;
    int64_t access;
    int64_t write;
    int64_t size;
    char name[260];
};

struct wfind64i32 {
    unsigned attrib;
    int64_t create;
    int64_t access;
    int64_t write;
    uint32_t size;
    char name[260];
};

struct wfind64_w {
    unsigned attrib;
    int64_t create;
    int64_t access;
    int64_t write;
    int64_t size;
    WCHAR name[260];
};

_Static_assert(sizeof(struct wstat64) == 56, "_stat64 layout");
_Static_assert(sizeof(struct wstat64i32) == 48, "_stat64i32 layout");

static int err(void)
{
    crt_errno_set(errno);
    return -1;
}

static int host_flags(int f)
{
    int h = f & W_O_RDWR ? O_RDWR : f & W_O_WRONLY ? O_WRONLY : O_RDONLY;
    if (f & W_O_APPEND)
        h |= O_APPEND;
    if (f & W_O_CREAT)
        h |= O_CREAT;
    if (f & W_O_TRUNC)
        h |= O_TRUNC;
    if (f & W_O_EXCL)
        h |= O_EXCL;
    return h | O_CLOEXEC;
}

static int open_path(const char *name, int flags, int mode)
{
    char *host = path_to_host(name);
    if (!host) {
        crt_errno_set(ENOENT);
        return -1;
    }
    int fd = open(host, host_flags(flags), mode & 0x80 ? 0644 : 0444);
    int e = errno;
    free(host);
    errno = e;
    return fd < 0 ? err() : fd;
}

static int WINAPI _open(const char *name, int flags, int mode) { return open_path(name, flags, mode); }

static int WINAPI _wopen(const WCHAR *name, int flags, int mode)
{
    char *n = wide_to_host(name);
    int r = open_path(n, flags, mode);
    free(n);
    return r;
}

static int WINAPI _sopen(const char *name, int flags, int share, int mode) { return open_path(name, flags, mode); }

static int WINAPI _sopen_s(int *fd, const char *name, int flags, int share, int mode)
{
    *fd = open_path(name, flags, mode);
    return *fd < 0 ? *crt_errno() : 0;
}

static int WINAPI _wsopen_s(int *fd, const WCHAR *name, int flags, int share, int mode)
{
    *fd = _wopen(name, flags, mode);
    return *fd < 0 ? *crt_errno() : 0;
}

static int WINAPI _close(int fd) { return close(fd) ? err() : 0; }

static int WINAPI _read(int fd, void *buf, unsigned n)
{
    ssize_t r = read(fd, buf, n);
    return r < 0 ? err() : (int)r;
}

static int WINAPI _write(int fd, const void *buf, unsigned n)
{
    ssize_t r = write(fd, buf, n);
    return r < 0 ? err() : (int)r;
}

static LONG WINAPI _lseek(int fd, LONG off, int whence)
{
    off_t r = lseek(fd, off, whence);
    return r < 0 ? err() : (LONG)r;
}

static int64_t WINAPI _lseeki64(int fd, int64_t off, int whence)
{
    off_t r = lseek(fd, off, whence);
    return r < 0 ? err() : r;
}

static LONG WINAPI _tell(int fd) { return _lseek(fd, 0, SEEK_CUR); }
static int64_t WINAPI _telli64(int fd) { return _lseeki64(fd, 0, SEEK_CUR); }

static int64_t WINAPI _filelengthi64(int fd)
{
    struct stat st;
    return fstat(fd, &st) ? err() : st.st_size;
}

static LONG WINAPI _filelength(int fd) { return (LONG)_filelengthi64(fd); }

static int WINAPI _eof(int fd)
{
    off_t pos = lseek(fd, 0, SEEK_CUR);
    struct stat st;
    if (pos < 0 || fstat(fd, &st))
        return err();
    return pos >= st.st_size;
}

static int WINAPI _chsize_s(int fd, int64_t size) { return ftruncate(fd, size) ? (err(), errno) : 0; }
static int WINAPI _chsize(int fd, LONG size) { return ftruncate(fd, size) ? err() : 0; }
static int WINAPI _commit(int fd) { return fsync(fd) ? err() : 0; }
static int WINAPI _dup(int fd) { int r = dup(fd); return r < 0 ? err() : r; }
static int WINAPI _dup2(int a, int b) { return dup2(a, b) < 0 ? err() : 0; }
static int WINAPI _isatty(int fd) { return isatty(fd); }
static int WINAPI _setmode(int fd, int mode) { return W_O_TEXT; }

static int on_path(const char *name, int (*op)(const char *, int), int arg)
{
    char *host = path_to_host(name);
    int r = host ? op(host, arg) : -1;
    int e = host ? errno : ENOENT;
    free(host);
    if (r) {
        crt_errno_set(e);
        return -1;
    }
    return 0;
}

static int on_path_w(const WCHAR *name, int (*op)(const char *, int), int arg)
{
    char *n = wide_to_host(name);
    int r = on_path(n, op, arg);
    free(n);
    return r;
}

static int do_access(const char *p, int mode) { return access(p, mode & 6 ? (mode & 2 ? W_OK : 0) | (mode & 4 ? R_OK : 0) : F_OK); }
static int do_mkdir(const char *p, int unused) { return mkdir(p, 0755); }
static int do_rmdir(const char *p, int unused) { return rmdir(p); }
static int do_chdir(const char *p, int unused) { return chdir(p); }
static int do_chmod(const char *p, int mode) { return chmod(p, mode & 0x80 ? 0644 : 0444); }

static int WINAPI _access(const char *n, int m) { return on_path(n, do_access, m); }
static int WINAPI _waccess(const WCHAR *n, int m) { return on_path_w(n, do_access, m); }
static int WINAPI _access_s(const char *n, int m) { return _access(n, m) ? *crt_errno() : 0; }
static int WINAPI _waccess_s(const WCHAR *n, int m) { return _waccess(n, m) ? *crt_errno() : 0; }
static int WINAPI _mkdir(const char *n) { return on_path(n, do_mkdir, 0); }
static int WINAPI _wmkdir(const WCHAR *n) { return on_path_w(n, do_mkdir, 0); }
static int WINAPI _rmdir(const char *n) { return on_path(n, do_rmdir, 0); }
static int WINAPI _wrmdir(const WCHAR *n) { return on_path_w(n, do_rmdir, 0); }
static int WINAPI _chdir(const char *n) { return on_path(n, do_chdir, 0); }
static int WINAPI _wchdir(const WCHAR *n) { return on_path_w(n, do_chdir, 0); }
static int WINAPI _chmod(const char *n, int m) { return on_path(n, do_chmod, m); }
static int WINAPI _wchmod(const WCHAR *n, int m) { return on_path_w(n, do_chmod, m); }

static int do_unlink(const char *p, int unused) { return unlink(p); }
static int WINAPI _wunlink(const WCHAR *n) { return on_path_w(n, do_unlink, 0); }

static char *WINAPI _getcwd(char *buf, int size)
{
    char host[PATH_MAX];
    if (!getcwd(host, sizeof host)) {
        err();
        return NULL;
    }
    char *win = path_to_windows(host);
    if (!buf)
        return win;
    if ((int)strlen(win) >= size) {
        free(win);
        crt_errno_set(ERANGE);
        return NULL;
    }
    strcpy(buf, win);
    free(win);
    return buf;
}

static WCHAR *WINAPI _wgetcwd(WCHAR *buf, int size)
{
    char *cwd = _getcwd(NULL, 0);
    if (!cwd)
        return NULL;
    WCHAR *w = host_to_wide(cwd);
    free(cwd);
    if (!buf)
        return w;
    if ((int)wstr_len(w) >= size) {
        free(w);
        crt_errno_set(ERANGE);
        return NULL;
    }
    memcpy(buf, w, (wstr_len(w) + 1) * 2);
    free(w);
    return buf;
}

static char *WINAPI _getdcwd(int drive, char *buf, int size) { return _getcwd(buf, size); }
static int WINAPI _getdrive(void) { return 3; }
static int WINAPI _umask(int mode) { return 0; }

static uint16_t win_mode(mode_t m)
{
    uint16_t w = S_ISDIR(m) ? W_S_IFDIR : S_ISCHR(m) ? W_S_IFCHR : S_ISFIFO(m) ? W_S_IFIFO : W_S_IFREG;
    w |= 0x100;
    if (m & S_IWUSR)
        w |= 0x80;
    if (S_ISDIR(m) || (m & S_IXUSR))
        w |= 0x40;
    return w | ((w & 0x1C0) >> 3) | ((w & 0x1C0) >> 6);
}

static void fill64(const struct stat *st, struct wstat64 *w)
{
    *w = (struct wstat64){ st->st_dev, st->st_ino, win_mode(st->st_mode), st->st_nlink, 0, 0,
                           st->st_rdev, st->st_size, st->st_atime, st->st_mtime, st->st_ctime };
}

static void fill64i32(const struct stat *st, struct wstat64i32 *w)
{
    *w = (struct wstat64i32){ st->st_dev, st->st_ino, win_mode(st->st_mode), st->st_nlink, 0, 0,
                              st->st_rdev, (int32_t)st->st_size, st->st_atime, st->st_mtime, st->st_ctime };
}

static int host_stat(const char *name, struct stat *st)
{
    char *host = path_to_host(name);
    int r = host ? stat(host, st) : -1;
    int e = host ? errno : ENOENT;
    free(host);
    if (r)
        crt_errno_set(e);
    return r ? -1 : 0;
}

static int WINAPI _stat64(const char *n, struct wstat64 *w)
{
    struct stat st;
    if (host_stat(n, &st))
        return -1;
    fill64(&st, w);
    return 0;
}

static int WINAPI _stat64i32(const char *n, struct wstat64i32 *w)
{
    struct stat st;
    if (host_stat(n, &st))
        return -1;
    fill64i32(&st, w);
    return 0;
}

static int WINAPI _wstat64(const WCHAR *n, struct wstat64 *w)
{
    char *u = wide_to_host(n);
    int r = _stat64(u, w);
    free(u);
    return r;
}

static int WINAPI _wstat64i32(const WCHAR *n, struct wstat64i32 *w)
{
    char *u = wide_to_host(n);
    int r = _stat64i32(u, w);
    free(u);
    return r;
}

static int WINAPI _fstat64(int fd, struct wstat64 *w)
{
    struct stat st;
    if (fstat(fd, &st))
        return err();
    fill64(&st, w);
    return 0;
}

static int WINAPI _fstat64i32(int fd, struct wstat64i32 *w)
{
    struct stat st;
    if (fstat(fd, &st))
        return err();
    fill64i32(&st, w);
    return 0;
}

struct finder {
    DIR *dir;
    char *path;
    char *pattern;
};

struct found {
    struct stat st;
    char name[NAME_MAX + 1];
};

static bool find_next(struct finder *f, struct found *out)
{
    struct dirent *d;
    while ((d = readdir(f->dir))) {
        if (fnmatch(f->pattern, d->d_name, FNM_CASEFOLD))
            continue;
        char full[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", f->path, d->d_name);
        if (stat(full, &out->st))
            continue;
        snprintf(out->name, sizeof out->name, "%s", d->d_name);
        return true;
    }
    return false;
}

static unsigned attrib(const struct found *e)
{
    unsigned a = S_ISDIR(e->st.st_mode) ? W_A_SUBDIR : W_A_ARCH;
    if (!(e->st.st_mode & S_IWUSR))
        a |= W_A_RDONLY;
    if (e->name[0] == '.' && strcmp(e->name, ".") && strcmp(e->name, ".."))
        a |= W_A_HIDDEN;
    return a;
}

static intptr_t find_first(const char *spec, struct found *out)
{
    char *host = path_to_host(spec);
    if (!host) {
        crt_errno_set(ENOENT);
        return -1;
    }
    struct finder *f = calloc(1, sizeof *f);
    char *slash = strrchr(host, '/');
    if (!slash) {
        f->path = strdup(".");
        f->pattern = strdup(host);
    } else {
        f->pattern = strdup(slash + 1);
        *slash = 0;
        f->path = strdup(slash == host ? "/" : host);
    }
    free(host);
    if (!strcmp(f->pattern, "*.*")) {
        free(f->pattern);
        f->pattern = strdup("*");
    }
    f->dir = opendir(f->path);
    if (!f->dir || !find_next(f, out)) {
        if (f->dir)
            closedir(f->dir);
        free(f->path);
        free(f->pattern);
        free(f);
        crt_errno_set(ENOENT);
        return -1;
    }
    return (intptr_t)f;
}

static int find_more(intptr_t h, struct found *out)
{
    if (h == -1 || !h || !find_next((struct finder *)h, out)) {
        crt_errno_set(ENOENT);
        return -1;
    }
    return 0;
}

static void put64(const struct found *e, struct wfind64 *w)
{
    w->attrib = attrib(e);
    w->create = e->st.st_ctime;
    w->access = e->st.st_atime;
    w->write = e->st.st_mtime;
    w->size = S_ISDIR(e->st.st_mode) ? 0 : e->st.st_size;
    snprintf(w->name, sizeof w->name, "%s", e->name);
}

static void put64i32(const struct found *e, struct wfind64i32 *w)
{
    w->attrib = attrib(e);
    w->create = e->st.st_ctime;
    w->access = e->st.st_atime;
    w->write = e->st.st_mtime;
    w->size = S_ISDIR(e->st.st_mode) ? 0 : (uint32_t)e->st.st_size;
    snprintf(w->name, sizeof w->name, "%s", e->name);
}

static void put64w(const struct found *e, struct wfind64_w *w)
{
    w->attrib = attrib(e);
    w->create = e->st.st_ctime;
    w->access = e->st.st_atime;
    w->write = e->st.st_mtime;
    w->size = S_ISDIR(e->st.st_mode) ? 0 : e->st.st_size;
    utf8_to_utf16(e->name, -1, w->name, 259);
}

static intptr_t WINAPI _findfirst64(const char *spec, struct wfind64 *w)
{
    struct found e;
    intptr_t h = find_first(spec, &e);
    if (h != -1)
        put64(&e, w);
    return h;
}

static int WINAPI _findnext64(intptr_t h, struct wfind64 *w)
{
    struct found e;
    if (find_more(h, &e))
        return -1;
    put64(&e, w);
    return 0;
}

static intptr_t WINAPI _findfirst64i32(const char *spec, struct wfind64i32 *w)
{
    struct found e;
    intptr_t h = find_first(spec, &e);
    if (h != -1)
        put64i32(&e, w);
    return h;
}

static int WINAPI _findnext64i32(intptr_t h, struct wfind64i32 *w)
{
    struct found e;
    if (find_more(h, &e))
        return -1;
    put64i32(&e, w);
    return 0;
}

static intptr_t WINAPI _wfindfirst64(const WCHAR *spec, struct wfind64_w *w)
{
    char *s = wide_to_host(spec);
    struct found e;
    intptr_t h = find_first(s, &e);
    free(s);
    if (h != -1)
        put64w(&e, w);
    return h;
}

static int WINAPI _wfindnext64(intptr_t h, struct wfind64_w *w)
{
    struct found e;
    if (find_more(h, &e))
        return -1;
    put64w(&e, w);
    return 0;
}

static int WINAPI _findclose(intptr_t h)
{
    if (h == -1 || !h)
        return -1;
    struct finder *f = (struct finder *)h;
    closedir(f->dir);
    free(f->path);
    free(f->pattern);
    free(f);
    return 0;
}

static intptr_t WINAPI _get_osfhandle(int fd)
{
    if (fd >= 0 && fd <= 2)
        return (intptr_t)std_handle(fd);
    int copy = dup(fd);
    return copy < 0 ? -1 : (intptr_t)file_wrap(copy, NULL);
}

static int WINAPI _open_osfhandle(intptr_t h, int flags)
{
    struct file *f = file_get((HANDLE)h);
    if (!f) {
        crt_errno_set(EBADF);
        return -1;
    }
    return f->fd <= 2 ? f->fd : dup(f->fd);
}

static int WINAPI _pipe(int *fds, unsigned size, int mode) { return pipe(fds) ? err() : 0; }

BIT_TABLE(crt_io,
          BIT_FN(_open), BIT_FN(_wopen), BIT_FN(_sopen), BIT_FN(_sopen_s), BIT_FN(_wsopen_s),
          BIT_FN(_close), BIT_FN(_read), BIT_FN(_write), BIT_FN(_lseek), BIT_FN(_lseeki64),
          BIT_FN(_tell), BIT_FN(_telli64), BIT_FN(_filelength), BIT_FN(_filelengthi64), BIT_FN(_eof),
          BIT_FN(_chsize), BIT_FN(_chsize_s), BIT_FN(_commit), BIT_FN(_dup), BIT_FN(_dup2),
          BIT_FN(_isatty), BIT_FN(_setmode),
          BIT_FN(_access), BIT_FN(_waccess), BIT_FN(_access_s), BIT_FN(_waccess_s),
          BIT_FN(_mkdir), BIT_FN(_wmkdir), BIT_FN(_rmdir), BIT_FN(_wrmdir),
          BIT_FN(_chdir), BIT_FN(_wchdir), BIT_FN(_chmod), BIT_FN(_wchmod), BIT_FN(_wunlink),
          BIT_FN(_getcwd), BIT_FN(_wgetcwd), BIT_FN(_getdcwd), BIT_FN(_getdrive), BIT_FN(_umask),
          BIT_FN(_stat64), BIT_FN(_stat64i32), BIT_AS("_stat", _stat64i32), BIT_AS("stat", _stat64i32),
          BIT_AS("_stati64", _stat64), BIT_FN(_wstat64), BIT_FN(_wstat64i32), BIT_AS("_wstat", _wstat64i32),
          BIT_FN(_fstat64), BIT_FN(_fstat64i32), BIT_AS("_fstat", _fstat64i32), BIT_AS("fstat", _fstat64i32),
          BIT_AS("_fstati64", _fstat64),
          BIT_FN(_findfirst64), BIT_FN(_findnext64), BIT_FN(_findfirst64i32), BIT_FN(_findnext64i32),
          BIT_AS("_findfirst", _findfirst64i32), BIT_AS("_findnext", _findnext64i32),
          BIT_FN(_wfindfirst64), BIT_FN(_wfindnext64), BIT_FN(_findclose),
          BIT_FN(_get_osfhandle), BIT_FN(_open_osfhandle), BIT_FN(_pipe));
