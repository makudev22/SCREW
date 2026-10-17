#include <screw/loader.h>
#include <screw/log.h>

bool reloc_apply(struct module *m, int64_t delta)
{
    uint32_t size;
    uint8_t *p = module_dir(m, PE_DIR_BASERELOC, &size);
    if (!p)
        return false;

    uint8_t *end = p + size;
    while (p + sizeof(struct pe_reloc_block) <= end) {
        struct pe_reloc_block *blk = (struct pe_reloc_block *)p;
        if (blk->block_size < sizeof *blk)
            break;
        uint16_t *entry = (uint16_t *)(blk + 1);
        size_t count = (blk->block_size - sizeof *blk) / sizeof *entry;
        for (size_t i = 0; i < count; i++) {
            uint8_t *target = m->base + blk->page_rva + (entry[i] & 0xFFF);
            switch (entry[i] >> 12) {
            case PE_REL_ABSOLUTE:
                break;
            case PE_REL_DIR64:
                *(uint64_t *)target += delta;
                break;
            case PE_REL_HIGHLOW:
                *(uint32_t *)target += (uint32_t)delta;
                break;
            default:
                die("%s: unsupported relocation type %u", m->name, entry[i] >> 12);
            }
        }
        p += blk->block_size;
    }
    trace("relocated %s by %+lld", m->name, (long long)delta);
    return true;
}
