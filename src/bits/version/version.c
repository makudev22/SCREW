#include <screw/bit.h>
#include <screw/path.h>
#include <screw/pe.h>
#include <screw/teb.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define ERROR_RESOURCE_TYPE_NOT_FOUND 1813
#define HEADER_MAGIC 0x53435257u

struct header {
    uint32_t magic;
    uint32_t raw_size;
    uint32_t ansi_used;
    uint32_t ansi_size;
};

struct block {
    WORD length;
    WORD value_length;
    WORD type;
    WCHAR key[];
};

static uint32_t align4(uint32_t v) { return (v + 3) & ~3u; }

static WCHAR towlower_ascii(WCHAR c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static const uint8_t *rva_to_file(const uint8_t *file, size_t size, uint32_t rva)
{
    const struct pe_dos *dos = (const struct pe_dos *)file;
    const struct pe_nt *nt = (const struct pe_nt *)(file + dos->nt_offset);
    const struct pe_section *sec = pe_sections((struct pe_nt *)nt);
    for (unsigned i = 0; i < nt->file.section_count; i++) {
        uint32_t span = sec[i].virtual_size > sec[i].raw_size ? sec[i].virtual_size : sec[i].raw_size;
        if (rva >= sec[i].rva && rva < sec[i].rva + span) {
            size_t off = sec[i].raw_offset + (rva - sec[i].rva);
            return off < size ? file + off : NULL;
        }
    }
    return NULL;
}

static const struct pe_resource_entry *first_entry(const uint8_t *root, uint32_t offset, uint32_t id, bool any)
{
    const struct pe_resource_dir *dir = (const struct pe_resource_dir *)(root + offset);
    const struct pe_resource_entry *e = (const struct pe_resource_entry *)(dir + 1);
    unsigned count = dir->named_count + dir->id_count;
    for (unsigned i = 0; i < count; i++)
        if (any || (!(e[i].name & PE_RESOURCE_SUBDIR) && e[i].name == id))
            return &e[i];
    return NULL;
}

static uint8_t *version_resource(const char *win_path, uint32_t *size_out)
{
    char *host = path_to_host(win_path);
    int fd = host ? open(host, O_RDONLY | O_CLOEXEC) : -1;
    free(host);
    if (fd < 0) {
        last_error_set(ERROR_FILE_NOT_FOUND);
        return NULL;
    }
    struct stat st;
    uint8_t *file = MAP_FAILED;
    if (!fstat(fd, &st) && st.st_size > (off_t)sizeof(struct pe_dos))
        file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file == MAP_FAILED) {
        last_error_set(ERROR_FILE_NOT_FOUND);
        return NULL;
    }

    uint8_t *out = NULL;
    size_t size = st.st_size;
    const struct pe_dos *dos = (const struct pe_dos *)file;
    const struct pe_nt *nt = (const struct pe_nt *)(file + dos->nt_offset);
    if (dos->magic != PE_DOS_MAGIC || (size_t)dos->nt_offset + sizeof *nt > size || nt->signature != PE_NT_MAGIC ||
        nt->opt.magic != PE_OPT64_MAGIC || nt->opt.dir_count <= PE_DIR_RESOURCE ||
        !nt->opt.dirs[PE_DIR_RESOURCE].rva)
        goto done;

    const uint8_t *root = rva_to_file(file, size, nt->opt.dirs[PE_DIR_RESOURCE].rva);
    if (!root)
        goto done;
    const struct pe_resource_entry *type = first_entry(root, 0, PE_RT_VERSION, false);
    if (!type || !(type->offset & PE_RESOURCE_SUBDIR))
        goto done;
    const struct pe_resource_entry *name = first_entry(root, type->offset & ~PE_RESOURCE_SUBDIR, 0, true);
    if (!name || !(name->offset & PE_RESOURCE_SUBDIR))
        goto done;
    const struct pe_resource_entry *lang = first_entry(root, name->offset & ~PE_RESOURCE_SUBDIR, 0, true);
    if (!lang || (lang->offset & PE_RESOURCE_SUBDIR))
        goto done;
    const struct pe_resource_data *data = (const struct pe_resource_data *)(root + lang->offset);
    const uint8_t *bytes = rva_to_file(file, size, data->rva);
    if (!bytes || bytes + data->size > file + size)
        goto done;
    out = malloc(data->size);
    memcpy(out, bytes, data->size);
    *size_out = data->size;
done:
    munmap(file, size);
    if (!out)
        last_error_set(ERROR_RESOURCE_TYPE_NOT_FOUND);
    return out;
}

static DWORD info_size(const char *path)
{
    uint32_t raw;
    uint8_t *res = version_resource(path, &raw);
    if (!res)
        return 0;
    free(res);
    return sizeof(struct header) + align4(raw) + raw * 2;
}

static BOOL info(const char *path, DWORD len, void *buf)
{
    uint32_t raw;
    uint8_t *res = version_resource(path, &raw);
    if (!res)
        return FALSE;
    DWORD need = sizeof(struct header) + align4(raw);
    if (len < need) {
        free(res);
        last_error_set(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    struct header *h = buf;
    *h = (struct header){ HEADER_MAGIC, raw, 0, len - need };
    memcpy(h + 1, res, raw);
    free(res);
    return TRUE;
}

static DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR path, LPDWORD handle)
{
    if (handle)
        *handle = 0;
    return info_size(path);
}

static DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR path, LPDWORD handle)
{
    char *p = wide_to_host(path);
    DWORD r = GetFileVersionInfoSizeA(p, handle);
    free(p);
    return r;
}

static DWORD WINAPI GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR path, LPDWORD handle)
{
    return GetFileVersionInfoSizeW(path, handle);
}

static BOOL WINAPI GetFileVersionInfoA(LPCSTR path, DWORD handle, DWORD len, void *buf) { return info(path, len, buf); }

static BOOL WINAPI GetFileVersionInfoW(LPCWSTR path, DWORD handle, DWORD len, void *buf)
{
    char *p = wide_to_host(path);
    BOOL r = info(p, len, buf);
    free(p);
    return r;
}

static BOOL WINAPI GetFileVersionInfoExW(DWORD flags, LPCWSTR path, DWORD handle, DWORD len, void *buf)
{
    return GetFileVersionInfoW(path, handle, len, buf);
}

static const uint8_t *block_value(const struct block *b)
{
    size_t key = (wstr_len(b->key) + 1) * sizeof(WCHAR);
    return (const uint8_t *)b + align4(6 + key);
}

static const struct block *child(const struct block *b, const WCHAR *name, size_t name_len)
{
    const uint8_t *end = (const uint8_t *)b + b->length;
    size_t value_bytes = b->type == 1 ? b->value_length * sizeof(WCHAR) : b->value_length;
    const uint8_t *p = block_value(b) + align4(value_bytes);
    while (p + 6 < end) {
        const struct block *c = (const struct block *)p;
        if (!c->length)
            break;
        size_t klen = wstr_len(c->key);
        bool match = klen == name_len;
        for (size_t i = 0; match && i < klen; i++)
            match = towlower_ascii(c->key[i]) == towlower_ascii(name[i]);
        if (match)
            return c;
        p += align4(c->length);
    }
    return NULL;
}

static const struct block *find(const struct block *root, const WCHAR *path)
{
    const struct block *b = root;
    while (*path) {
        while (*path == '\\')
            path++;
        if (!*path)
            break;
        const WCHAR *end = path;
        while (*end && *end != '\\')
            end++;
        b = child(b, path, end - path);
        if (!b)
            return NULL;
        path = end;
    }
    return b;
}

static BOOL query(const void *buf, const WCHAR *path, void **out, UINT *len, bool ansi)
{
    const struct header *h = buf;
    if (!buf || h->magic != HEADER_MAGIC)
        return FALSE;
    const struct block *b = find((const struct block *)(h + 1), path);
    if (!b || !b->value_length) {
        last_error_set(ERROR_RESOURCE_TYPE_NOT_FOUND);
        return FALSE;
    }
    void *value = (void *)block_value(b);
    UINT n = b->value_length;
    if (b->type == 1 && ansi) {
        struct header *wh = (struct header *)buf;
        char *area = (char *)(wh + 1) + align4(wh->raw_size) + wh->ansi_used;
        int need = utf16_to_utf8(value, n, NULL, 0);
        if (need < 0 || wh->ansi_used + need + 1 > wh->ansi_size)
            return FALSE;
        utf16_to_utf8(value, n, area, need);
        area[need] = 0;
        wh->ansi_used += need + 1;
        value = area;
        n = strlen(area) + 1;
    }
    if (out)
        *out = value;
    if (len)
        *len = n;
    return TRUE;
}

static BOOL WINAPI VerQueryValueW(const void *buf, LPCWSTR sub, void **out, UINT *len)
{
    return query(buf, sub, out, len, false);
}

static BOOL WINAPI VerQueryValueA(const void *buf, LPCSTR sub, void **out, UINT *len)
{
    WCHAR *w = host_to_wide(sub);
    BOOL r = query(buf, w, out, len, true);
    free(w);
    return r;
}

BIT_TABLE(version_exports,
          BIT_FN(GetFileVersionInfoSizeA), BIT_FN(GetFileVersionInfoSizeW), BIT_FN(GetFileVersionInfoSizeExW),
          BIT_FN(GetFileVersionInfoA), BIT_FN(GetFileVersionInfoW), BIT_FN(GetFileVersionInfoExW),
          BIT_FN(VerQueryValueA), BIT_FN(VerQueryValueW));

BIT_DEFINE(bit_version, "version.dll", &version_exports);
