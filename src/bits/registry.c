#include <screw/bit.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

extern const struct bit bit_kernel32;
extern const struct bit bit_msvcrt;
extern const struct bit bit_ntdll;
extern const struct bit bit_advapi32;
extern const struct bit bit_ws2_32;
extern const struct bit bit_user32;
extern const struct bit bit_vulkan;
extern const struct bit bit_version;
extern const struct bit bit_gdi32;
extern const struct bit bit_setupapi;

static const struct bit *const bits[] = {
    &bit_kernel32,
    &bit_msvcrt,
    &bit_ntdll,
    &bit_advapi32,
    &bit_ws2_32,
    &bit_user32,
    &bit_vulkan,
    &bit_version,
    &bit_gdi32,
    &bit_setupapi,
};

#define BIT_COUNT (sizeof bits / sizeof bits[0])

struct alias {
    const char *prefix;
    const struct bit *bit;
};

static const struct alias aliases[] = {
    { "api-ms-win-crt-", &bit_msvcrt },
    { "ucrtbase", &bit_msvcrt },
    { "vcruntime140", &bit_msvcrt },
    { "msvcr", &bit_msvcrt },
    { "api-ms-win-core-", &bit_kernel32 },
    { "kernelbase", &bit_kernel32 },
    { "api-ms-win-security-", &bit_advapi32 },
    { "wsock32", &bit_ws2_32 },
    { "api-ms-win-eventing-", &bit_advapi32 },
    { "ext-ms-win-ntuser-", &bit_user32 },
    { "api-ms-win-core-version-", &bit_version },
    { "ext-ms-win-gdi-", &bit_gdi32 },
};

static struct bit_export *sorted[BIT_COUNT];
static size_t sorted_count[BIT_COUNT];
static pthread_once_t once = PTHREAD_ONCE_INIT;

static int by_name(const void *a, const void *b)
{
    return strcmp(((const struct bit_export *)a)->name, ((const struct bit_export *)b)->name);
}

static void build(void)
{
    for (size_t i = 0; i < BIT_COUNT; i++) {
        size_t n = 0;
        for (const struct bit_table *const *t = bits[i]->tables; *t; t++)
            n += (*t)->count;
        sorted[i] = malloc(n * sizeof(struct bit_export));
        n = 0;
        for (const struct bit_table *const *t = bits[i]->tables; *t; t++) {
            memcpy(sorted[i] + n, (*t)->exports, (*t)->count * sizeof(struct bit_export));
            n += (*t)->count;
        }
        qsort(sorted[i], n, sizeof(struct bit_export), by_name);
        sorted_count[i] = n;
    }
}

const struct bit *bit_find(const char *dll)
{
    for (size_t i = 0; i < BIT_COUNT; i++)
        if (!strcmp(bits[i]->name, dll))
            return bits[i];
    for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; i++)
        if (!strncmp(dll, aliases[i].prefix, strlen(aliases[i].prefix)))
            return aliases[i].bit;
    return NULL;
}

void *bit_symbol(const struct bit *b, const char *name)
{
    pthread_once(&once, build);
    for (size_t i = 0; i < BIT_COUNT; i++) {
        if (bits[i] != b)
            continue;
        struct bit_export key = { name, NULL };
        struct bit_export *e = bsearch(&key, sorted[i], sorted_count[i], sizeof key, by_name);
        if (e)
            return e->addr;
        return b->resolve ? b->resolve(name) : NULL;
    }
    return NULL;
}
