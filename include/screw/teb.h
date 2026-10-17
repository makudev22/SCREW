#ifndef SCREW_TEB_H
#define SCREW_TEB_H

#include <screw/win.h>

#include <stddef.h>

#define TLS_SLOTS 64
#define TLS_EXPANSION 1024
#define TLS_IMPLICIT_MAX 64

struct peb {
    BYTE inherited;
    BYTE read_image_options;
    BYTE being_debugged;
    BYTE flags;
    BYTE pad0[4];
    void *mutant;
    void *image_base;
    void *ldr;
    void *parameters;
    void *subsystem_data;
    void *process_heap;
    BYTE reserved[0x7d0 - 0x38];
};

struct teb {
    void *exception_list;
    void *stack_base;
    void *stack_limit;
    void *subsystem_tib;
    void *fiber_data;
    void *arbitrary;
    struct teb *self;
    void *environment;
    ULONG_PTR process_id;
    ULONG_PTR thread_id;
    void *rpc_handle;
    void **tls_pointer;
    struct peb *peb;
    DWORD last_error;
    BYTE reserved0[0x1480 - 0x6c];
    void *tls_slots[TLS_SLOTS];
    BYTE reserved1[0x1780 - 0x1680];
    void **tls_expansion;
    BYTE reserved2[0x1800 - 0x1788];
    void *implicit_tls[TLS_IMPLICIT_MAX];
    HANDLE handle;
};

_Static_assert(offsetof(struct peb, process_heap) == 0x30, "peb layout");
_Static_assert(offsetof(struct teb, self) == 0x30, "teb layout");
_Static_assert(offsetof(struct teb, tls_pointer) == 0x58, "teb layout");
_Static_assert(offsetof(struct teb, peb) == 0x60, "teb layout");
_Static_assert(offsetof(struct teb, last_error) == 0x68, "teb layout");
_Static_assert(offsetof(struct teb, tls_slots) == 0x1480, "teb layout");
_Static_assert(offsetof(struct teb, tls_expansion) == 0x1780, "teb layout");

struct peb *peb_get(void);
struct teb *teb_current(void);
struct teb *teb_create(void);
void teb_bind(struct teb *t);
void teb_destroy(struct teb *t);

static inline void last_error_set(DWORD e) { teb_current()->last_error = e; }
static inline DWORD last_error_get(void) { return teb_current()->last_error; }

#endif
