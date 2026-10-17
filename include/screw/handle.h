#ifndef SCREW_HANDLE_H
#define SCREW_HANDLE_H

#include <screw/win.h>

#include <stdatomic.h>
#include <stdbool.h>

enum object_kind {
    OBJ_ANY,
    OBJ_FILE,
    OBJ_FIND,
    OBJ_WAITABLE,
    OBJ_MAPPING,
    OBJ_SOCKET,
    OBJ_KEY,
};

struct object {
    enum object_kind kind;
    atomic_int refs;
    void (*destroy)(struct object *o);
};

struct file {
    struct object base;
    int fd;
    char *path;
    bool console;
    bool delete_on_close;
    bool append;
};

void handles_init(void);

HANDLE handle_new(struct object *o);
struct object *handle_get(HANDLE h, enum object_kind kind);
bool handle_close(HANDLE h);
HANDLE handle_dup(HANDLE h);

void object_init(struct object *o, enum object_kind kind, void (*destroy)(struct object *));
void object_ref(struct object *o);
void object_unref(struct object *o);

struct file *file_get(HANDLE h);
HANDLE file_wrap(int fd, const char *path);
HANDLE std_handle(int index);
void std_handle_set(int index, HANDLE h);

#endif
