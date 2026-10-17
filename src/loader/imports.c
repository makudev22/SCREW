#include <screw/loader.h>
#include <screw/log.h>

#include <stdio.h>

static void *bind(struct module *dll, const char *dll_name, uint64_t thunk, uint8_t *base)
{
    if (thunk & PE_ORDINAL_FLAG) {
        uint32_t ord = thunk & 0xFFFF;
        void *addr = module_ordinal(dll, ord);
        if (addr)
            return addr;
        char label[16];
        snprintf(label, sizeof label, "#%u", ord);
        return trap_make(dll_name, label);
    }
    const char *name = (const char *)base + (uint32_t)thunk + 2;
    void *addr = module_symbol(dll, name);
    return addr ? addr : trap_make(dll_name, name);
}

void imports_resolve(struct module *m)
{
    uint32_t size;
    struct pe_import *imp = module_dir(m, PE_DIR_IMPORT, &size);
    if (!imp)
        return;

    for (; imp->name_rva; imp++) {
        const char *dll_name = (const char *)m->base + imp->name_rva;
        struct module *dll = module_load(dll_name);
        if (!dll)
            die("%s needs %s, which is neither built in nor next to the program",
                m->name, dll_name);

        uint64_t *iat = (uint64_t *)(m->base + imp->iat_rva);
        uint64_t *lookup = imp->lookup_rva ? (uint64_t *)(m->base + imp->lookup_rva) : iat;
        for (size_t i = 0; lookup[i]; i++)
            iat[i] = (uint64_t)bind(dll, dll_name, lookup[i], m->base);
        trace("linked %s -> %s", m->name, dll->name);
    }
}
