#include <screw/path.h>
#include <screw/unicode.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char c_root[PATH_MAX];

static void make_dirs(const char *path)
{
    char buf[PATH_MAX];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(buf, 0755);
            *p = '/';
        }
    mkdir(buf, 0755);
}

void path_init(void)
{
    const char *env = getenv("SCREW_C");
    if (env && *env)
        snprintf(c_root, sizeof c_root, "%s", env);
    else
        snprintf(c_root, sizeof c_root, "%s/.screw/c", getenv("HOME") ? getenv("HOME") : "/tmp");
    make_dirs(c_root);
}

const char *path_c_root(void) { return c_root; }

static const char *drive_root(char letter)
{
    letter = toupper((unsigned char)letter);
    if (letter == 'C')
        return c_root;
    if (letter == 'Z')
        return "";
    char var[] = "SCREW_DRIVE_X";
    var[sizeof var - 2] = letter;
    return getenv(var);
}

static bool exists(const char *p)
{
    struct stat st;
    return lstat(p, &st) == 0;
}

static bool match_component(const char *dir, char *name)
{
    DIR *d = opendir(*dir ? dir : "/");
    if (!d)
        return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d)))
        if (!strcasecmp(e->d_name, name)) {
            strcpy(name, e->d_name);
            found = true;
        }
    closedir(d);
    return found;
}

static void fold_case(char *path)
{
    if (exists(path))
        return;
    char out[PATH_MAX] = "";
    size_t len = 0;
    char *save, *copy = strdup(path);
    bool absolute = path[0] == '/';
    bool missing = false;
    for (char *tok = strtok_r(copy, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        char name[NAME_MAX + 1];
        snprintf(name, sizeof name, "%s", tok);
        char dir[PATH_MAX];
        snprintf(dir, sizeof dir, "%s", len ? out : absolute ? "" : ".");
        if (!missing) {
            char probe[PATH_MAX];
            snprintf(probe, sizeof probe, "%s/%s", dir, name);
            if (!exists(probe) && !match_component(dir, name))
                missing = true;
        }
        len += snprintf(out + len, sizeof out - len, "%s%s", len || absolute ? "/" : "", name);
    }
    free(copy);
    if (len < PATH_MAX)
        strcpy(path, out);
}

char *path_to_host(const char *win)
{
    if (!win)
        return NULL;
    if (!strncmp(win, "\\\\?\\", 4) || !strncmp(win, "\\??\\", 4))
        win += 4;

    char buf[PATH_MAX];
    const char *rest = win;
    const char *root = NULL;
    if (isalpha((unsigned char)win[0]) && win[1] == ':') {
        root = drive_root(win[0]);
        if (!root)
            return NULL;
        rest = win + 2;
    } else if (win[0] == '\\' || win[0] == '/') {
        if (win[0] == '/' && exists(win))
            return strdup(win);
        root = c_root;
    }

    int n = snprintf(buf, sizeof buf, "%s%s", root ? root : "", root && *rest != '\\' && *rest != '/' ? "/" : "");
    for (const char *p = rest; *p && n < PATH_MAX - 1; p++)
        buf[n++] = *p == '\\' ? '/' : *p;
    buf[n] = 0;
    if (!buf[0])
        strcpy(buf, "/");
    fold_case(buf);
    return strdup(buf);
}

char *path_to_host_w(const WCHAR *win)
{
    char *u = wide_to_host(win);
    char *h = path_to_host(u);
    free(u);
    return h;
}

char *path_to_windows(const char *host)
{
    char abs[PATH_MAX];
    if (!realpath(host, abs))
        snprintf(abs, sizeof abs, "%s", host);

    size_t cl = strlen(c_root);
    const char *rest;
    char drive;
    if (!strncmp(abs, c_root, cl) && (abs[cl] == '/' || !abs[cl])) {
        drive = 'C';
        rest = abs + cl;
    } else {
        drive = 'Z';
        rest = abs;
    }
    char *out = malloc(strlen(rest) + 4);
    out[0] = drive;
    out[1] = ':';
    size_t n = 2;
    for (const char *p = rest; *p; p++)
        out[n++] = *p == '/' ? '\\' : *p;
    if (n == 2)
        out[n++] = '\\';
    out[n] = 0;
    return out;
}

DWORD path_errno_to_error(int err)
{
    switch (err) {
    case 0: return ERROR_SUCCESS;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    case ENOTEMPTY: return ERROR_DIR_NOT_EMPTY;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    case EBADF: return ERROR_INVALID_HANDLE;
    case EINVAL: return ERROR_INVALID_PARAMETER;
    case EPIPE: return ERROR_BROKEN_PIPE;
    case ENOSYS: return ERROR_NOT_SUPPORTED;
    default: return ERROR_INVALID_FUNCTION;
    }
}
