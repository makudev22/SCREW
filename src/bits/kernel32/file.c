#include "k32.h"

#include <screw/process.h>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define GENERIC_ALL 0x10000000u
#define FILE_READ_DATA 0x0001u
#define FILE_WRITE_DATA 0x0002u
#define FILE_APPEND_DATA 0x0004u

#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5

#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000u

#define FILE_ATTRIBUTE_READONLY 0x01u
#define FILE_ATTRIBUTE_HIDDEN 0x02u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu

#define FILE_TYPE_DISK 1
#define FILE_TYPE_CHAR 2
#define FILE_TYPE_PIPE 3

#define MOVEFILE_REPLACE_EXISTING 1
#define DRIVE_FIXED 3
#define DRIVE_NO_ROOT_DIR 1

typedef struct {
    DWORD attributes;
    FILETIME created;
    FILETIME accessed;
    FILETIME written;
    DWORD size_high;
    DWORD size_low;
} FILE_ATTRIBUTE_DATA;

typedef struct {
    DWORD attributes;
    FILETIME created;
    FILETIME accessed;
    FILETIME written;
    DWORD volume_serial;
    DWORD size_high;
    DWORD size_low;
    DWORD links;
    DWORD index_high;
    DWORD index_low;
} BY_HANDLE_FILE_INFORMATION;

typedef struct {
    DWORD length;
    LPVOID security;
    BOOL inherit;
} SECURITY_ATTRIBUTES;

typedef struct {
    ULONG_PTR internal;
    ULONG_PTR internal_high;
    DWORD offset;
    DWORD offset_high;
    HANDLE event;
} OVERLAPPED;

DWORD attributes_of(const char *host, const struct stat *st)
{
    DWORD a = 0;
    if (S_ISDIR(st->st_mode))
        a |= FILE_ATTRIBUTE_DIRECTORY;
    if (!(st->st_mode & S_IWUSR))
        a |= FILE_ATTRIBUTE_READONLY;
    const char *base = strrchr(host, '/');
    base = base ? base + 1 : host;
    if (base[0] == '.' && base[1] && strcmp(base, ".."))
        a |= FILE_ATTRIBUTE_HIDDEN;
    return a ? a : FILE_ATTRIBUTE_NORMAL;
}

static int open_flags(DWORD access, DWORD disposition, bool *append)
{
    bool r = access & (GENERIC_READ | GENERIC_ALL | FILE_READ_DATA);
    bool w = access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA);
    *append = (access & FILE_APPEND_DATA) && !(access & (GENERIC_WRITE | FILE_WRITE_DATA));
    int f = w ? (r ? O_RDWR : O_WRONLY) : O_RDONLY;
    switch (disposition) {
    case CREATE_NEW: return f | O_CREAT | O_EXCL;
    case CREATE_ALWAYS: return f | O_CREAT | O_TRUNC;
    case OPEN_ALWAYS: return f | O_CREAT;
    case TRUNCATE_EXISTING: return f | O_TRUNC;
    default: return f;
    }
}

static HANDLE special(const char *name)
{
    if (!strcasecmp(name, "CONIN$"))
        return handle_dup(std_handle(0));
    if (!strcasecmp(name, "CONOUT$"))
        return handle_dup(std_handle(1));
    if (!strcasecmp(name, "NUL") || !strcasecmp(name, "\\\\.\\NUL"))
        return file_wrap(open("/dev/null", O_RDWR | O_CLOEXEC), "/dev/null");
    return NULL;
}

static HANDLE create_file(const char *name, DWORD access, DWORD disposition, DWORD flags)
{
    HANDLE h = special(name);
    if (h)
        return h;

    char *host = path_to_host(name);
    if (!host) {
        last_error_set(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    struct stat st;
    bool existed = !stat(host, &st);
    bool append;
    int oflags = open_flags(access, disposition, &append);
    if (existed && S_ISDIR(st.st_mode))
        oflags = O_RDONLY | O_DIRECTORY;
    if (!existed && (disposition == OPEN_EXISTING || disposition == TRUNCATE_EXISTING)) {
        free(host);
        last_error_set(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }

    int fd = open(host, oflags | O_CLOEXEC, 0644);
    if (fd < 0) {
        free(host);
        last_error_set(errno == EEXIST ? ERROR_FILE_EXISTS : path_errno_to_error(errno));
        return INVALID_HANDLE_VALUE;
    }
    h = file_wrap(fd, host);
    struct file *f = file_get(h);
    f->append = append;
    f->delete_on_close = flags & FILE_FLAG_DELETE_ON_CLOSE;
    free(host);
    last_error_set(existed && (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS)
                       ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return h;
}

static HANDLE WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD share, SECURITY_ATTRIBUTES *sa,
                                 DWORD disposition, DWORD flags, HANDLE tmpl)
{
    return create_file(name, access, disposition, flags);
}

static HANDLE WINAPI CreateFileW(LPCWSTR name, DWORD access, DWORD share, SECURITY_ATTRIBUTES *sa,
                                 DWORD disposition, DWORD flags, HANDLE tmpl)
{
    char *n = wide_to_host(name);
    HANDLE h = create_file(n, access, disposition, flags);
    free(n);
    return h;
}

static BOOL WINAPI ReadFile(HANDLE h, LPVOID buf, DWORD size, LPDWORD done, OVERLAPPED *ov)
{
    struct file *f = file_get(h);
    if (done)
        *done = 0;
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    ssize_t n;
    if (ov) {
        off_t off = ((off_t)ov->offset_high << 32) | ov->offset;
        n = pread(f->fd, buf, size, off);
    } else {
        do
            n = read(f->fd, buf, size);
        while (n < 0 && errno == EINTR);
    }
    if (n < 0)
        return fail_errno();
    if (done)
        *done = n;
    if (ov)
        ov->internal_high = n;
    return TRUE;
}

static BOOL WINAPI WriteFile(HANDLE h, LPCVOID buf, DWORD size, LPDWORD done, OVERLAPPED *ov)
{
    struct file *f = file_get(h);
    if (done)
        *done = 0;
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    const uint8_t *p = buf;
    size_t left = size;
    off_t off = ov ? ((off_t)ov->offset_high << 32) | ov->offset : 0;
    if (f->append)
        lseek(f->fd, 0, SEEK_END);
    while (left) {
        ssize_t n = ov ? pwrite(f->fd, p, left, off) : write(f->fd, p, left);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return fail_errno();
        p += n;
        left -= n;
        off += n;
    }
    if (done)
        *done = size;
    if (ov)
        ov->internal_high = size;
    return TRUE;
}

static BOOL WINAPI CloseHandle(HANDLE h)
{
    if (h == CURRENT_PROCESS || h == CURRENT_THREAD)
        return TRUE;
    return handle_close(h) ? TRUE : fail(ERROR_INVALID_HANDLE);
}

static BOOL WINAPI DuplicateHandle(HANDLE sp, HANDLE src, HANDLE tp, HANDLE *out, DWORD access,
                                   BOOL inherit, DWORD options)
{
    HANDLE d = src == CURRENT_THREAD || src == CURRENT_PROCESS ? src : handle_dup(src);
    if (!d)
        return fail(ERROR_INVALID_HANDLE);
    if (options & 1)
        handle_close(src);
    *out = d;
    return TRUE;
}

static BOOL WINAPI GetFileSizeEx(HANDLE h, LARGE_INTEGER *size)
{
    struct file *f = file_get(h);
    struct stat st;
    if (!f || fstat(f->fd, &st))
        return fail(ERROR_INVALID_HANDLE);
    size->quad = st.st_size;
    return TRUE;
}

static DWORD WINAPI GetFileSize(HANDLE h, LPDWORD high)
{
    LARGE_INTEGER s;
    if (!GetFileSizeEx(h, &s))
        return 0xFFFFFFFFu;
    if (high)
        *high = s.quad >> 32;
    return (DWORD)s.quad;
}

static BOOL WINAPI SetFilePointerEx(HANDLE h, LARGE_INTEGER dist, LARGE_INTEGER *out, DWORD method)
{
    struct file *f = file_get(h);
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    off_t r = lseek(f->fd, dist.quad, method == 0 ? SEEK_SET : method == 1 ? SEEK_CUR : SEEK_END);
    if (r < 0)
        return fail_errno();
    if (out)
        out->quad = r;
    return TRUE;
}

static DWORD WINAPI SetFilePointer(HANDLE h, LONG low, PLONG high, DWORD method)
{
    LARGE_INTEGER d, r;
    d.quad = high ? ((LONGLONG)*high << 32) | (DWORD)low : low;
    if (!SetFilePointerEx(h, d, &r, method))
        return 0xFFFFFFFFu;
    if (high)
        *high = r.quad >> 32;
    last_error_set(ERROR_SUCCESS);
    return (DWORD)r.quad;
}

static BOOL WINAPI SetEndOfFile(HANDLE h)
{
    struct file *f = file_get(h);
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    off_t pos = lseek(f->fd, 0, SEEK_CUR);
    return pos >= 0 && !ftruncate(f->fd, pos) ? TRUE : fail_errno();
}

static BOOL WINAPI FlushFileBuffers(HANDLE h)
{
    struct file *f = file_get(h);
    if (!f)
        return fail(ERROR_INVALID_HANDLE);
    fsync(f->fd);
    return TRUE;
}

static DWORD WINAPI GetFileType(HANDLE h)
{
    struct file *f = file_get(h);
    struct stat st;
    if (!f || fstat(f->fd, &st))
        return 0;
    if (S_ISCHR(st.st_mode))
        return FILE_TYPE_CHAR;
    if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode))
        return FILE_TYPE_PIPE;
    return FILE_TYPE_DISK;
}

static BOOL WINAPI GetFileInformationByHandle(HANDLE h, BY_HANDLE_FILE_INFORMATION *info)
{
    struct file *f = file_get(h);
    struct stat st;
    if (!f || fstat(f->fd, &st))
        return fail(ERROR_INVALID_HANDLE);
    info->attributes = attributes_of(f->path ? f->path : "", &st);
    info->created = filetime_from_ticks(ticks_from_timespec(&st.st_ctim));
    info->accessed = filetime_from_ticks(ticks_from_timespec(&st.st_atim));
    info->written = filetime_from_ticks(ticks_from_timespec(&st.st_mtim));
    info->volume_serial = st.st_dev;
    info->size_high = st.st_size >> 32;
    info->size_low = st.st_size;
    info->links = st.st_nlink;
    info->index_high = st.st_ino >> 32;
    info->index_low = st.st_ino;
    return TRUE;
}

static BOOL WINAPI GetFileTime(HANDLE h, FILETIME *created, FILETIME *accessed, FILETIME *written)
{
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info))
        return FALSE;
    if (created)
        *created = info.created;
    if (accessed)
        *accessed = info.accessed;
    if (written)
        *written = info.written;
    return TRUE;
}

typedef int (*path_op)(const char *host);

static BOOL on_path(const char *name, path_op op)
{
    char *host = path_to_host(name);
    if (!host)
        return fail(ERROR_PATH_NOT_FOUND);
    int r = op(host);
    int e = errno;
    free(host);
    errno = e;
    return r ? fail_errno() : TRUE;
}

static BOOL on_path_w(LPCWSTR name, path_op op)
{
    char *n = wide_to_host(name);
    BOOL r = on_path(n, op);
    free(n);
    return r;
}

static int do_mkdir(const char *p) { return mkdir(p, 0755); }

static BOOL WINAPI DeleteFileA(LPCSTR n) { return on_path(n, unlink); }
static BOOL WINAPI DeleteFileW(LPCWSTR n) { return on_path_w(n, unlink); }
static BOOL WINAPI RemoveDirectoryA(LPCSTR n) { return on_path(n, rmdir); }
static BOOL WINAPI RemoveDirectoryW(LPCWSTR n) { return on_path_w(n, rmdir); }
static BOOL WINAPI CreateDirectoryA(LPCSTR n, SECURITY_ATTRIBUTES *sa) { return on_path(n, do_mkdir); }
static BOOL WINAPI CreateDirectoryW(LPCWSTR n, SECURITY_ATTRIBUTES *sa) { return on_path_w(n, do_mkdir); }

static int do_chdir(const char *p) { return chdir(p); }
static BOOL WINAPI SetCurrentDirectoryA(LPCSTR n) { return on_path(n, do_chdir); }
static BOOL WINAPI SetCurrentDirectoryW(LPCWSTR n) { return on_path_w(n, do_chdir); }

static DWORD attributes(const char *name)
{
    char *host = path_to_host(name);
    struct stat st;
    DWORD a = INVALID_FILE_ATTRIBUTES;
    if (host && !stat(host, &st))
        a = attributes_of(host, &st);
    else
        last_error_set(host ? path_errno_to_error(errno) : ERROR_PATH_NOT_FOUND);
    free(host);
    return a;
}

static DWORD WINAPI GetFileAttributesA(LPCSTR n) { return attributes(n); }

static DWORD WINAPI GetFileAttributesW(LPCWSTR n)
{
    char *u = wide_to_host(n);
    DWORD a = attributes(u);
    free(u);
    return a;
}

static BOOL attributes_ex(const char *name, FILE_ATTRIBUTE_DATA *out)
{
    char *host = path_to_host(name);
    struct stat st;
    if (!host || stat(host, &st)) {
        free(host);
        return fail_errno();
    }
    out->attributes = attributes_of(host, &st);
    out->created = filetime_from_ticks(ticks_from_timespec(&st.st_ctim));
    out->accessed = filetime_from_ticks(ticks_from_timespec(&st.st_atim));
    out->written = filetime_from_ticks(ticks_from_timespec(&st.st_mtim));
    out->size_high = st.st_size >> 32;
    out->size_low = st.st_size;
    free(host);
    return TRUE;
}

static BOOL WINAPI GetFileAttributesExA(LPCSTR n, int level, FILE_ATTRIBUTE_DATA *out)
{
    return attributes_ex(n, out);
}

static BOOL WINAPI GetFileAttributesExW(LPCWSTR n, int level, FILE_ATTRIBUTE_DATA *out)
{
    char *u = wide_to_host(n);
    BOOL r = attributes_ex(u, out);
    free(u);
    return r;
}

static BOOL set_attributes(const char *name, DWORD attrs)
{
    char *host = path_to_host(name);
    struct stat st;
    if (!host || stat(host, &st)) {
        free(host);
        return fail_errno();
    }
    mode_t m = attrs & FILE_ATTRIBUTE_READONLY ? st.st_mode & ~0222 : st.st_mode | S_IWUSR;
    chmod(host, m);
    free(host);
    return TRUE;
}

static BOOL WINAPI SetFileAttributesA(LPCSTR n, DWORD a) { return set_attributes(n, a); }

static BOOL WINAPI SetFileAttributesW(LPCWSTR n, DWORD a)
{
    char *u = wide_to_host(n);
    BOOL r = set_attributes(u, a);
    free(u);
    return r;
}

static BOOL move(const char *from, const char *to, DWORD flags)
{
    char *a = path_to_host(from), *b = path_to_host(to);
    struct stat st;
    BOOL r;
    if (!a || !b)
        r = fail(ERROR_PATH_NOT_FOUND);
    else if (!(flags & MOVEFILE_REPLACE_EXISTING) && !stat(b, &st))
        r = fail(ERROR_ALREADY_EXISTS);
    else
        r = rename(a, b) ? fail_errno() : TRUE;
    free(a);
    free(b);
    return r;
}

static BOOL WINAPI MoveFileA(LPCSTR a, LPCSTR b) { return move(a, b, 0); }
static BOOL WINAPI MoveFileExA(LPCSTR a, LPCSTR b, DWORD f) { return move(a, b, f); }

static BOOL WINAPI MoveFileExW(LPCWSTR a, LPCWSTR b, DWORD f)
{
    char *x = wide_to_host(a), *y = wide_to_host(b);
    BOOL r = move(x, y, f);
    free(x);
    free(y);
    return r;
}

static BOOL WINAPI MoveFileW(LPCWSTR a, LPCWSTR b) { return MoveFileExW(a, b, 0); }

static BOOL copy(const char *from, const char *to, BOOL fail_if_exists)
{
    char *a = path_to_host(from), *b = path_to_host(to);
    BOOL r = FALSE;
    int in = a ? open(a, O_RDONLY | O_CLOEXEC) : -1;
    int out = -1;
    if (in < 0) {
        fail_errno();
        goto done;
    }
    out = open(b, O_WRONLY | O_CREAT | O_CLOEXEC | (fail_if_exists ? O_EXCL : O_TRUNC), 0644);
    if (out < 0) {
        last_error_set(errno == EEXIST ? ERROR_FILE_EXISTS : path_errno_to_error(errno));
        goto done;
    }
    char buf[65536];
    ssize_t n;
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, n) != n) {
            fail_errno();
            goto done;
        }
    r = n == 0 ? TRUE : fail_errno();
done:
    if (in >= 0)
        close(in);
    if (out >= 0)
        close(out);
    free(a);
    free(b);
    return r;
}

static BOOL WINAPI CopyFileA(LPCSTR a, LPCSTR b, BOOL f) { return copy(a, b, f); }

static BOOL WINAPI CopyFileW(LPCWSTR a, LPCWSTR b, BOOL f)
{
    char *x = wide_to_host(a), *y = wide_to_host(b);
    BOOL r = copy(x, y, f);
    free(x);
    free(y);
    return r;
}

static char *current_dir(void)
{
    char buf[PATH_MAX];
    return path_to_windows(getcwd(buf, sizeof buf) ? buf : "/");
}

static DWORD WINAPI GetCurrentDirectoryA(DWORD size, LPSTR out)
{
    char *d = current_dir();
    DWORD r = copy_out_a(d, out, size);
    free(d);
    return r;
}

static DWORD WINAPI GetCurrentDirectoryW(DWORD size, LPWSTR out)
{
    char *d = current_dir();
    DWORD r = copy_out_w(d, out, size);
    free(d);
    return r;
}

static char *full_path(const char *name)
{
    char *host = path_to_host(name);
    if (!host)
        return strdup(name);
    char buf[PATH_MAX];
    if (host[0] != '/') {
        char cwd[PATH_MAX];
        snprintf(buf, sizeof buf, "%s/%s", getcwd(cwd, sizeof cwd) ? cwd : "", host);
    } else {
        snprintf(buf, sizeof buf, "%s", host);
    }
    free(host);

    char norm[PATH_MAX];
    size_t n = 0;
    for (char *save, *tok = strtok_r(buf, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (!strcmp(tok, "."))
            continue;
        if (!strcmp(tok, "..")) {
            while (n && norm[--n] != '/')
                ;
            continue;
        }
        n += snprintf(norm + n, sizeof norm - n, "/%s", tok);
    }
    if (!n)
        norm[n++] = '/';
    norm[n] = 0;
    return path_to_windows(norm);
}

static void set_file_part_a(LPSTR buf, LPSTR *part)
{
    if (!part)
        return;
    char *s = strrchr(buf, '\\');
    *part = s && s[1] ? s + 1 : NULL;
}

static DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD size, LPSTR out, LPSTR *part)
{
    char *full = full_path(name);
    DWORD r = copy_out_a(full, out, size);
    free(full);
    if (r < size)
        set_file_part_a(out, part);
    return r;
}

static DWORD WINAPI GetFullPathNameW(LPCWSTR name, DWORD size, LPWSTR out, LPWSTR *part)
{
    char *n = wide_to_host(name);
    char *full = full_path(n);
    DWORD r = copy_out_w(full, out, size);
    free(n);
    free(full);
    if (part && r < size) {
        LPWSTR s = NULL;
        for (LPWSTR p = out; *p; p++)
            if (*p == '\\')
                s = p;
        *part = s && s[1] ? s + 1 : NULL;
    }
    return r;
}

static DWORD WINAPI GetLongPathNameA(LPCSTR in, LPSTR out, DWORD size) { return copy_out_a(in, out, size); }
static DWORD WINAPI GetShortPathNameA(LPCSTR in, LPSTR out, DWORD size) { return copy_out_a(in, out, size); }

static DWORD WINAPI GetLongPathNameW(LPCWSTR in, LPWSTR out, DWORD size)
{
    size_t n = wstr_len(in);
    if (!out || size <= n)
        return n + 1;
    memmove(out, in, (n + 1) * sizeof(WCHAR));
    return n;
}

static DWORD WINAPI GetShortPathNameW(LPCWSTR in, LPWSTR out, DWORD size) { return GetLongPathNameW(in, out, size); }

static char *temp_dir(void)
{
    const char *t = env_get("TEMP");
    char *out = malloc(PATH_MAX);
    snprintf(out, PATH_MAX, "%s\\", t ? t : "C:\\windows\\temp");
    return out;
}

static DWORD WINAPI GetTempPathA(DWORD size, LPSTR out)
{
    char *t = temp_dir();
    DWORD r = copy_out_a(t, out, size);
    free(t);
    return r;
}

static DWORD WINAPI GetTempPathW(DWORD size, LPWSTR out)
{
    char *t = temp_dir();
    DWORD r = copy_out_w(t, out, size);
    free(t);
    return r;
}

static UINT temp_file(const char *dir, const char *prefix, UINT unique, char *out)
{
    UINT n = unique ? unique : (UINT)(getpid() ^ (uintptr_t)out) & 0xFFFF;
    for (int tries = 0; tries < 65536; tries++, n = (n + 1) & 0xFFFF) {
        if (!n)
            continue;
        snprintf(out, PATH_MAX, "%s%s%.3s%X.tmp", dir,
                 dir[strlen(dir) - 1] == '\\' ? "" : "\\", prefix, n);
        if (unique)
            return n;
        HANDLE h = create_file(out, GENERIC_WRITE, CREATE_NEW, 0);
        if (h != INVALID_HANDLE_VALUE) {
            handle_close(h);
            return n;
        }
    }
    return 0;
}

static UINT WINAPI GetTempFileNameA(LPCSTR dir, LPCSTR prefix, UINT unique, LPSTR out)
{
    char buf[PATH_MAX];
    UINT r = temp_file(dir, prefix, unique, buf);
    if (r)
        strcpy(out, buf);
    return r;
}

static UINT WINAPI GetTempFileNameW(LPCWSTR dir, LPCWSTR prefix, UINT unique, LPWSTR out)
{
    char *d = wide_to_host(dir), *p = wide_to_host(prefix), buf[PATH_MAX];
    UINT r = temp_file(d, p, unique, buf);
    if (r)
        utf8_to_utf16(buf, -1, out, PATH_MAX);
    free(d);
    free(p);
    return r;
}

static BOOL disk_space(const char *root, ULONGLONG *avail, ULONGLONG *total, ULONGLONG *free_)
{
    char *host = path_to_host(root ? root : "C:\\");
    struct statvfs s;
    int r = host ? statvfs(host, &s) : -1;
    free(host);
    if (r)
        return fail_errno();
    if (avail)
        *avail = (ULONGLONG)s.f_bavail * s.f_frsize;
    if (total)
        *total = (ULONGLONG)s.f_blocks * s.f_frsize;
    if (free_)
        *free_ = (ULONGLONG)s.f_bfree * s.f_frsize;
    return TRUE;
}

static BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR root, ULONGLONG *a, ULONGLONG *t, ULONGLONG *f)
{
    return disk_space(root, a, t, f);
}

static BOOL WINAPI GetDiskFreeSpaceExW(LPCWSTR root, ULONGLONG *a, ULONGLONG *t, ULONGLONG *f)
{
    char *r = wide_to_host(root);
    BOOL ok = disk_space(r, a, t, f);
    free(r);
    return ok;
}

static UINT drive_type(const char *root)
{
    char *host = path_to_host(root ? root : "C:\\");
    struct stat st;
    UINT t = host && !stat(host, &st) ? DRIVE_FIXED : DRIVE_NO_ROOT_DIR;
    free(host);
    return t;
}

static UINT WINAPI GetDriveTypeA(LPCSTR root) { return drive_type(root); }

static UINT WINAPI GetDriveTypeW(LPCWSTR root)
{
    char *r = wide_to_host(root);
    UINT t = drive_type(r);
    free(r);
    return t;
}

static DWORD WINAPI GetLogicalDrives(void)
{
    DWORD mask = (1u << 2) | (1u << 25);
    char var[] = "SCREW_DRIVE_X";
    for (int i = 0; i < 26; i++) {
        var[sizeof var - 2] = 'A' + i;
        if (getenv(var))
            mask |= 1u << i;
    }
    return mask;
}

BIT_TABLE(k32_file,
          BIT_FN(CreateFileA), BIT_FN(CreateFileW), BIT_FN(ReadFile), BIT_FN(WriteFile),
          BIT_FN(CloseHandle), BIT_FN(DuplicateHandle),
          BIT_FN(GetFileSize), BIT_FN(GetFileSizeEx), BIT_FN(SetFilePointer), BIT_FN(SetFilePointerEx),
          BIT_FN(SetEndOfFile), BIT_FN(FlushFileBuffers), BIT_FN(GetFileType),
          BIT_FN(GetFileInformationByHandle), BIT_FN(GetFileTime),
          BIT_FN(DeleteFileA), BIT_FN(DeleteFileW), BIT_FN(RemoveDirectoryA), BIT_FN(RemoveDirectoryW),
          BIT_FN(CreateDirectoryA), BIT_FN(CreateDirectoryW),
          BIT_FN(SetCurrentDirectoryA), BIT_FN(SetCurrentDirectoryW),
          BIT_FN(GetCurrentDirectoryA), BIT_FN(GetCurrentDirectoryW),
          BIT_FN(GetFileAttributesA), BIT_FN(GetFileAttributesW),
          BIT_FN(GetFileAttributesExA), BIT_FN(GetFileAttributesExW),
          BIT_FN(SetFileAttributesA), BIT_FN(SetFileAttributesW),
          BIT_FN(MoveFileA), BIT_FN(MoveFileW), BIT_FN(MoveFileExA), BIT_FN(MoveFileExW),
          BIT_FN(CopyFileA), BIT_FN(CopyFileW),
          BIT_FN(GetFullPathNameA), BIT_FN(GetFullPathNameW),
          BIT_FN(GetLongPathNameA), BIT_FN(GetLongPathNameW),
          BIT_FN(GetShortPathNameA), BIT_FN(GetShortPathNameW),
          BIT_FN(GetTempPathA), BIT_FN(GetTempPathW),
          BIT_FN(GetTempFileNameA), BIT_FN(GetTempFileNameW),
          BIT_FN(GetDiskFreeSpaceExA), BIT_FN(GetDiskFreeSpaceExW),
          BIT_FN(GetDriveTypeA), BIT_FN(GetDriveTypeW), BIT_FN(GetLogicalDrives));
