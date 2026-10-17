#include "k32.h"

#include <screw/except.h>
#include <screw/log.h>
#include <screw/module.h>
#include <screw/pe.h>

#include <stdio.h>

_Noreturn void WINAPI screw_restore_context(CONTEXT *context);

typedef struct {
    DWORD begin;
    DWORD end;
    DWORD handler;
    DWORD target;
} SCOPE_RECORD;

typedef struct {
    DWORD count;
    SCOPE_RECORD records[];
} SCOPE_TABLE;

typedef LONG WINAPI (*scope_filter)(void *pointers, void *frame);
typedef void WINAPI (*scope_finally)(BOOL abnormal, void *frame);

static DWORD64 *gpr(CONTEXT *c) { return &c->rax; }

static unsigned opcode_slots(uint16_t code)
{
    unsigned op = (code >> 8) & 0xF, info = (code >> 12) & 0xF;
    switch (op) {
    case UWOP_ALLOC_LARGE: return info ? 3 : 2;
    case UWOP_SAVE_NONVOL:
    case UWOP_SAVE_XMM128: return 2;
    case UWOP_SAVE_NONVOL_FAR:
    case UWOP_SAVE_XMM128_FAR: return 3;
    default: return 1;
    }
}

RUNTIME_FUNCTION *WINAPI RtlLookupFunctionEntry(DWORD64 pc, DWORD64 *image_base, void *history)
{
    struct module *m = module_at((void *)pc);
    if (!m || m->bit || !m->nt) {
        if (image_base)
            *image_base = 0;
        return NULL;
    }
    if (image_base)
        *image_base = (DWORD64)m->base;
    uint32_t size;
    RUNTIME_FUNCTION *table = module_dir(m, PE_DIR_EXCEPTION, &size);
    if (!table)
        return NULL;
    uint32_t rva = (uint32_t)(pc - (DWORD64)m->base);
    int lo = 0, hi = (int)(size / sizeof *table) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (rva < table[mid].begin_address)
            hi = mid - 1;
        else if (rva >= table[mid].end_address)
            lo = mid + 1;
        else
            return &table[mid];
    }
    return NULL;
}

EXCEPTION_ROUTINE WINAPI RtlVirtualUnwind(DWORD type, DWORD64 base, DWORD64 pc, RUNTIME_FUNCTION *function,
                                          CONTEXT *context, void **data, DWORD64 *frame_ret, void *regs)
{
    DWORD64 frame = *frame_ret = context->rsp;
    DWORD64 *reg = gpr(context);
    UNWIND_INFO *info;
    bool machine_frame = false;

    for (;;) {
        info = (UNWIND_INFO *)(base + function->unwind_data);
        unsigned prolog = pc - (base + function->begin_address);
        if (prolog >= info->size_of_prolog)
            prolog = ~0u;

        for (unsigned i = 0; i < info->count_of_codes; i += opcode_slots(info->codes[i])) {
            uint16_t code = info->codes[i];
            unsigned off = code & 0xFF, op = (code >> 8) & 0xF, opinfo = (code >> 12) & 0xF;
            if (prolog < off)
                continue;
            switch (op) {
            case UWOP_PUSH_NONVOL:
                reg[opinfo] = *(DWORD64 *)context->rsp;
                context->rsp += 8;
                break;
            case UWOP_ALLOC_LARGE:
                context->rsp += opinfo ? (info->codes[i + 1] | ((DWORD)info->codes[i + 2] << 16))
                                       : info->codes[i + 1] * 8u;
                break;
            case UWOP_ALLOC_SMALL:
                context->rsp += opinfo * 8 + 8;
                break;
            case UWOP_SET_FPREG:
                frame = reg[unwind_frame_reg(info)] - unwind_frame_off(info) * 16;
                context->rsp = frame;
                break;
            case UWOP_SAVE_NONVOL:
                reg[opinfo] = *(DWORD64 *)(frame + info->codes[i + 1] * 8u);
                break;
            case UWOP_SAVE_NONVOL_FAR:
                reg[opinfo] = *(DWORD64 *)(frame + (info->codes[i + 1] | ((DWORD)info->codes[i + 2] << 16)));
                break;
            case UWOP_SAVE_XMM128:
                memcpy(&context->flt_save.xmm[opinfo], (void *)(frame + info->codes[i + 1] * 16u), 16);
                break;
            case UWOP_SAVE_XMM128_FAR:
                memcpy(&context->flt_save.xmm[opinfo],
                       (void *)(frame + (info->codes[i + 1] | ((DWORD)info->codes[i + 2] << 16))), 16);
                break;
            case UWOP_PUSH_MACHFRAME:
                if (opinfo)
                    context->rsp += 8;
                context->rip = *(DWORD64 *)context->rsp;
                context->rsp = *(DWORD64 *)(context->rsp + 24);
                machine_frame = true;
                break;
            default:
                break;
            }
        }
        if (!(unwind_flags(info) & UNW_FLAG_CHAININFO))
            break;
        unsigned slots = (info->count_of_codes + 1) & ~1u;
        function = (RUNTIME_FUNCTION *)&info->codes[slots];
    }

    if (!machine_frame) {
        context->rip = *(DWORD64 *)context->rsp;
        context->rsp += 8;
    }

    if ((type & 0x2) && (unwind_flags(info) & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER))) {
        unsigned slots = (info->count_of_codes + 1) & ~1u;
        DWORD *handler = (DWORD *)&info->codes[slots];
        if (data)
            *data = handler + 1;
        return (EXCEPTION_ROUTINE)(base + *handler);
    }
    if (data)
        *data = NULL;
    return NULL;
}

static bool in_stack(DWORD64 sp)
{
    struct teb *t = teb_current();
    if (!t || !t->stack_base || !t->stack_limit)
        return sp && !(sp & 7) ? true : false;
    return sp >= (DWORD64)t->stack_limit && sp < (DWORD64)t->stack_base && !(sp & 7);
}

static EXCEPTION_ROUTINE host_handler(EXCEPTION_ROUTINE h) { return h; }

static bool dispatch(EXCEPTION_RECORD *record, CONTEXT *context)
{
    CONTEXT walk = *context;
    while (in_stack(walk.rsp)) {
        DWORD64 base, establisher;
        RUNTIME_FUNCTION *fn = RtlLookupFunctionEntry(walk.rip, &base, NULL);
        CONTEXT next = walk;
        if (fn) {
            void *handler_data;
            EXCEPTION_ROUTINE handler =
                RtlVirtualUnwind(0x2, base, walk.rip, fn, &next, &handler_data, &establisher, NULL);
            if (handler) {
                DISPATCHER_CONTEXT disp = { walk.rip, base, fn, establisher, 0, &walk, handler, handler_data,
                                            NULL, 0, 0 };
                EXCEPTION_DISPOSITION d =
                    host_handler(handler)(record, (void *)establisher, &walk, &disp);
                if (d == ExceptionContinueExecution) {
                    if (record->exception_flags & EXCEPTION_NONCONTINUABLE)
                        return false;
                    screw_restore_context(context);
                }
                if (d == ExceptionNestedException || d == ExceptionCollidedUnwind)
                    return false;
            }
        } else {
            next.rip = *(DWORD64 *)walk.rsp;
            next.rsp = walk.rsp + 8;
        }
        if (!next.rip)
            break;
        walk = next;
    }
    return false;
}

static _Noreturn void dispatch_unwind(EXCEPTION_RECORD *record, void *target_frame, void *target_ip, void *retval,
                            CONTEXT *context)
{
    CONTEXT walk = *context;
    while (in_stack(walk.rsp)) {
        DWORD64 base, establisher;
        RUNTIME_FUNCTION *fn = RtlLookupFunctionEntry(walk.rip, &base, NULL);
        CONTEXT next = walk;
        if (!fn) {
            if (!walk.rsp)
                break;
            next.rip = *(DWORD64 *)walk.rsp;
            next.rsp = walk.rsp + 8;
            walk = next;
            continue;
        }
        void *handler_data;
        EXCEPTION_ROUTINE handler =
            RtlVirtualUnwind(0x2, base, walk.rip, fn, &next, &handler_data, &establisher, NULL);
        if ((DWORD64)target_frame == establisher && (DWORD64)target_frame)
            break;
        if (handler) {
            record->exception_flags |= EXCEPTION_UNWINDING;
            DISPATCHER_CONTEXT disp = { walk.rip, base, fn, establisher, (DWORD64)target_ip, &walk,
                                        handler, handler_data, NULL, 0, 0 };
            host_handler(handler)(record, (void *)establisher, &walk, &disp);
        }
        if (establisher == (DWORD64)target_frame)
            break;
        walk = next;
    }
    context->rip = (DWORD64)target_ip;
    context->rax = (DWORD64)retval;
    screw_restore_context(context);
}

void WINAPI RtlUnwindEx(void *target_frame, void *target_ip, EXCEPTION_RECORD *record, void *retval,
                        CONTEXT *context, void *history)
{
    EXCEPTION_RECORD local;
    if (!record) {
        memset(&local, 0, sizeof local);
        local.exception_code = STATUS_UNWIND_CONSOLIDATE;
        local.exception_flags = EXCEPTION_UNWINDING;
        record = &local;
    }
    if (target_frame)
        record->exception_flags |= EXCEPTION_UNWINDING;
    else
        record->exception_flags |= EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND;

    CONTEXT start;
    RtlCaptureContext(&start);
    dispatch_unwind(record, target_frame, target_ip, retval, &start);
    __builtin_unreachable();
}

static void WINAPI RtlUnwind(void *target_frame, void *target_ip, EXCEPTION_RECORD *record, void *retval)
{
    RtlUnwindEx(target_frame, target_ip, record, retval, NULL, NULL);
}

LONG WINAPI screw_c_specific_handler(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *disp_)
{
    DISPATCHER_CONTEXT *disp = disp_;
    SCOPE_TABLE *table = disp->handler_data;
    DWORD64 base = disp->image_base;
    DWORD rva = (DWORD)(disp->control_pc - base);

    if (record->exception_flags & EXCEPTION_UNWINDING) {
        for (DWORD i = 0; i < table->count; i++) {
            SCOPE_RECORD *s = &table->records[i];
            if (rva < s->begin || rva >= s->end)
                continue;
            if (s->target || (DWORD64)disp->target_ip == base + s->handler)
                continue;
            ((scope_finally)(base + s->handler))(TRUE, frame);
        }
        return ExceptionContinueSearch;
    }

    for (DWORD i = 0; i < table->count; i++) {
        SCOPE_RECORD *s = &table->records[i];
        if (rva < s->begin || rva >= s->end || !s->target)
            continue;
        LONG result = 1;
        if (s->handler != 1) {
            void *pointers[2] = { record, context };
            result = ((scope_filter)(base + s->handler))(pointers, frame);
        }
        if (result < 0)
            return ExceptionContinueExecution;
        if (result > 0)
            RtlUnwindEx(frame, (void *)(base + s->target), record, (void *)(ULONG_PTR)record->exception_code,
                        context, NULL);
    }
    return ExceptionContinueSearch;
}

void screw_raise_exception(DWORD code, DWORD flags, DWORD argc, const ULONG_PTR *args)
{
    EXCEPTION_RECORD record;
    memset(&record, 0, sizeof record);
    record.exception_code = code;
    record.exception_flags = flags;
    record.number_parameters = argc > EXCEPTION_MAXIMUM_PARAMETERS ? EXCEPTION_MAXIMUM_PARAMETERS : argc;
    for (DWORD i = 0; i < record.number_parameters && args; i++)
        record.exception_information[i] = args[i];

    CONTEXT context;
    RtlCaptureContext(&context);
    record.exception_address = (void *)context.rip;
    dispatch(&record, &context);

    screw_unhandled(&record, &context);
}

static NTSTATUS WINAPI RtlRaiseException(EXCEPTION_RECORD *record)
{
    CONTEXT context;
    RtlCaptureContext(&context);
    record->exception_address = (void *)context.rip;
    dispatch(record, &context);
    screw_unhandled(record, &context);
    return record->exception_code;
}

static BOOL WINAPI RtlAddFunctionTable(RUNTIME_FUNCTION *table, DWORD count, DWORD64 base) { return TRUE; }
static BOOL WINAPI RtlDeleteFunctionTable(RUNTIME_FUNCTION *table) { return TRUE; }
static BOOL WINAPI RtlInstallFunctionTableCallback(DWORD64 id, DWORD64 base, DWORD len, void *cb, void *ctx,
                                                   const WCHAR *dll) { return TRUE; }

static void WINAPI RtlRestoreContext(CONTEXT *context, EXCEPTION_RECORD *record)
{
    screw_restore_context(context);
}

static DWORD64 WINAPI RtlLookupFunctionTable(DWORD64 pc, DWORD64 *base, DWORD *size) { return 0; }

static BOOL WINAPI GetThreadContext(HANDLE thread, CONTEXT *context)
{
    if (thread == CURRENT_THREAD) {
        RtlCaptureContext(context);
        return TRUE;
    }
    return fail(ERROR_NOT_SUPPORTED);
}

static BOOL WINAPI SetThreadContext(HANDLE thread, const CONTEXT *context) { return TRUE; }

static void *WINAPI RtlPcToFileHeader(void *pc, void **base)
{
    struct module *m = module_at(pc);
    *base = m ? m->base : NULL;
    return m ? m->base : NULL;
}

BIT_TABLE(k32_unwind,
          BIT_FN(RtlCaptureContext), BIT_FN(RtlLookupFunctionEntry), BIT_FN(RtlVirtualUnwind),
          BIT_FN(RtlUnwindEx), BIT_FN(RtlUnwind), BIT_FN(RtlRaiseException),
          BIT_AS("__C_specific_handler", screw_c_specific_handler),
          BIT_FN(RtlAddFunctionTable), BIT_FN(RtlDeleteFunctionTable), BIT_FN(RtlInstallFunctionTableCallback),
          BIT_FN(RtlRestoreContext), BIT_FN(RtlLookupFunctionTable), BIT_FN(RtlPcToFileHeader),
          BIT_FN(GetThreadContext), BIT_FN(SetThreadContext));
