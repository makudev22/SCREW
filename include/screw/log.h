#ifndef SCREW_LOG_H
#define SCREW_LOG_H

#include <stdbool.h>

void log_init(void);
bool log_tracing(void);
bool log_lenient(void);
int log_fd(void);

void trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
_Noreturn void die(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
