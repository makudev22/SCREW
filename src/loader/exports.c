#include <screw/loader.h>
#include <screw/log.h>

#include <stdlib.h>
#include <string.h>

static void *forward(const char *spec)
{
    const char *dot = strrchr(spec, '.');
    if (!dot || dot == spec)
        return NULL;
    char dll[MODULE_NAME_MAX];
    size_t n = dot - spec;
    if (n >= sizeof dll - 4)
        return NULL;
    memcpy(dll, spec, n);
    dll[n] = 0;

    struct module *target = module_load(dll);
    if (!target)
        return NULL;
    if (dot[1] == '#')
        return module_ordinal(target, strtoul(dot + 2, NULL, 10));
    return module_symbol(target, dot + 1);
}

static void *resolve(struct module *m, struct pe_export *ex, uint32_t size, uint32_t index)
{
    if (index >= ex->function_count)
        return NULL;
    uint32_t rva = ((uint32_t *)(m->base + ex->functions_rva))[index];
    if (!rva)
        return NULL;
    uint32_t dir_rva = m->nt->opt.dirs[PE_DIR_EXPORT].rva;
    if (rva >= dir_rva && rva < dir_rva + size)
        return forward((const char *)m->base + rva);
    return m->base + rva;
}

void *exports_lookup(struct module *m, const char *name)
{
    uint32_t size;
    struct pe_export *ex = module_dir(m, PE_DIR_EXPORT, &size);
    if (!ex)
        return NULL;

    uint32_t *names = (uint32_t *)(m->base + ex->names_rva);
    uint16_t *ordinals = (uint16_t *)(m->base + ex->ordinals_rva);
    int lo = 0, hi = (int)ex->name_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(name, (const char *)m->base + names[mid]);
        if (!c)
            return resolve(m, ex, size, ordinals[mid]);
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return NULL;
}

void *exports_ordinal(struct module *m, uint32_t ordinal)
{
    uint32_t size;
    struct pe_export *ex = module_dir(m, PE_DIR_EXPORT, &size);
    if (!ex || ordinal < ex->ordinal_base)
        return NULL;
    return resolve(m, ex, size, ordinal - ex->ordinal_base);
}
