#ifndef SCREW_MODULE_H
#define SCREW_MODULE_H

#include <screw/bit.h>
#include <screw/pe.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODULE_NAME_MAX 64

struct tls_image;

struct module {
    char name[MODULE_NAME_MAX];
    char *path;
    uint8_t *base;
    size_t size;
    struct pe_nt *nt;
    const struct bit *bit;
    struct tls_image *tls;
    bool is_dll;
    bool attached;
    struct module *next;
};

void module_normalize(const char *in, char out[MODULE_NAME_MAX]);

struct module *module_list(void);
struct module *module_main(void);
struct module *module_find(const char *name);
struct module *module_at(const void *addr);
struct module *module_load(const char *name);
struct module *module_load_main(const char *host_path);
struct module *module_load_file(const char *host_path);

void *module_symbol(struct module *m, const char *name);
void *module_ordinal(struct module *m, uint32_t ordinal);

void modules_attach(void);
void modules_thread_event(uint32_t reason);

void *module_dir(struct module *m, int index, uint32_t *size);

#endif
