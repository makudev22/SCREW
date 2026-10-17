#include <screw/log.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static bool tracing;
static bool lenient;

static bool env_flag(const char *name)
{
    const char *v = getenv(name);
    return v && *v && *v != '0';
}

void log_init(void)
{
    tracing = env_flag("SCREW_TRACE");
    lenient = env_flag("SCREW_LENIENT");
}

bool log_tracing(void) { return tracing; }
bool log_lenient(void) { return lenient; }

static void emit(const char *tag, const char *fmt, va_list ap)
{
    fprintf(stderr, "screw%s: ", tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void trace(const char *fmt, ...)
{
    if (!tracing)
        return;
    va_list ap;
    va_start(ap, fmt);
    emit("[trace]", fmt, ap);
    va_end(ap);
}

void warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit("", fmt, ap);
    va_end(ap);
}

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit("", fmt, ap);
    va_end(ap);
    exit(127);
}
