#ifndef SCREW_BIT_H
#define SCREW_BIT_H

#include <stddef.h>

struct bit_export {
    const char *name;
    void *addr;
};

struct bit_table {
    const struct bit_export *exports;
    size_t count;
};

struct bit {
    const char *name;
    const struct bit_table *const *tables;
    void *(*resolve)(const char *name);
};

#define BIT_FN(f) { #f, (void *)(f) }
#define BIT_AS(n, f) { n, (void *)(f) }
#define BIT_DATA(n, v) { n, (void *)&(v) }

#define BIT_TABLE(var, ...)                                              \
    static const struct bit_export var##_exports[] = { __VA_ARGS__ };    \
    const struct bit_table var = { var##_exports,                        \
                                   sizeof var##_exports / sizeof var##_exports[0] }

#define BIT_DEFINE(var, dll, ...)                                                \
    static const struct bit_table *const var##_tables[] = { __VA_ARGS__, NULL }; \
    const struct bit var = { dll, var##_tables, NULL }

#define BIT_DEFINE_RESOLVED(var, dll, resolver, ...)                             \
    static const struct bit_table *const var##_tables[] = { __VA_ARGS__, NULL }; \
    const struct bit var = { dll, var##_tables, resolver }

const struct bit *bit_find(const char *dll);
void *bit_symbol(const struct bit *b, const char *name);

#endif
