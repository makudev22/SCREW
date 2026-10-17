#include <screw/loader.h>
#include <screw/log.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAGE 4096ul
#define ALIGN_UP(x) (((x) + PAGE - 1) & ~(PAGE - 1))

static uint8_t *read_file(const char *path, size_t *size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size <= 0) {
        close(fd);
        return NULL;
    }
    uint8_t *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED)
        return NULL;
    *size = st.st_size;
    return data;
}

static struct pe_nt *validate(uint8_t *data, size_t size, const char *path)
{
    struct pe_dos *dos = (struct pe_dos *)data;
    if (size < sizeof *dos || dos->magic != PE_DOS_MAGIC)
        die("%s: not a Windows executable", path);
    if (dos->nt_offset <= 0 || (size_t)dos->nt_offset + sizeof(struct pe_nt) > size)
        die("%s: truncated PE header", path);
    struct pe_nt *nt = (struct pe_nt *)(data + dos->nt_offset);
    if (nt->signature != PE_NT_MAGIC)
        die("%s: bad PE signature", path);
    if (nt->file.machine != PE_MACHINE_AMD64 || nt->opt.magic != PE_OPT64_MAGIC)
        die("%s: only 64-bit x86 programs are supported", path);
    return nt;
}

static uint8_t *reserve(uint64_t want, size_t size)
{
    void *p = mmap((void *)want, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != MAP_FAILED && (uint64_t)p == want)
        return p;
    if (p != MAP_FAILED)
        munmap(p, size);
    p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static void copy_sections(uint8_t *base, size_t image_size, const uint8_t *file,
                          size_t file_size, const char *path)
{
    struct pe_nt *nt = (struct pe_nt *)(base + ((struct pe_dos *)base)->nt_offset);
    struct pe_section *sec = pe_sections(nt);
    for (unsigned i = 0; i < nt->file.section_count; i++) {
        if (!sec[i].raw_size || sec[i].raw_offset >= file_size)
            continue;
        size_t n = sec[i].raw_size;
        if (sec[i].virtual_size && n > sec[i].virtual_size)
            n = sec[i].virtual_size;
        if (sec[i].raw_offset + n > file_size)
            n = file_size - sec[i].raw_offset;
        if (sec[i].rva + n > image_size)
            die("%s: section %.8s outside image", path, sec[i].name);
        memcpy(base + sec[i].rva, file + sec[i].raw_offset, n);
    }
}

struct module *image_map(const char *path)
{
    size_t file_size;
    uint8_t *file = read_file(path, &file_size);
    if (!file)
        return NULL;

    struct pe_nt *fnt = validate(file, file_size, path);
    size_t image_size = ALIGN_UP(fnt->opt.image_size);
    uint8_t *base = reserve(fnt->opt.image_base, image_size);
    if (!base)
        die("%s: cannot reserve %zu bytes", path, image_size);

    size_t headers = fnt->opt.headers_size < file_size ? fnt->opt.headers_size : file_size;
    memcpy(base, file, headers);
    copy_sections(base, image_size, file, file_size, path);
    munmap(file, file_size);

    struct module *m = calloc(1, sizeof *m);
    const char *slash = strrchr(path, '/');
    module_normalize(slash ? slash + 1 : path, m->name);
    m->path = strdup(path);
    m->base = base;
    m->size = image_size;
    m->nt = (struct pe_nt *)(base + ((struct pe_dos *)base)->nt_offset);
    m->is_dll = m->nt->file.characteristics & PE_FILE_DLL;

    int64_t delta = (int64_t)((uint64_t)base - m->nt->opt.image_base);
    if (delta) {
        if (!reloc_apply(m, delta))
            die("%s: loaded at %p but image has no relocations", path, base);
        m->nt->opt.image_base = (uint64_t)base;
    }
    trace("mapped %s at %p (%zu KiB)", m->name, base, image_size / 1024);
    return m;
}

void image_protect(struct module *m)
{
    size_t pages = m->size / PAGE;
    uint8_t *prot = calloc(pages, 1);
    struct pe_section *sec = pe_sections(m->nt);

    for (unsigned i = 0; i < m->nt->file.section_count; i++) {
        uint32_t c = sec[i].characteristics;
        uint8_t p = PROT_READ;
        if (c & PE_SCN_WRITE)
            p |= PROT_WRITE;
        if (c & PE_SCN_EXECUTE)
            p |= PROT_EXEC;
        size_t span = sec[i].virtual_size ? sec[i].virtual_size : sec[i].raw_size;
        size_t last = ALIGN_UP(sec[i].rva + span) / PAGE;
        for (size_t pg = sec[i].rva / PAGE; pg < last && pg < pages; pg++)
            prot[pg] |= p;
    }

    size_t run = 0;
    for (size_t pg = 1; pg <= pages; pg++) {
        if (pg < pages && prot[pg] == prot[run])
            continue;
        mprotect(m->base + run * PAGE, (pg - run) * PAGE, prot[run] ? prot[run] : PROT_READ);
        run = pg;
    }
    free(prot);
}
