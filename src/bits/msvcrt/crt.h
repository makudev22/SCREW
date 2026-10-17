#ifndef SCREW_CRT_H
#define SCREW_CRT_H

#include <screw/bit.h>
#include <screw/process.h>
#include <screw/teb.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef __builtin_ms_va_list ms_va_list;
#define ms_va_start(ap, last) __builtin_ms_va_start(ap, last)
#define ms_va_end(ap) __builtin_ms_va_end(ap)
#define ms_va_arg(ap, type) __builtin_va_arg(ap, type)

typedef void WINAPI (*crt_fn)(void);
typedef int WINAPI (*crt_ifn)(void);

struct wfile {
    char *ptr;
    int cnt;
    char *base;
    int flag;
    int file;
    int charbuf;
    int bufsiz;
    char *tmpfname;
};

_Static_assert(sizeof(struct wfile) == 48, "msvcrt FILE layout");

extern const struct bit_table crt_startup;
extern const struct bit_table crt_stdio;
extern const struct bit_table crt_format;
extern const struct bit_table crt_scan;
extern const struct bit_table crt_string;
extern const struct bit_table crt_wide;
extern const struct bit_table crt_stdlib;
extern const struct bit_table crt_math;
extern const struct bit_table crt_time;
extern const struct bit_table crt_io;
extern const struct bit_table crt_misc;
extern const struct bit_table crt_wstream;
extern const struct bit_table crt_jump;

FILE *crt_host(struct wfile *f);
struct wfile *crt_stream(int index);
void crt_flush_all(void);
int crt_getc(struct wfile *f);

int *crt_errno(void);
void crt_errno_set(int e);

void crt_init(void);
void crt_run_atexit(void);
_Noreturn void crt_exit(int code);

#endif
