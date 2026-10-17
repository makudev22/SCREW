#ifndef SCREW_LOADER_H
#define SCREW_LOADER_H

#include <screw/module.h>

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3

struct module *image_map(const char *path);
void image_protect(struct module *m);

bool reloc_apply(struct module *m, int64_t delta);
void imports_resolve(struct module *m);
void *exports_lookup(struct module *m, const char *name);
void *exports_ordinal(struct module *m, uint32_t ordinal);

void *trap_make(const char *dll, const char *func);

void tls_register(struct module *m);
void tls_thread_init(void);
void tls_callbacks(struct module *m, uint32_t reason);

#endif
