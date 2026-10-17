#include "k32.h"

#include <malloc.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_DECOMMIT 0x4000u
#define MEM_RELEASE 0x8000u
#define MEM_FREE 0x10000u
#define MEM_PRIVATE 0x20000u
#define MEM_IMAGE 0x1000000u

#define PAGE_NOACCESS 0x01u
#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
#define PAGE_WRITECOPY 0x08u
#define PAGE_EXECUTE 0x10u
#define PAGE_EXECUTE_READ 0x20u
#define PAGE_EXECUTE_READWRITE 0x40u
#define PAGE_EXECUTE_WRITECOPY 0x80u

#define HEAP_ZERO_MEMORY 0x08u
#define LMEM_ZEROINIT 0x40u

typedef struct {
    void *base;
    void *allocation_base;
    DWORD allocation_protect;
    WORD partition;
    SIZE_T region_size;
    DWORD state;
    DWORD protect;
    DWORD type;
} MEMORY_BASIC_INFORMATION;

typedef struct {
    DWORD length;
    DWORD load;
    ULONGLONG total_phys;
    ULONGLONG avail_phys;
    ULONGLONG total_page;
    ULONGLONG avail_page;
    ULONGLONG total_virtual;
    ULONGLONG avail_virtual;
    ULONGLONG avail_extended;
} MEMORYSTATUSEX;

static const char process_heap_tag[] = "screw-heap";

static int to_prot(DWORD p)
{
    switch (p & 0xFF) {
    case PAGE_READONLY: return PROT_READ;
    case PAGE_READWRITE:
    case PAGE_WRITECOPY: return PROT_READ | PROT_WRITE;
    case PAGE_EXECUTE: return PROT_EXEC;
    case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY: return PROT_READ | PROT_WRITE | PROT_EXEC;
    default: return PROT_NONE;
    }
}

static DWORD from_prot(const char *perms)
{
    bool r = perms[0] == 'r', w = perms[1] == 'w', x = perms[2] == 'x';
    if (x)
        return w ? PAGE_EXECUTE_READWRITE : r ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    if (w)
        return PAGE_READWRITE;
    return r ? PAGE_READONLY : PAGE_NOACCESS;
}

static uintptr_t page_down(uintptr_t a) { return a & ~(uintptr_t)4095; }
static size_t page_up(size_t n) { return (n + 4095) & ~(size_t)4095; }

static LPVOID WINAPI VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD protect)
{
    if (!size) {
        last_error_set(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    int prot = type & MEM_COMMIT ? to_prot(protect) : PROT_NONE;
    uintptr_t start = page_down((uintptr_t)addr);
    size_t len = page_up((uintptr_t)addr + size - start);
    if (addr && !(type & MEM_RESERVE)) {
        if (mprotect((void *)start, len, prot)) {
            last_error_set(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        return addr;
    }
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | (type & MEM_COMMIT ? 0 : MAP_NORESERVE);
    if (addr)
        flags |= MAP_FIXED_NOREPLACE;
    void *p = mmap(addr ? (void *)start : NULL, addr ? len : page_up(size), prot, flags, -1, 0);
    if (p == MAP_FAILED && addr && errno == EEXIST && !mprotect((void *)start, len, prot))
        return (void *)start;
    if (p == MAP_FAILED) {
        last_error_set(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    return p;
}

static LPVOID WINAPI VirtualAllocEx(HANDLE proc, LPVOID addr, SIZE_T size, DWORD type, DWORD protect)
{
    return VirtualAlloc(addr, size, type, protect);
}

static bool region_of(uintptr_t addr, uintptr_t *start, uintptr_t *end, char perms[5])
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f)
        return false;
    char line[512];
    bool found = false;
    while (!found && fgets(line, sizeof line, f)) {
        unsigned long s, e;
        if (sscanf(line, "%lx-%lx %4s", &s, &e, perms) == 3 && addr >= s && addr < e) {
            *start = s;
            *end = e;
            found = true;
        }
    }
    fclose(f);
    return found;
}

static BOOL WINAPI VirtualFree(LPVOID addr, SIZE_T size, DWORD type)
{
    if (type & MEM_RELEASE) {
        uintptr_t s, e;
        char perms[5];
        if (!size && region_of((uintptr_t)addr, &s, &e, perms))
            size = e - (uintptr_t)addr;
        return munmap(addr, page_up(size)) ? fail(ERROR_INVALID_PARAMETER) : TRUE;
    }
    if (type & MEM_DECOMMIT) {
        uintptr_t start = page_down((uintptr_t)addr);
        size_t len = page_up((uintptr_t)addr + size - start);
        madvise((void *)start, len, MADV_DONTNEED);
        mprotect((void *)start, len, PROT_NONE);
        return TRUE;
    }
    return fail(ERROR_INVALID_PARAMETER);
}

static BOOL WINAPI VirtualFreeEx(HANDLE proc, LPVOID addr, SIZE_T size, DWORD type)
{
    return VirtualFree(addr, size, type);
}

static BOOL WINAPI VirtualProtect(LPVOID addr, SIZE_T size, DWORD protect, LPDWORD old)
{
    uintptr_t start = page_down((uintptr_t)addr), s, e;
    char perms[5] = "rw-p";
    if (old)
        *old = region_of(start, &s, &e, perms) ? from_prot(perms) : PAGE_READWRITE;
    size_t len = page_up((uintptr_t)addr + size - start);
    return mprotect((void *)start, len, to_prot(protect)) ? fail(ERROR_INVALID_PARAMETER) : TRUE;
}

static BOOL WINAPI VirtualProtectEx(HANDLE proc, LPVOID addr, SIZE_T size, DWORD protect, LPDWORD old)
{
    return VirtualProtect(addr, size, protect, old);
}

static SIZE_T WINAPI VirtualQuery(LPCVOID addr, MEMORY_BASIC_INFORMATION *info, SIZE_T len)
{
    if (len < sizeof *info)
        return fail(ERROR_INSUFFICIENT_BUFFER);
    uintptr_t a = page_down((uintptr_t)addr), s, e;
    char perms[5];
    memset(info, 0, sizeof *info);
    info->base = (void *)a;
    if (!region_of(a, &s, &e, perms)) {
        info->state = MEM_FREE;
        info->protect = PAGE_NOACCESS;
        info->region_size = 4096;
        return sizeof *info;
    }
    info->allocation_base = (void *)s;
    info->region_size = e - a;
    info->protect = from_prot(perms);
    info->allocation_protect = info->protect;
    info->state = MEM_COMMIT;
    info->type = MEM_PRIVATE;
    return sizeof *info;
}

static SIZE_T WINAPI VirtualQueryEx(HANDLE proc, LPCVOID addr, MEMORY_BASIC_INFORMATION *info, SIZE_T len)
{
    return VirtualQuery(addr, info, len);
}

static BOOL WINAPI VirtualLock(LPVOID addr, SIZE_T size) { return TRUE; }
static BOOL WINAPI VirtualUnlock(LPVOID addr, SIZE_T size) { return TRUE; }

static BOOL WINAPI FlushInstructionCache(HANDLE proc, LPCVOID addr, SIZE_T size) { return TRUE; }

static HANDLE WINAPI GetProcessHeap(void) { return (HANDLE)process_heap_tag; }

static HANDLE WINAPI HeapCreate(DWORD options, SIZE_T initial, SIZE_T max) { return (HANDLE)process_heap_tag; }
static BOOL WINAPI HeapDestroy(HANDLE heap) { return TRUE; }

static LPVOID WINAPI HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size)
{
    void *p = flags & HEAP_ZERO_MEMORY ? calloc(1, size ? size : 1) : malloc(size ? size : 1);
    if (!p)
        last_error_set(ERROR_NOT_ENOUGH_MEMORY);
    return p;
}

static LPVOID WINAPI HeapReAlloc(HANDLE heap, DWORD flags, LPVOID mem, SIZE_T size)
{
    size_t old = mem ? malloc_usable_size(mem) : 0;
    void *p = realloc(mem, size ? size : 1);
    if (!p) {
        last_error_set(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if ((flags & HEAP_ZERO_MEMORY) && size > old)
        memset((char *)p + old, 0, size - old);
    return p;
}

static BOOL WINAPI HeapFree(HANDLE heap, DWORD flags, LPVOID mem)
{
    free(mem);
    return TRUE;
}

static SIZE_T WINAPI HeapSize(HANDLE heap, DWORD flags, LPCVOID mem)
{
    return mem ? malloc_usable_size((void *)mem) : (SIZE_T)-1;
}

static BOOL WINAPI HeapValidate(HANDLE heap, DWORD flags, LPCVOID mem) { return TRUE; }
static SIZE_T WINAPI HeapCompact(HANDLE heap, DWORD flags) { return 0; }
static BOOL WINAPI HeapSetInformation(HANDLE heap, int cls, void *info, SIZE_T len) { return TRUE; }

static DWORD WINAPI GetProcessHeaps(DWORD n, HANDLE *out)
{
    if (n && out)
        out[0] = GetProcessHeap();
    return 1;
}

static HANDLE WINAPI LocalAlloc(UINT flags, SIZE_T size)
{
    return HeapAlloc(NULL, flags & LMEM_ZEROINIT ? HEAP_ZERO_MEMORY : 0, size);
}

static HANDLE WINAPI LocalReAlloc(HANDLE mem, SIZE_T size, UINT flags)
{
    return HeapReAlloc(NULL, flags & LMEM_ZEROINIT ? HEAP_ZERO_MEMORY : 0, mem, size);
}

static HANDLE WINAPI LocalFree(HANDLE mem)
{
    free(mem);
    return NULL;
}

static LPVOID WINAPI LocalLock(HANDLE mem) { return mem; }
static BOOL WINAPI LocalUnlock(HANDLE mem) { return TRUE; }
static SIZE_T WINAPI LocalSize(HANDLE mem) { return mem ? malloc_usable_size(mem) : 0; }

static HANDLE WINAPI GlobalAlloc(UINT flags, SIZE_T size) { return LocalAlloc(flags, size); }
static HANDLE WINAPI GlobalReAlloc(HANDLE mem, SIZE_T size, UINT flags) { return LocalReAlloc(mem, size, flags); }
static HANDLE WINAPI GlobalFree(HANDLE mem) { return LocalFree(mem); }
static LPVOID WINAPI GlobalLock(HANDLE mem) { return mem; }
static BOOL WINAPI GlobalUnlock(HANDLE mem) { return TRUE; }
static SIZE_T WINAPI GlobalSize(HANDLE mem) { return LocalSize(mem); }
static HANDLE WINAPI GlobalHandle(LPCVOID mem) { return (HANDLE)mem; }

static BOOL WINAPI GlobalMemoryStatusEx(MEMORYSTATUSEX *st)
{
    long pages = sysconf(_SC_PHYS_PAGES), avail = sysconf(_SC_AVPHYS_PAGES), size = sysconf(_SC_PAGESIZE);
    st->total_phys = (ULONGLONG)pages * size;
    st->avail_phys = (ULONGLONG)avail * size;
    st->load = pages ? 100 - (DWORD)(avail * 100 / pages) : 0;
    st->total_page = st->total_phys * 2;
    st->avail_page = st->avail_phys * 2;
    st->total_virtual = 0x7FFFFFFE0000ull;
    st->avail_virtual = 0x7FFF00000000ull;
    st->avail_extended = 0;
    return TRUE;
}

static BOOL WINAPI IsBadReadPtr(LPCVOID p, ULONG_PTR n)
{
    uintptr_t s, e;
    char perms[5];
    return !p || !region_of((uintptr_t)p, &s, &e, perms) || perms[0] != 'r';
}

static BOOL WINAPI IsBadWritePtr(LPVOID p, ULONG_PTR n)
{
    uintptr_t s, e;
    char perms[5];
    return !p || !region_of((uintptr_t)p, &s, &e, perms) || perms[1] != 'w';
}

BIT_TABLE(k32_memory,
          BIT_FN(VirtualAlloc), BIT_FN(VirtualAllocEx), BIT_FN(VirtualFree), BIT_FN(VirtualFreeEx),
          BIT_FN(VirtualProtect), BIT_FN(VirtualProtectEx), BIT_FN(VirtualQuery), BIT_FN(VirtualQueryEx),
          BIT_FN(VirtualLock), BIT_FN(VirtualUnlock), BIT_FN(FlushInstructionCache),
          BIT_FN(GetProcessHeap), BIT_FN(GetProcessHeaps), BIT_FN(HeapCreate), BIT_FN(HeapDestroy),
          BIT_FN(HeapAlloc), BIT_FN(HeapReAlloc), BIT_FN(HeapFree), BIT_FN(HeapSize),
          BIT_FN(HeapValidate), BIT_FN(HeapCompact), BIT_FN(HeapSetInformation),
          BIT_FN(LocalAlloc), BIT_FN(LocalReAlloc), BIT_FN(LocalFree),
          BIT_FN(LocalLock), BIT_FN(LocalUnlock), BIT_FN(LocalSize),
          BIT_FN(GlobalAlloc), BIT_FN(GlobalReAlloc), BIT_FN(GlobalFree),
          BIT_FN(GlobalLock), BIT_FN(GlobalUnlock), BIT_FN(GlobalSize), BIT_FN(GlobalHandle),
          BIT_FN(GlobalMemoryStatusEx), BIT_FN(IsBadReadPtr), BIT_FN(IsBadWritePtr));
