#include <screw/path.h>
#include <screw/process.h>
#include <screw/unicode.h>

#include <limits.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

static int argc_;
static char **argv_;
static WCHAR **wargv;
static char *cmdline;
static WCHAR *wcmdline;
static char exe_host[PATH_MAX];
static char *exe_windows;

static char **env;
static size_t env_count, env_cap;
static pthread_mutex_t env_lock = PTHREAD_MUTEX_INITIALIZER;

static void quote(char **buf, size_t *len, size_t *cap, const char *arg)
{
    size_t need = *len + strlen(arg) * 2 + 4;
    if (need > *cap) {
        *cap = need * 2;
        *buf = realloc(*buf, *cap);
    }
    char *o = *buf + *len;
    if (*len)
        *o++ = ' ';
    bool q = !*arg || strpbrk(arg, " \t\"");
    if (q)
        *o++ = '"';
    for (const char *p = arg;; p++) {
        size_t slashes = 0;
        while (*p == '\\')
            p++, slashes++;
        if (!*p) {
            for (size_t i = 0; i < slashes * (q ? 2 : 1); i++)
                *o++ = '\\';
            break;
        }
        if (*p == '"')
            slashes = slashes * 2 + 1;
        for (size_t i = 0; i < slashes; i++)
            *o++ = '\\';
        *o++ = *p;
    }
    if (q)
        *o++ = '"';
    *o = 0;
    *len = o - *buf;
}

static ssize_t env_find(const char *name, size_t n)
{
    for (size_t i = 0; i < env_count; i++)
        if (!strncasecmp(env[i], name, n) && env[i][n] == '=')
            return i;
    return -1;
}

static void env_put(const char *name, const char *value)
{
    size_t n = strlen(name);
    ssize_t i = env_find(name, n);
    if (!value) {
        if (i >= 0) {
            free(env[i]);
            env[i] = env[--env_count];
            env[env_count] = NULL;
        }
        return;
    }
    char *entry = malloc(n + strlen(value) + 2);
    sprintf(entry, "%s=%s", name, value);
    if (i >= 0) {
        free(env[i]);
        env[i] = entry;
        return;
    }
    if (env_count + 2 > env_cap) {
        env_cap = env_cap ? env_cap * 2 : 64;
        env = realloc(env, env_cap * sizeof *env);
    }
    env[env_count++] = entry;
    env[env_count] = NULL;
}

static void ensure_dir(const char *win)
{
    char *host = path_to_host(win);
    if (host) {
        char buf[PATH_MAX];
        snprintf(buf, sizeof buf, "%s", host);
        for (char *p = buf + 1; *p; p++)
            if (*p == '/') {
                *p = 0;
                mkdir(buf, 0755);
                *p = '/';
            }
        mkdir(buf, 0755);
        free(host);
    }
}

static void env_defaults(void)
{
    for (char **e = environ; *e; e++) {
        char *eq = strchr(*e, '=');
        if (!eq || eq == *e)
            continue;
        char name[256];
        size_t n = eq - *e < 255 ? (size_t)(eq - *e) : 255;
        memcpy(name, *e, n);
        name[n] = 0;
        env_put(name, eq + 1);
    }

    struct passwd *pw = getpwuid(getuid());
    const char *user = pw ? pw->pw_name : "user";
    char host[256] = "SCREW";
    gethostname(host, sizeof host);
    char cpus[16];
    snprintf(cpus, sizeof cpus, "%ld", sysconf(_SC_NPROCESSORS_ONLN));

    char profile[PATH_MAX], buf[PATH_MAX];
    snprintf(profile, sizeof profile, "C:\\users\\%s", user);
    ensure_dir(profile);

    env_put("PATH", "C:\\windows\\system32;C:\\windows");
    env_put("OS", "Windows_NT");
    env_put("SystemDrive", "C:");
    env_put("SystemRoot", "C:\\windows");
    env_put("windir", "C:\\windows");
    env_put("COMPUTERNAME", host);
    env_put("USERNAME", user);
    env_put("USERPROFILE", profile);
    env_put("HOMEDRIVE", "C:");
    env_put("HOMEPATH", profile + 2);
    env_put("PROCESSOR_ARCHITECTURE", "AMD64");
    env_put("NUMBER_OF_PROCESSORS", cpus);
    env_put("PATHEXT", ".COM;.EXE;.BAT;.CMD");
    env_put("ProgramFiles", "C:\\Program Files");
    env_put("ProgramData", "C:\\ProgramData");
    env_put("PUBLIC", "C:\\users\\Public");

    snprintf(buf, sizeof buf, "%s\\AppData\\Roaming", profile);
    ensure_dir(buf);
    env_put("APPDATA", buf);
    snprintf(buf, sizeof buf, "%s\\AppData\\Local", profile);
    ensure_dir(buf);
    env_put("LOCALAPPDATA", buf);
    snprintf(buf, sizeof buf, "%s\\AppData\\Local\\Temp", profile);
    ensure_dir(buf);
    env_put("TEMP", buf);
    env_put("TMP", buf);
}

void process_init(int argc, char **argv)
{
    if (!realpath(argv[0], exe_host))
        snprintf(exe_host, sizeof exe_host, "%s", argv[0]);
    exe_windows = path_to_windows(exe_host);

    argc_ = argc;
    argv_ = calloc(argc + 1, sizeof *argv_);
    wargv = calloc(argc + 1, sizeof *wargv);
    argv_[0] = exe_windows;
    for (int i = 1; i < argc; i++)
        argv_[i] = argv[i];

    size_t len = 0, cap = 0;
    for (int i = 0; i < argc; i++) {
        quote(&cmdline, &len, &cap, argv_[i]);
        wargv[i] = host_to_wide(argv_[i]);
    }
    wcmdline = host_to_wide(cmdline);
    env_defaults();
}

char **cmdline_split(const char *cmd, int *argc)
{
    size_t cap = 8, n = 0;
    char **argv = malloc(cap * sizeof *argv);
    char *buf = malloc(strlen(cmd) + 1);
    const char *p = cmd;
    for (;;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        char *o = buf;
        bool quoted = false;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            size_t slashes = 0;
            while (*p == '\\')
                p++, slashes++;
            if (*p == '"') {
                for (size_t i = 0; i < slashes / 2; i++)
                    *o++ = '\\';
                if (slashes % 2)
                    *o++ = '"';
                else if (quoted && p[1] == '"')
                    *o++ = '"', p++;
                else
                    quoted = !quoted;
                p++;
            } else {
                for (size_t i = 0; i < slashes; i++)
                    *o++ = '\\';
                if (*p && (quoted || (*p != ' ' && *p != '\t')))
                    *o++ = *p++;
            }
        }
        *o = 0;
        if (n + 2 > cap) {
            cap *= 2;
            argv = realloc(argv, cap * sizeof *argv);
        }
        argv[n++] = strdup(buf);
    }
    argv[n] = NULL;
    free(buf);
    *argc = n;
    return argv;
}

int process_argc(void) { return argc_; }
char **process_argv(void) { return argv_; }
WCHAR **process_wargv(void) { return wargv; }
const char *process_cmdline(void) { return cmdline; }
const WCHAR *process_wcmdline(void) { return wcmdline; }
const char *process_exe_host(void) { return exe_host; }
const char *process_exe_windows(void) { return exe_windows; }

const char *env_get(const char *name)
{
    pthread_mutex_lock(&env_lock);
    ssize_t i = env_find(name, strlen(name));
    const char *v = i >= 0 ? strchr(env[i], '=') + 1 : NULL;
    pthread_mutex_unlock(&env_lock);
    return v;
}

void env_set(const char *name, const char *value)
{
    pthread_mutex_lock(&env_lock);
    env_put(name, value);
    pthread_mutex_unlock(&env_lock);
}

char **env_vector(void) { return env; }

char *env_block(void)
{
    pthread_mutex_lock(&env_lock);
    size_t total = 1;
    for (size_t i = 0; i < env_count; i++)
        total += strlen(env[i]) + 1;
    char *block = malloc(total), *o = block;
    for (size_t i = 0; i < env_count; i++)
        o = stpcpy(o, env[i]) + 1;
    *o = 0;
    pthread_mutex_unlock(&env_lock);
    return block;
}

WCHAR *env_block_w(void)
{
    char *block = env_block();
    size_t len = 0;
    while (block[len] || block[len + 1])
        len++;
    len += 2;
    int n = utf8_to_utf16(block, len, NULL, 0);
    WCHAR *w = malloc(n * sizeof *w);
    utf8_to_utf16(block, len, w, n);
    free(block);
    return w;
}

void process_exit(UINT code)
{
    fflush(NULL);
    _exit(code);
}
