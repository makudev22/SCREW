#ifndef SCREW_EXCEPT_H
#define SCREW_EXCEPT_H

#include <screw/win.h>

#define EXCEPTION_MAXIMUM_PARAMETERS 15

#define EXCEPTION_NONCONTINUABLE 0x1
#define EXCEPTION_UNWINDING 0x2
#define EXCEPTION_EXIT_UNWIND 0x4
#define EXCEPTION_TARGET_UNWIND 0x20
#define EXCEPTION_COLLIDED_UNWIND 0x40

#define STATUS_UNWIND_CONSOLIDATE ((DWORD)0x80000029)
#define STATUS_ACCESS_VIOLATION ((DWORD)0xC0000005)
#define STATUS_NONCONTINUABLE_EXCEPTION ((DWORD)0xC0000025)

#define CONTEXT_AMD64 0x00100000
#define CONTEXT_CONTROL (CONTEXT_AMD64 | 0x1)
#define CONTEXT_INTEGER (CONTEXT_AMD64 | 0x2)
#define CONTEXT_FLOATING_POINT (CONTEXT_AMD64 | 0x8)
#define CONTEXT_FULL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)

enum unwind_op {
    UWOP_PUSH_NONVOL = 0,
    UWOP_ALLOC_LARGE = 1,
    UWOP_ALLOC_SMALL = 2,
    UWOP_SET_FPREG = 3,
    UWOP_SAVE_NONVOL = 4,
    UWOP_SAVE_NONVOL_FAR = 5,
    UWOP_EPILOG = 6,
    UWOP_SAVE_XMM128 = 8,
    UWOP_SAVE_XMM128_FAR = 9,
    UWOP_PUSH_MACHFRAME = 10,
};

#define UNW_FLAG_EHANDLER 0x1
#define UNW_FLAG_UHANDLER 0x2
#define UNW_FLAG_CHAININFO 0x4

typedef enum {
    ExceptionContinueExecution = 0,
    ExceptionContinueSearch = 1,
    ExceptionNestedException = 2,
    ExceptionCollidedUnwind = 3,
} EXCEPTION_DISPOSITION;

typedef struct {
    DWORD exception_code;
    DWORD exception_flags;
    void *exception_record;
    void *exception_address;
    DWORD number_parameters;
    DWORD reserved;
    ULONG_PTR exception_information[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD;

typedef struct {
    uint8_t header[0xA0];
    uint8_t xmm[16][16];
    uint8_t rest[96];
} XSAVE_FORMAT;

_Static_assert(sizeof(XSAVE_FORMAT) == 512, "XSAVE_FORMAT size");

typedef struct {
    DWORD64 m128_low;
    int64_t m128_high;
} M128A;

typedef struct {
    DWORD64 p_home[6];
    DWORD context_flags;
    DWORD mx_csr;
    WORD seg_cs, seg_ds, seg_es, seg_fs, seg_gs, seg_ss;
    DWORD eflags;
    DWORD64 dr0, dr1, dr2, dr3, dr6, dr7;
    DWORD64 rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi;
    DWORD64 r8, r9, r10, r11, r12, r13, r14, r15;
    DWORD64 rip;
    XSAVE_FORMAT flt_save;
    M128A vector_register[26];
    DWORD64 vector_control;
    DWORD64 debug_control, last_branch_to_rip, last_branch_from_rip, last_exception_to_rip, last_exception_from_rip;
} CONTEXT;

_Static_assert(offsetof(CONTEXT, context_flags) == 0x30, "CONTEXT flags offset");
_Static_assert(offsetof(CONTEXT, rax) == 0x78, "CONTEXT rax offset");
_Static_assert(offsetof(CONTEXT, rsp) == 0x98, "CONTEXT rsp offset");
_Static_assert(offsetof(CONTEXT, rip) == 0xF8, "CONTEXT rip offset");
_Static_assert(offsetof(CONTEXT, flt_save) == 0x100, "CONTEXT flt_save offset");
_Static_assert(offsetof(CONTEXT, flt_save.xmm) == 0x1A0, "CONTEXT xmm offset");

typedef struct {
    DWORD begin_address;
    DWORD end_address;
    DWORD unwind_data;
} RUNTIME_FUNCTION;

typedef struct {
    uint8_t version_flags;
    uint8_t size_of_prolog;
    uint8_t count_of_codes;
    uint8_t frame;
    uint16_t codes[];
} UNWIND_INFO;

static inline unsigned unwind_version(const UNWIND_INFO *u) { return u->version_flags & 0x7; }
static inline unsigned unwind_flags(const UNWIND_INFO *u) { return u->version_flags >> 3; }
static inline unsigned unwind_frame_reg(const UNWIND_INFO *u) { return u->frame & 0xF; }
static inline unsigned unwind_frame_off(const UNWIND_INFO *u) { return u->frame >> 4; }

typedef struct {
    EXCEPTION_RECORD *record;
    void *value;
    CONTEXT *context;
    void *target;
} DISPATCHER_CONTEXT_HEAD;

typedef EXCEPTION_DISPOSITION WINAPI (*EXCEPTION_ROUTINE)(EXCEPTION_RECORD *, void *, CONTEXT *, void *);

typedef struct {
    DWORD64 control_pc;
    DWORD64 image_base;
    RUNTIME_FUNCTION *function_entry;
    DWORD64 establisher_frame;
    DWORD64 target_ip;
    CONTEXT *context;
    EXCEPTION_ROUTINE language_handler;
    void *handler_data;
    void *history_table;
    DWORD scope_index;
    DWORD fill0;
} DISPATCHER_CONTEXT;

void WINAPI RtlCaptureContext(CONTEXT *context);
RUNTIME_FUNCTION *WINAPI RtlLookupFunctionEntry(DWORD64 pc, DWORD64 *image_base, void *history);
EXCEPTION_ROUTINE WINAPI RtlVirtualUnwind(DWORD type, DWORD64 image_base, DWORD64 pc, RUNTIME_FUNCTION *fn,
                                          CONTEXT *context, void **handler_data, DWORD64 *establisher,
                                          void *regs);
_Noreturn void WINAPI RtlUnwindEx(void *target_frame, void *target_ip, EXCEPTION_RECORD *record, void *value,
                                  CONTEXT *context, void *history);

void screw_raise_exception(DWORD code, DWORD flags, DWORD argc, const ULONG_PTR *args);
LONG WINAPI screw_c_specific_handler(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *disp);
LONG screw_unhandled(EXCEPTION_RECORD *record, CONTEXT *context);

extern const struct bit_table k32_unwind;

#endif
