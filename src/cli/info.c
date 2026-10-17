#include "cli.h"

#include <screw/loader.h>
#include <screw/log.h>

#include <stdio.h>

static const char *subsystem(uint16_t s)
{
    return s == PE_SUBSYSTEM_GUI ? "windows gui" : s == PE_SUBSYSTEM_CONSOLE ? "console" : "other";
}

int cli_info(const char *path)
{
    struct module *m = image_map(path);
    if (!m)
        die("cannot open %s", path);

    printf("%s: %s, %s, entry +0x%x, image %zu KiB\n", path, m->is_dll ? "dll" : "program",
           subsystem(m->nt->opt.subsystem), m->nt->opt.entry, m->size / 1024);

    uint32_t size;
    struct pe_import *imp = module_dir(m, PE_DIR_IMPORT, &size);
    size_t total = 0, missing = 0;
    for (; imp && imp->name_rva; imp++) {
        const char *dll = (const char *)m->base + imp->name_rva;
        char norm[MODULE_NAME_MAX];
        module_normalize(dll, norm);
        const struct bit *b = bit_find(norm);
        printf("\n  %s %s\n", dll, b ? "(built in)" : "(native dll needed)");
        uint64_t *t = (uint64_t *)(m->base + (imp->lookup_rva ? imp->lookup_rva : imp->iat_rva));
        for (; *t; t++) {
            total++;
            if (*t & PE_ORDINAL_FLAG) {
                printf("    ?  #%llu\n", (unsigned long long)(*t & 0xFFFF));
                continue;
            }
            const char *fn = (const char *)m->base + (uint32_t)*t + 2;
            bool have = b && bit_symbol(b, fn);
            if (b && !have)
                missing++;
            printf("    %s  %s\n", !b ? "?" : have ? "+" : "-", fn);
        }
    }
    printf("\n%zu imports, %zu missing from bits\n", total, missing);
    return missing ? 1 : 0;
}
