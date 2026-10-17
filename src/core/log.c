#include <screw/log.h>

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static bool tracing;
static bool lenient;
static FILE *file;
static struct timespec start;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static bool env_flag(const char *name)
{
    const char *v = getenv(name);
    return v && *v && *v != '0';
}

void log_init(void)
{
    clock_gettime(CLOCK_MONOTONIC, &start);
    tracing = env_flag("SCREW_TRACE");
    lenient = env_flag("SCREW_LENIENT");
    const char *path = getenv("SCREW_LOG");
    if (path && *path) {
        file = fopen(path, "a");
        if (file) {
            setvbuf(file, NULL, _IOLBF, 0);
            tracing = true;
        } else {
            fprintf(stderr, "screw: cannot open log file %s\n", path);
        }
    }
}

bool log_tracing(void) { return tracing; }
bool log_lenient(void) { return lenient; }
int log_fd(void) { return file ? fileno(file) : 2; }

static void write_line(FILE *out, const char *tag, const char *fmt, va_list ap)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double t = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
    fprintf(out, "screw%s %8.3f %5ld: ", tag, t, (long)syscall(SYS_gettid));
    vfprintf(out, fmt, ap);
    fputc('\n', out);
}

static void emit(const char *tag, bool always_stderr, const char *fmt, va_list ap)
{
    pthread_mutex_lock(&lock);
    if (file) {
        va_list copy;
        va_copy(copy, ap);
        write_line(file, tag, fmt, copy);
        va_end(copy);
    }
    if (!file || always_stderr)
        write_line(stderr, tag, fmt, ap);
    pthread_mutex_unlock(&lock);
}

void trace(const char *fmt, ...)
{
    if (!tracing)
        return;
    va_list ap;
    va_start(ap, fmt);
    emit("[trace]", false, fmt, ap);
    va_end(ap);
}

void warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit("", true, fmt, ap);
    va_end(ap);
}

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit("", true, fmt, ap);
    va_end(ap);
    if (file)
        fflush(file);
    exit(127);
}
