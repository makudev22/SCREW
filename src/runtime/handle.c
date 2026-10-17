#include <screw/handle.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HANDLE_STEP 4

static struct object **table;
static size_t capacity;
static size_t free_hint = 1;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static HANDLE std[3];

void object_init(struct object *o, enum object_kind kind, void (*destroy)(struct object *))
{
    o->kind = kind;
    atomic_init(&o->refs, 1);
    o->destroy = destroy;
}

void object_ref(struct object *o) { atomic_fetch_add(&o->refs, 1); }

void object_unref(struct object *o)
{
    if (atomic_fetch_sub(&o->refs, 1) == 1 && o->destroy)
        o->destroy(o);
}

static size_t index_of(HANDLE h)
{
    uintptr_t v = (uintptr_t)h;
    if (!v || v % HANDLE_STEP)
        return 0;
    return v / HANDLE_STEP;
}

HANDLE handle_new(struct object *o)
{
    pthread_mutex_lock(&lock);
    size_t i = free_hint;
    while (i < capacity && table[i])
        i++;
    if (i >= capacity) {
        size_t cap = capacity ? capacity * 2 : 256;
        table = realloc(table, cap * sizeof *table);
        memset(table + capacity, 0, (cap - capacity) * sizeof *table);
        capacity = cap;
    }
    table[i] = o;
    free_hint = i + 1;
    pthread_mutex_unlock(&lock);
    return (HANDLE)(uintptr_t)(i * HANDLE_STEP);
}

struct object *handle_get(HANDLE h, enum object_kind kind)
{
    size_t i = index_of(h);
    struct object *o = NULL;
    pthread_mutex_lock(&lock);
    if (i && i < capacity)
        o = table[i];
    pthread_mutex_unlock(&lock);
    if (o && kind != OBJ_ANY && o->kind != kind)
        return NULL;
    return o;
}

bool handle_close(HANDLE h)
{
    size_t i = index_of(h);
    struct object *o = NULL;
    pthread_mutex_lock(&lock);
    if (i && i < capacity) {
        o = table[i];
        table[i] = NULL;
        if (i < free_hint)
            free_hint = i;
    }
    pthread_mutex_unlock(&lock);
    if (!o)
        return false;
    object_unref(o);
    return true;
}

HANDLE handle_dup(HANDLE h)
{
    struct object *o = handle_get(h, OBJ_ANY);
    if (!o)
        return NULL;
    object_ref(o);
    return handle_new(o);
}

static void file_destroy(struct object *o)
{
    struct file *f = (struct file *)o;
    if (f->fd > 2)
        close(f->fd);
    if (f->delete_on_close && f->path)
        unlink(f->path);
    free(f->path);
    free(f);
}

HANDLE file_wrap(int fd, const char *path)
{
    struct file *f = calloc(1, sizeof *f);
    object_init(&f->base, OBJ_FILE, file_destroy);
    f->fd = fd;
    f->path = path ? strdup(path) : NULL;
    f->console = isatty(fd);
    return handle_new(&f->base);
}

struct file *file_get(HANDLE h)
{
    return (struct file *)handle_get(h, OBJ_FILE);
}

void handles_init(void)
{
    for (int i = 0; i < 3; i++)
        std[i] = file_wrap(i, NULL);
}

HANDLE std_handle(int index)
{
    return index >= 0 && index < 3 ? std[index] : INVALID_HANDLE_VALUE;
}

void std_handle_set(int index, HANDLE h)
{
    if (index >= 0 && index < 3)
        std[index] = h;
}
