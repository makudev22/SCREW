#include "k32.h"

#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
#define PAGE_WRITECOPY 0x08u

#define FILE_MAP_COPY 0x0001u
#define FILE_MAP_WRITE 0x0002u
#define FILE_MAP_READ 0x0004u

#define MAX_VIEWS 256

struct mapping {
    struct object base;
    int fd;
    DWORD protect;
    uint64_t size;
};

struct view {
    void *addr;
    size_t len;
};

static struct view views[MAX_VIEWS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void mapping_destroy(struct object *o)
{
    struct mapping *m = (struct mapping *)o;
    if (m->fd >= 0)
        close(m->fd);
    free(m);
}

static HANDLE create_mapping(HANDLE file, DWORD protect, uint64_t size)
{
    struct mapping *m = calloc(1, sizeof *m);
    object_init(&m->base, OBJ_MAPPING, mapping_destroy);
    m->fd = -1;
    m->protect = protect;
    m->size = size;

    struct file *f = file_get(file);
    if (f) {
        struct stat st;
        if (!size && !fstat(f->fd, &st))
            m->size = st.st_size;
        m->fd = dup(f->fd);
    } else if (file == INVALID_HANDLE_VALUE && size) {
        m->fd = memfd_create("screw-map", MFD_CLOEXEC);
        if (m->fd < 0 || ftruncate(m->fd, size)) {
            mapping_destroy(&m->base);
            return fail(ERROR_NOT_ENOUGH_MEMORY), (HANDLE)NULL;
        }
    } else {
        mapping_destroy(&m->base);
        return fail(ERROR_INVALID_HANDLE), (HANDLE)NULL;
    }
    last_error_set(ERROR_SUCCESS);
    return handle_new(&m->base);
}

static HANDLE WINAPI CreateFileMappingA(HANDLE file, void *sa, DWORD protect, DWORD high, DWORD low, LPCSTR name)
{
    return create_mapping(file, protect, ((uint64_t)high << 32) | low);
}

static HANDLE WINAPI CreateFileMappingW(HANDLE file, void *sa, DWORD protect, DWORD high, DWORD low, LPCWSTR name)
{
    return create_mapping(file, protect, ((uint64_t)high << 32) | low);
}

static LPVOID WINAPI MapViewOfFile(HANDLE h, DWORD access, DWORD high, DWORD low, SIZE_T bytes)
{
    struct mapping *m = (struct mapping *)handle_get(h, OBJ_MAPPING);
    if (!m) {
        last_error_set(ERROR_INVALID_HANDLE);
        return NULL;
    }
    uint64_t offset = ((uint64_t)high << 32) | low;
    size_t len = bytes ? bytes : (size_t)(m->size - offset);
    int prot = access & FILE_MAP_WRITE ? PROT_READ | PROT_WRITE
               : access & FILE_MAP_COPY ? PROT_READ | PROT_WRITE : PROT_READ;
    int flags = access & (FILE_MAP_COPY) ? MAP_PRIVATE : MAP_SHARED;
    void *p = mmap(NULL, len, prot, flags, m->fd, offset);
    if (p == MAP_FAILED) {
        last_error_set(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_VIEWS; i++)
        if (!views[i].addr) {
            views[i].addr = p;
            views[i].len = len;
            break;
        }
    pthread_mutex_unlock(&lock);
    return p;
}

static LPVOID WINAPI MapViewOfFileEx(HANDLE h, DWORD access, DWORD high, DWORD low, SIZE_T bytes, void *base)
{
    return MapViewOfFile(h, access, high, low, bytes);
}

static BOOL WINAPI UnmapViewOfFile(LPCVOID addr)
{
    size_t len = 0;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < MAX_VIEWS; i++)
        if (views[i].addr == addr) {
            len = views[i].len;
            views[i].addr = NULL;
            break;
        }
    pthread_mutex_unlock(&lock);
    if (!len)
        return fail(ERROR_INVALID_PARAMETER);
    return munmap((void *)addr, len) ? fail_errno() : TRUE;
}

static BOOL WINAPI FlushViewOfFile(LPCVOID addr, SIZE_T bytes)
{
    size_t len = bytes;
    if (!len) {
        pthread_mutex_lock(&lock);
        for (int i = 0; i < MAX_VIEWS; i++)
            if (views[i].addr == addr)
                len = views[i].len;
        pthread_mutex_unlock(&lock);
    }
    return msync((void *)addr, len, MS_SYNC) ? fail_errno() : TRUE;
}

BIT_TABLE(k32_filemap,
          BIT_FN(CreateFileMappingA), BIT_FN(CreateFileMappingW),
          BIT_FN(MapViewOfFile), BIT_FN(MapViewOfFileEx), BIT_FN(UnmapViewOfFile), BIT_FN(FlushViewOfFile));
