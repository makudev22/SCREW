#ifndef SCREW_BIT_H
#define SCREW_BIT_H

#include <stddef.h>

struct bit_export {
    const char *name;
    void *addr;
};

struct bit {
    const char *name;
    const struct bit_export *exports;
    size_t count;
};

#define BIT_FN(f) { #f, (void *)(f) }
#define BIT_AS(n, f) { n, (void *)(f) }
#define BIT_DATA(n, v) { n, (void *)&(v) }

#define BIT_DEFINE(var, dll, table) \
    const struct bit var = { dll, table, sizeof(table) / sizeof((table)[0]) }

const struct bit *bit_find(const char *dll);
void *bit_symbol(const struct bit *b, const char *name);

#endif
