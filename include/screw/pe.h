#ifndef SCREW_PE_H
#define SCREW_PE_H

#include <stdint.h>

#define PE_DOS_MAGIC 0x5A4D
#define PE_NT_MAGIC 0x00004550
#define PE_OPT64_MAGIC 0x20B
#define PE_MACHINE_AMD64 0x8664

#define PE_FILE_DLL 0x2000

#define PE_DIR_EXPORT 0
#define PE_DIR_IMPORT 1
#define PE_DIR_EXCEPTION 3
#define PE_DIR_BASERELOC 5
#define PE_DIR_TLS 9
#define PE_DIR_COUNT 16

#define PE_SCN_EXECUTE 0x20000000u
#define PE_SCN_READ 0x40000000u
#define PE_SCN_WRITE 0x80000000u

#define PE_REL_ABSOLUTE 0
#define PE_REL_HIGHLOW 3
#define PE_REL_DIR64 10

#define PE_ORDINAL_FLAG 0x8000000000000000ull

#define PE_SUBSYSTEM_GUI 2
#define PE_SUBSYSTEM_CONSOLE 3

struct pe_dos {
    uint16_t magic;
    uint16_t unused[29];
    int32_t nt_offset;
};

struct pe_file {
    uint16_t machine;
    uint16_t section_count;
    uint32_t timestamp;
    uint32_t symbol_table;
    uint32_t symbol_count;
    uint16_t optional_size;
    uint16_t characteristics;
};

struct pe_dir {
    uint32_t rva;
    uint32_t size;
};

struct pe_opt64 {
    uint16_t magic;
    uint8_t linker_major;
    uint8_t linker_minor;
    uint32_t code_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t entry;
    uint32_t code_base;
    uint64_t image_base;
    uint32_t section_align;
    uint32_t file_align;
    uint16_t os_major;
    uint16_t os_minor;
    uint16_t image_major;
    uint16_t image_minor;
    uint16_t subsystem_major;
    uint16_t subsystem_minor;
    uint32_t win32_version;
    uint32_t image_size;
    uint32_t headers_size;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint64_t stack_reserve;
    uint64_t stack_commit;
    uint64_t heap_reserve;
    uint64_t heap_commit;
    uint32_t loader_flags;
    uint32_t dir_count;
    struct pe_dir dirs[PE_DIR_COUNT];
};

struct pe_nt {
    uint32_t signature;
    struct pe_file file;
    struct pe_opt64 opt;
};

struct pe_section {
    char name[8];
    uint32_t virtual_size;
    uint32_t rva;
    uint32_t raw_size;
    uint32_t raw_offset;
    uint32_t reloc_offset;
    uint32_t lines_offset;
    uint16_t reloc_count;
    uint16_t lines_count;
    uint32_t characteristics;
};

struct pe_import {
    uint32_t lookup_rva;
    uint32_t timestamp;
    uint32_t forwarder;
    uint32_t name_rva;
    uint32_t iat_rva;
};

struct pe_export {
    uint32_t characteristics;
    uint32_t timestamp;
    uint16_t major;
    uint16_t minor;
    uint32_t name_rva;
    uint32_t ordinal_base;
    uint32_t function_count;
    uint32_t name_count;
    uint32_t functions_rva;
    uint32_t names_rva;
    uint32_t ordinals_rva;
};

struct pe_reloc_block {
    uint32_t page_rva;
    uint32_t block_size;
};

struct pe_tls {
    uint64_t raw_start;
    uint64_t raw_end;
    uint64_t index_address;
    uint64_t callbacks;
    uint32_t zero_fill;
    uint32_t characteristics;
};

static inline struct pe_section *pe_sections(struct pe_nt *nt)
{
    return (struct pe_section *)((uint8_t *)&nt->opt + nt->file.optional_size);
}

#endif
