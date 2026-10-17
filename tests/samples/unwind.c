#include <stdio.h>
#include <windows.h>

typedef struct { DWORD begin, end, unwind; } RTF;

typedef RTF *(WINAPI *lookup_fn)(DWORD64, DWORD64 *, void *);
typedef void (WINAPI *capture_fn)(void *);
typedef void *(WINAPI *virtual_fn)(DWORD, DWORD64, DWORD64, RTF *, void *, void **, DWORD64 *, void *);

static lookup_fn Lookup;
static capture_fn Capture;
static virtual_fn Virtual;

#define RIP_OFF 0xF8
#define RSP_OFF 0x98

static DWORD64 ctx_rip(void *c) { return *(DWORD64 *)((char *)c + RIP_OFF); }
static DWORD64 ctx_rsp(void *c) { return *(DWORD64 *)((char *)c + RSP_OFF); }

static DWORD begin_at(void *pc)
{
    DWORD64 base;
    RTF *f = Lookup((DWORD64)pc, &base, NULL);
    return f ? f->begin : 0;
}

static unsigned char context[4096] __attribute__((aligned(16)));
static DWORD chain[8];
static int depth_count;
static DWORD64 last_rsp;
static int rsp_monotonic = 1;

__attribute__((noinline)) static void f0(void)
{
    Capture(context);
    last_rsp = ctx_rsp(context);
    for (int i = 0; i < 8; i++) {
        DWORD64 rip = ctx_rip(context), base, est;
        RTF *f = Lookup(rip, &base, NULL);
        if (!f)
            break;
        void *data;
        Virtual(0, base, rip, f, context, &data, &est, NULL);
        DWORD64 rsp = ctx_rsp(context);
        if (rsp <= last_rsp)
            rsp_monotonic = 0;
        last_rsp = rsp;
        if (!ctx_rip(context))
            break;
        chain[depth_count++] = begin_at((void *)ctx_rip(context));
        if (depth_count >= 4)
            break;
    }
}

__attribute__((noinline)) static void f1(void) { f0(); __asm__ volatile("" ::: "memory"); }
__attribute__((noinline)) static void f2(void) { f1(); __asm__ volatile("" ::: "memory"); }
__attribute__((noinline)) static void f3(void) { f2(); __asm__ volatile("" ::: "memory"); }

int main(void)
{
    HMODULE k = GetModuleHandleA("kernel32.dll");
    Lookup = (lookup_fn)GetProcAddress(k, "RtlLookupFunctionEntry");
    Capture = (capture_fn)GetProcAddress(k, "RtlCaptureContext");
    Virtual = (virtual_fn)GetProcAddress(k, "RtlVirtualUnwind");
    printf("apis: %s\n", Lookup && Capture && Virtual ? "ok" : "missing");
    if (!Lookup || !Capture || !Virtual)
        return 1;

    DWORD want[4] = { begin_at((void *)f1), begin_at((void *)f2), begin_at((void *)f3), begin_at((void *)main) };
    f3();

    printf("frames walked: %d\n", depth_count);
    int match = depth_count == 4;
    for (int i = 0; i < 4 && i < depth_count; i++)
        if (chain[i] != want[i])
            match = 0;
    printf("frame chain f1..main: %s\n", match ? "ok" : "BAD");
    printf("rsp rises each frame: %s\n", rsp_monotonic ? "ok" : "BAD");
    printf("lookup rejects junk: %s\n", begin_at((void *)0x1234) == 0 ? "ok" : "BAD");
    return 0;
}
